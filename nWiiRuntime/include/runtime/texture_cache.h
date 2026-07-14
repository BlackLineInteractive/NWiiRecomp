#pragma once

#include "runtime/cpu_context.h"
#include "runtime/gx_state.h"
#include <raylib.h>
#include <unordered_map>
#include <cstdint>

namespace nwii::runtime::hle {

// GX texture formats (TexImage0 format field).
enum class TextureFormat {
    I4 = 0,
    I8 = 1,
    IA4 = 2,
    IA8 = 3,
    RGB565 = 4,
    RGB5A3 = 5,
    RGBA8 = 6,
    C4 = 8,
    C8 = 9,
    C14X2 = 10,
    CMPR = 14
};

struct TextureKey {
    uint32_t addr;
    uint32_t width;
    uint32_t height;
    uint32_t format;
    uint32_t tlut_offset;
    uint32_t tlut_format;
    uint32_t data_hash;

    bool operator==(const TextureKey& other) const {
        return addr == other.addr && width == other.width &&
               height == other.height && format == other.format &&
               tlut_offset == other.tlut_offset &&
               tlut_format == other.tlut_format &&
               data_hash == other.data_hash;
    }
};

struct TextureKeyHash {
    std::size_t operator()(const TextureKey& k) const {
        std::size_t h = k.addr;
        h = h * 31 + k.width;
        h = h * 31 + k.height;
        h = h * 31 + k.format;
        h = h * 31 + k.tlut_offset;
        h = h * 31 + k.tlut_format;
        h = h * 31 + k.data_hash;
        return h;
    }
};

class TextureCache {
public:
    static TextureCache& get();

    // Decode the texture a BP TexImage stage points at (or return the
    // cached GPU texture). Handles paletted formats via the stage's TLUT
    // binding. Content changes are caught by a sampled data hash.
    Texture2D get_texture(CPUContext& ctx, const gx::TexStage& stage);

    void clear();

private:
    TextureCache() = default;

    std::unordered_map<TextureKey, Texture2D, TextureKeyHash> cache;

    Image decode_texture(CPUContext& ctx, const gx::TexStage& stage);

    void decode_i4(CPUContext& ctx, uint32_t addr, uint32_t width, uint32_t height, Color* out);
    void decode_i8(CPUContext& ctx, uint32_t addr, uint32_t width, uint32_t height, Color* out);
    void decode_ia4(CPUContext& ctx, uint32_t addr, uint32_t width, uint32_t height, Color* out);
    void decode_ia8(CPUContext& ctx, uint32_t addr, uint32_t width, uint32_t height, Color* out);
    void decode_rgb565(CPUContext& ctx, uint32_t addr, uint32_t width, uint32_t height, Color* out);
    void decode_rgb5a3(CPUContext& ctx, uint32_t addr, uint32_t width, uint32_t height, Color* out);
    void decode_rgba8(CPUContext& ctx, uint32_t addr, uint32_t width, uint32_t height, Color* out);
    void decode_cmpr(CPUContext& ctx, uint32_t addr, uint32_t width, uint32_t height, Color* out);
    void decode_paletted(CPUContext& ctx, const gx::TexStage& stage, Color* out);
};

// Byte size of a texture image in guest RAM (tile-rounded dimensions).
uint32_t texture_data_size(uint32_t width, uint32_t height, uint32_t format);

} // namespace nwii::runtime::hle
