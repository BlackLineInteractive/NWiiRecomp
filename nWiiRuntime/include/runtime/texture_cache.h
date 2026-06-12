#pragma once

#include "runtime/cpu_context.h"
#include <raylib.h>
#include <unordered_map>
#include <cstdint>

namespace nwii::runtime::hle {

enum class TextureFormat {
    I4 = 0,
    I8 = 1,
    IA4 = 2,
    IA8 = 3,
    RGB565 = 4,
    RGB5A3 = 5,
    RGBA8 = 6,
    CMPR = 14
};

struct TextureKey {
    uint32_t addr;
    uint32_t width;
    uint32_t height;
    uint32_t format;

    bool operator==(const TextureKey& other) const {
        return addr == other.addr && width == other.width &&
               height == other.height && format == other.format;
    }
};

struct TextureKeyHash {
    std::size_t operator()(const TextureKey& k) const {
        return ((std::size_t)k.addr ^ (k.width << 16) ^ (k.height << 8) ^ k.format);
    }
};

class TextureCache {
public:
    static TextureCache& get();

    // Decode texture from guest memory, or return cached Texture2D
    Texture2D get_texture(CPUContext& ctx, uint32_t addr, uint32_t width, uint32_t height, uint32_t format);

    void clear();

private:
    TextureCache() = default;
    
    std::unordered_map<TextureKey, Texture2D, TextureKeyHash> cache;

    Image decode_texture(CPUContext& ctx, uint32_t addr, uint32_t width, uint32_t height, uint32_t format);
    
    void decode_i4(CPUContext& ctx, uint32_t addr, uint32_t width, uint32_t height, Color* out);
    void decode_i8(CPUContext& ctx, uint32_t addr, uint32_t width, uint32_t height, Color* out);
    void decode_ia4(CPUContext& ctx, uint32_t addr, uint32_t width, uint32_t height, Color* out);
    void decode_ia8(CPUContext& ctx, uint32_t addr, uint32_t width, uint32_t height, Color* out);
    void decode_rgb565(CPUContext& ctx, uint32_t addr, uint32_t width, uint32_t height, Color* out);
    void decode_rgb5a3(CPUContext& ctx, uint32_t addr, uint32_t width, uint32_t height, Color* out);
    void decode_rgba8(CPUContext& ctx, uint32_t addr, uint32_t width, uint32_t height, Color* out);
    void decode_cmpr(CPUContext& ctx, uint32_t addr, uint32_t width, uint32_t height, Color* out);
};

} // namespace nwii::runtime::hle
