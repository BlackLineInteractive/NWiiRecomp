#include "runtime/texture_cache.h"
#include <iostream>

// GX texture decoding, kept bit-exact with Dolphin's TextureDecoder:
// tile-based layouts, IIII replication for intensity formats, RGB5A3
// dual encoding, the split AR/GB cache lines of RGBA8, TLUT-paletted
// C4/C8/C14X2, and CMPR's DXT1-with-big-endian-words scheme. Decoding
// runs on the CPU (like every GC emulator); only the decoded RGBA8
// image is uploaded to the GPU.

namespace nwii::runtime::hle {

using gx::g_state;

TextureCache& TextureCache::get() {
    static TextureCache instance;
    return instance;
}

void TextureCache::clear() {
    for (auto& pair : cache) {
        UnloadTexture(pair.second);
    }
    cache.clear();
}

// 5/6/4/3-bit channel expansion, matching Dolphin's ConvertNTo8 tables.
static inline uint8_t c5to8(uint32_t v) { return (uint8_t)((v << 3) | (v >> 2)); }
static inline uint8_t c6to8(uint32_t v) { return (uint8_t)((v << 2) | (v >> 4)); }
static inline uint8_t c4to8(uint32_t v) { return (uint8_t)((v << 4) | v); }
static inline uint8_t c3to8(uint32_t v) { return (uint8_t)((v << 5) | (v << 2) | (v >> 1)); }

uint32_t texture_data_size(uint32_t width, uint32_t height, uint32_t format) {
    uint32_t tw, th, bpp;
    switch ((TextureFormat)format) {
        case TextureFormat::I4:     tw = 8; th = 8; bpp = 4;  break;
        case TextureFormat::I8:     tw = 8; th = 4; bpp = 8;  break;
        case TextureFormat::IA4:    tw = 8; th = 4; bpp = 8;  break;
        case TextureFormat::IA8:    tw = 4; th = 4; bpp = 16; break;
        case TextureFormat::RGB565: tw = 4; th = 4; bpp = 16; break;
        case TextureFormat::RGB5A3: tw = 4; th = 4; bpp = 16; break;
        case TextureFormat::RGBA8:  tw = 4; th = 4; bpp = 32; break;
        case TextureFormat::C4:     tw = 8; th = 8; bpp = 4;  break;
        case TextureFormat::C8:     tw = 8; th = 4; bpp = 8;  break;
        case TextureFormat::C14X2:  tw = 4; th = 4; bpp = 16; break;
        case TextureFormat::CMPR:   tw = 8; th = 8; bpp = 4;  break;
        default:                    tw = 4; th = 4; bpp = 16; break;
    }
    uint32_t wt = (width + tw - 1) / tw;
    uint32_t ht = (height + th - 1) / th;
    return wt * ht * tw * th * bpp / 8;
}

Texture2D TextureCache::get_texture(CPUContext& ctx, const gx::TexStage& stage) {
    // Sampled content hash: textures are routinely re-rendered in place
    // (fonts, video frames), so the address alone cannot identify content.
    uint32_t size = texture_data_size(stage.width, stage.height, stage.format);
    uint32_t stride = size > 32 * 4096 ? size / 4096 : 32;
    uint32_t hash = 2166136261u;
    for (uint32_t off = 0; off + 4 <= size; off += stride)
        hash = (hash ^ ctx.mmu.read32(stage.base_addr + off)) * 16777619u;

    TextureKey key = {stage.base_addr, stage.width, stage.height,
                      stage.format, stage.tlut_offset, stage.tlut_format, hash};
    auto it = cache.find(key);
    if (it != cache.end()) {
        return it->second;
    }

    // Content changed or new texture: cap the cache so per-frame regenerated
    // textures (video splash frames) cannot grow it without bound.
    if (cache.size() > 512)
        clear();

    Image img = decode_texture(ctx, stage);
    Texture2D tex = LoadTextureFromImage(img);
    // Textures are decoded with a single mip level; the default trilinear
    // sampler would sample missing mips (black). Point matches GX_NEAR;
    // bilinear would need GX_TF filter state, tracked later.
    SetTextureFilter(tex, TEXTURE_FILTER_POINT);
    UnloadImage(img);

    cache[key] = tex;
    return tex;
}

Image TextureCache::decode_texture(CPUContext& ctx, const gx::TexStage& stage) {
    uint32_t width = stage.width, height = stage.height;
    Image img = GenImageColor(width, height, BLANK);
    Color* pixels = (Color*)img.data;
    uint32_t addr = stage.base_addr;

    switch ((TextureFormat)stage.format) {
        case TextureFormat::I4:     decode_i4(ctx, addr, width, height, pixels); break;
        case TextureFormat::I8:     decode_i8(ctx, addr, width, height, pixels); break;
        case TextureFormat::IA4:    decode_ia4(ctx, addr, width, height, pixels); break;
        case TextureFormat::IA8:    decode_ia8(ctx, addr, width, height, pixels); break;
        case TextureFormat::RGB565: decode_rgb565(ctx, addr, width, height, pixels); break;
        case TextureFormat::RGB5A3: decode_rgb5a3(ctx, addr, width, height, pixels); break;
        case TextureFormat::RGBA8:  decode_rgba8(ctx, addr, width, height, pixels); break;
        case TextureFormat::C4:
        case TextureFormat::C8:
        case TextureFormat::C14X2:  decode_paletted(ctx, stage, pixels); break;
        case TextureFormat::CMPR:   decode_cmpr(ctx, addr, width, height, pixels); break;
        default:
            std::cout << "[TextureCache] Unknown texture format: "
                      << (int)stage.format << std::endl;
            for (uint32_t i = 0; i < width * height; i++) pixels[i] = MAGENTA;
            break;
    }

    return img;
}

// Byte offset of texel (x,y) in a tiled GC texture image.
static uint32_t get_tile_address(uint32_t x, uint32_t y, uint32_t bw, uint32_t bh, uint32_t bpp, uint32_t width) {
    uint32_t tiles_x = (width + bw - 1) / bw;
    uint32_t tx = x / bw;
    uint32_t ty = y / bh;
    uint32_t in_tile_x = x % bw;
    uint32_t in_tile_y = y % bh;

    uint32_t tile_idx = ty * tiles_x + tx;
    uint32_t tile_offset = tile_idx * bw * bh * bpp / 8;
    uint32_t in_tile_offset = (in_tile_y * bw + in_tile_x) * bpp / 8;

    return tile_offset + in_tile_offset;
}

void TextureCache::decode_i4(CPUContext& ctx, uint32_t addr, uint32_t width, uint32_t height, Color* out) {
    // 8x8 tiles, 4bpp. Intensity replicates into ALL four channels
    // including alpha (fonts rely on I-as-alpha).
    for (uint32_t y = 0; y < height; y++) {
        for (uint32_t x = 0; x < width; x+=2) {
            uint32_t offset = get_tile_address(x, y, 8, 8, 4, width);
            uint8_t b = ctx.mmu.read8(addr + offset);
            uint8_t i1 = c4to8(b >> 4);
            uint8_t i2 = c4to8(b & 0x0F);
            out[y * width + x] = {i1, i1, i1, i1};
            if (x+1 < width) out[y * width + x + 1] = {i2, i2, i2, i2};
        }
    }
}

void TextureCache::decode_i8(CPUContext& ctx, uint32_t addr, uint32_t width, uint32_t height, Color* out) {
    // 8x4 tiles, 8bpp. RGBA = IIII.
    for (uint32_t y = 0; y < height; y++) {
        for (uint32_t x = 0; x < width; x++) {
            uint32_t offset = get_tile_address(x, y, 8, 4, 8, width);
            uint8_t i = ctx.mmu.read8(addr + offset);
            out[y * width + x] = {i, i, i, i};
        }
    }
}

void TextureCache::decode_ia4(CPUContext& ctx, uint32_t addr, uint32_t width, uint32_t height, Color* out) {
    // 8x4 tiles, 8bpp: high nibble alpha, low nibble intensity.
    for (uint32_t y = 0; y < height; y++) {
        for (uint32_t x = 0; x < width; x++) {
            uint32_t offset = get_tile_address(x, y, 8, 4, 8, width);
            uint8_t b = ctx.mmu.read8(addr + offset);
            uint8_t a = c4to8(b >> 4);
            uint8_t i = c4to8(b & 0x0F);
            out[y * width + x] = {i, i, i, a};
        }
    }
}

void TextureCache::decode_ia8(CPUContext& ctx, uint32_t addr, uint32_t width, uint32_t height, Color* out) {
    // 4x4 tiles, 16bpp: first byte alpha, second intensity.
    for (uint32_t y = 0; y < height; y++) {
        for (uint32_t x = 0; x < width; x++) {
            uint32_t offset = get_tile_address(x, y, 4, 4, 16, width);
            uint8_t a = ctx.mmu.read8(addr + offset);
            uint8_t i = ctx.mmu.read8(addr + offset + 1);
            out[y * width + x] = {i, i, i, a};
        }
    }
}

static inline Color unpack_rgb565(uint16_t c) {
    return { c5to8((c >> 11) & 0x1F), c6to8((c >> 5) & 0x3F), c5to8(c & 0x1F), 255 };
}

static inline Color unpack_rgb5a3(uint16_t c) {
    if (c & 0x8000) { // RGB555, opaque
        return { c5to8((c >> 10) & 0x1F), c5to8((c >> 5) & 0x1F), c5to8(c & 0x1F), 255 };
    }
    return { c4to8((c >> 8) & 0xF), c4to8((c >> 4) & 0xF), c4to8(c & 0xF),
             c3to8((c >> 12) & 0x7) };
}

void TextureCache::decode_rgb565(CPUContext& ctx, uint32_t addr, uint32_t width, uint32_t height, Color* out) {
    // 4x4 tiles, 16bpp
    for (uint32_t y = 0; y < height; y++) {
        for (uint32_t x = 0; x < width; x++) {
            uint32_t offset = get_tile_address(x, y, 4, 4, 16, width);
            out[y * width + x] = unpack_rgb565(ctx.mmu.read16(addr + offset));
        }
    }
}

void TextureCache::decode_rgb5a3(CPUContext& ctx, uint32_t addr, uint32_t width, uint32_t height, Color* out) {
    // 4x4 tiles, 16bpp
    for (uint32_t y = 0; y < height; y++) {
        for (uint32_t x = 0; x < width; x++) {
            uint32_t offset = get_tile_address(x, y, 4, 4, 16, width);
            out[y * width + x] = unpack_rgb5a3(ctx.mmu.read16(addr + offset));
        }
    }
}

void TextureCache::decode_rgba8(CPUContext& ctx, uint32_t addr, uint32_t width, uint32_t height, Color* out) {
    // 4x4 tiles, 32bpp (two 32-byte cache lines per tile: AR then GB)
    uint32_t tiles_x = (width + 3) / 4;
    for (uint32_t y = 0; y < height; y++) {
        for (uint32_t x = 0; x < width; x++) {
            uint32_t tx = x / 4;
            uint32_t ty = y / 4;
            uint32_t tile_idx = ty * tiles_x + tx;
            uint32_t tile_offset = tile_idx * 64; // 32 bytes AR + 32 bytes GB

            uint32_t in_tile_x = x % 4;
            uint32_t in_tile_y = y % 4;
            uint32_t in_tile_offset = (in_tile_y * 4 + in_tile_x) * 2;

            uint16_t ar = ctx.mmu.read16(addr + tile_offset + in_tile_offset);
            uint16_t gb = ctx.mmu.read16(addr + tile_offset + 32 + in_tile_offset);

            out[y * width + x] = {
                (uint8_t)(ar & 0xFF),
                (uint8_t)(gb >> 8),
                (uint8_t)(gb & 0xFF),
                (uint8_t)(ar >> 8)
            };
        }
    }
}

void TextureCache::decode_paletted(CPUContext& ctx, const gx::TexStage& stage, Color* out) {
    // C4: 8x8 tiles 4bpp; C8: 8x4 tiles 8bpp; C14X2: 4x4 tiles 16bpp.
    // Each texel is an index into the TLUT bank; entry format per SETTLUT.
    uint32_t width = stage.width, height = stage.height, addr = stage.base_addr;
    auto lookup = [&](uint32_t index) -> Color {
        uint32_t off = stage.tlut_offset + index * 2;
        if (off + 1 >= sizeof(g_state.tlutMem)) return MAGENTA;
        uint16_t entry = (g_state.tlutMem[off] << 8) | g_state.tlutMem[off + 1];
        switch (stage.tlut_format) {
            case 0: { // IA8
                uint8_t a = entry >> 8, i = entry & 0xFF;
                return {i, i, i, a};
            }
            case 1: return unpack_rgb565(entry);
            default: return unpack_rgb5a3(entry);
        }
    };

    for (uint32_t y = 0; y < height; y++) {
        for (uint32_t x = 0; x < width; x++) {
            uint32_t index;
            switch ((TextureFormat)stage.format) {
                case TextureFormat::C4: {
                    uint32_t offset = get_tile_address(x & ~1u, y, 8, 8, 4, width);
                    uint8_t b = ctx.mmu.read8(addr + offset);
                    index = (x & 1) ? (b & 0x0F) : (b >> 4);
                    break;
                }
                case TextureFormat::C8: {
                    uint32_t offset = get_tile_address(x, y, 8, 4, 8, width);
                    index = ctx.mmu.read8(addr + offset);
                    break;
                }
                default: { // C14X2
                    uint32_t offset = get_tile_address(x, y, 4, 4, 16, width);
                    index = ctx.mmu.read16(addr + offset) & 0x3FFF;
                    break;
                }
            }
            out[y * width + x] = lookup(index);
        }
    }
}

void TextureCache::decode_cmpr(CPUContext& ctx, uint32_t addr, uint32_t width, uint32_t height, Color* out) {
    // 8x8 tiles of four 4x4 DXT1 blocks (8 bytes each). Unlike PC DXT1 the
    // u16 color words are big-endian and the 2-bit indices start at the TOP
    // bits of the big-endian index word.
    uint32_t tiles_x = (width + 7) / 8;
    for (uint32_t y = 0; y < height; y+=8) {
        for (uint32_t x = 0; x < width; x+=8) {
            uint32_t tx = x / 8;
            uint32_t ty = y / 8;
            uint32_t tile_offset = (ty * tiles_x + tx) * 32;

            for (int by = 0; by < 2; by++) {
                for (int bx = 0; bx < 2; bx++) {
                    uint32_t block_offset = tile_offset + (by * 2 + bx) * 8;
                    uint16_t c0 = ctx.mmu.read16(addr + block_offset);
                    uint16_t c1 = ctx.mmu.read16(addr + block_offset + 2);
                    uint32_t indices = ctx.mmu.read32(addr + block_offset + 4);

                    Color pal[4];
                    pal[0] = unpack_rgb565(c0);
                    pal[1] = unpack_rgb565(c1);
                    if (c0 > c1) {
                        pal[2] = { (uint8_t)((2*pal[0].r + pal[1].r)/3), (uint8_t)((2*pal[0].g + pal[1].g)/3), (uint8_t)((2*pal[0].b + pal[1].b)/3), 255 };
                        pal[3] = { (uint8_t)((pal[0].r + 2*pal[1].r)/3), (uint8_t)((pal[0].g + 2*pal[1].g)/3), (uint8_t)((pal[0].b + 2*pal[1].b)/3), 255 };
                    } else {
                        // Transparent mode: color 3 keeps the midpoint RGB with
                        // alpha 0 so bilinear edges don't bleed to black.
                        pal[2] = { (uint8_t)((pal[0].r + pal[1].r)/2), (uint8_t)((pal[0].g + pal[1].g)/2), (uint8_t)((pal[0].b + pal[1].b)/2), 255 };
                        pal[3] = { pal[2].r, pal[2].g, pal[2].b, 0 };
                    }

                    for (int py = 0; py < 4; py++) {
                        for (int px = 0; px < 4; px++) {
                            int shift = 30 - 2 * (py * 4 + px); // pixel 0 in the top bits
                            int idx = (indices >> shift) & 3;
                            uint32_t out_x = x + bx * 4 + px;
                            uint32_t out_y = y + by * 4 + py;
                            if (out_x < width && out_y < height) {
                                out[out_y * width + out_x] = pal[idx];
                            }
                        }
                    }
                }
            }
        }
    }
}

} // namespace nwii::runtime::hle
