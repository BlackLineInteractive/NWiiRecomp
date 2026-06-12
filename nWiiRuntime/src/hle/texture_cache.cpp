#include "runtime/texture_cache.h"
#include <iostream>

namespace nwii::runtime::hle {

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

Texture2D TextureCache::get_texture(CPUContext& ctx, uint32_t addr, uint32_t width, uint32_t height, uint32_t format) {
    TextureKey key = {addr, width, height, format};
    auto it = cache.find(key);
    if (it != cache.end()) {
        return it->second;
    }

    Image img = decode_texture(ctx, addr, width, height, format);
    Texture2D tex = LoadTextureFromImage(img);
    UnloadImage(img);

    cache[key] = tex;
    return tex;
}

Image TextureCache::decode_texture(CPUContext& ctx, uint32_t addr, uint32_t width, uint32_t height, uint32_t format) {
    Image img = GenImageColor(width, height, BLANK);
    Color* pixels = (Color*)img.data;

    switch (format) {
        case (uint32_t)TextureFormat::I4: decode_i4(ctx, addr, width, height, pixels); break;
        case (uint32_t)TextureFormat::I8: decode_i8(ctx, addr, width, height, pixels); break;
        case (uint32_t)TextureFormat::IA4: decode_ia4(ctx, addr, width, height, pixels); break;
        case (uint32_t)TextureFormat::IA8: decode_ia8(ctx, addr, width, height, pixels); break;
        case (uint32_t)TextureFormat::RGB565: decode_rgb565(ctx, addr, width, height, pixels); break;
        case (uint32_t)TextureFormat::RGB5A3: decode_rgb5a3(ctx, addr, width, height, pixels); break;
        case (uint32_t)TextureFormat::RGBA8: decode_rgba8(ctx, addr, width, height, pixels); break;
        case (uint32_t)TextureFormat::CMPR: decode_cmpr(ctx, addr, width, height, pixels); break;
        default:
            std::cout << "[TextureCache] Unknown texture format: " << format << std::endl;
            for (uint32_t i = 0; i < width * height; i++) pixels[i] = MAGENTA;
            break;
    }

    return img;
}

// Helper to handle GameCube texture tiling
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
    // 8x8 tiles, 4bpp
    for (uint32_t y = 0; y < height; y++) {
        for (uint32_t x = 0; x < width; x+=2) {
            uint32_t offset = get_tile_address(x, y, 8, 8, 4, width);
            uint8_t b = ctx.mmu.read8(addr + offset);
            uint8_t i1 = (b >> 4) * 0x11;
            uint8_t i2 = (b & 0x0F) * 0x11;
            out[y * width + x] = {i1, i1, i1, 255};
            if (x+1 < width) out[y * width + x + 1] = {i2, i2, i2, 255};
        }
    }
}

void TextureCache::decode_i8(CPUContext& ctx, uint32_t addr, uint32_t width, uint32_t height, Color* out) {
    // 8x4 tiles, 8bpp
    for (uint32_t y = 0; y < height; y++) {
        for (uint32_t x = 0; x < width; x++) {
            uint32_t offset = get_tile_address(x, y, 8, 4, 8, width);
            uint8_t i = ctx.mmu.read8(addr + offset);
            out[y * width + x] = {i, i, i, 255};
        }
    }
}

void TextureCache::decode_ia4(CPUContext& ctx, uint32_t addr, uint32_t width, uint32_t height, Color* out) {
    // 8x4 tiles, 8bpp
    for (uint32_t y = 0; y < height; y++) {
        for (uint32_t x = 0; x < width; x++) {
            uint32_t offset = get_tile_address(x, y, 8, 4, 8, width);
            uint8_t b = ctx.mmu.read8(addr + offset);
            uint8_t a = (b >> 4) * 0x11;
            uint8_t i = (b & 0x0F) * 0x11;
            out[y * width + x] = {i, i, i, a};
        }
    }
}

void TextureCache::decode_ia8(CPUContext& ctx, uint32_t addr, uint32_t width, uint32_t height, Color* out) {
    // 4x4 tiles, 16bpp
    for (uint32_t y = 0; y < height; y++) {
        for (uint32_t x = 0; x < width; x++) {
            uint32_t offset = get_tile_address(x, y, 4, 4, 16, width);
            uint8_t a = ctx.mmu.read8(addr + offset);
            uint8_t i = ctx.mmu.read8(addr + offset + 1);
            out[y * width + x] = {i, i, i, a};
        }
    }
}

void TextureCache::decode_rgb565(CPUContext& ctx, uint32_t addr, uint32_t width, uint32_t height, Color* out) {
    // 4x4 tiles, 16bpp
    for (uint32_t y = 0; y < height; y++) {
        for (uint32_t x = 0; x < width; x++) {
            uint32_t offset = get_tile_address(x, y, 4, 4, 16, width);
            uint16_t c = ctx.mmu.read16(addr + offset);
            uint8_t r = ((c >> 11) & 0x1F) * 255 / 31;
            uint8_t g = ((c >> 5) & 0x3F) * 255 / 63;
            uint8_t b = (c & 0x1F) * 255 / 31;
            out[y * width + x] = {r, g, b, 255};
        }
    }
}

void TextureCache::decode_rgb5a3(CPUContext& ctx, uint32_t addr, uint32_t width, uint32_t height, Color* out) {
    // 4x4 tiles, 16bpp
    for (uint32_t y = 0; y < height; y++) {
        for (uint32_t x = 0; x < width; x++) {
            uint32_t offset = get_tile_address(x, y, 4, 4, 16, width);
            uint16_t c = ctx.mmu.read16(addr + offset);
            if (c & 0x8000) { // RGB555
                uint8_t r = ((c >> 10) & 0x1F) * 255 / 31;
                uint8_t g = ((c >> 5) & 0x1F) * 255 / 31;
                uint8_t b = (c & 0x1F) * 255 / 31;
                out[y * width + x] = {r, g, b, 255};
            } else { // RGB4A3
                uint8_t a = ((c >> 12) & 0x7) * 255 / 7;
                uint8_t r = ((c >> 8) & 0xF) * 255 / 15;
                uint8_t g = ((c >> 4) & 0xF) * 255 / 15;
                uint8_t b = (c & 0xF) * 255 / 15;
                out[y * width + x] = {r, g, b, a};
            }
        }
    }
}

void TextureCache::decode_rgba8(CPUContext& ctx, uint32_t addr, uint32_t width, uint32_t height, Color* out) {
    // 4x4 tiles, 32bpp (separated into AR and GB blocks of 32 bytes each)
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

static Color unpack_rgb565(uint16_t c) {
    return {
        (uint8_t)(((c >> 11) & 0x1F) * 255 / 31),
        (uint8_t)(((c >> 5) & 0x3F) * 255 / 63),
        (uint8_t)((c & 0x1F) * 255 / 31),
        255
    };
}

void TextureCache::decode_cmpr(CPUContext& ctx, uint32_t addr, uint32_t width, uint32_t height, Color* out) {
    // 8x8 tiles composed of four 4x4 blocks (8 bytes each). CMPR is DXT1 with swapped bytes and endianness.
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
                        pal[2] = { (uint8_t)((pal[0].r + pal[1].r)/2), (uint8_t)((pal[0].g + pal[1].g)/2), (uint8_t)((pal[0].b + pal[1].b)/2), 255 };
                        pal[3] = { 0, 0, 0, 0 }; // Transparent
                    }

                    for (int py = 0; py < 4; py++) {
                        for (int px = 0; px < 4; px++) {
                            int shift = 30 - 2 * (py * 4 + px); // CMPR bit order is backwards compared to DXT1? Actually standard big-endian reads
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
