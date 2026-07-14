#pragma once
#include <cstdint>
#include <vector>

namespace nwii::runtime::gx {

enum class GXCommandType {
    BPRegister,
    CPRegister,
    XFRegister,
    DrawPrimitive
};

struct VertexData {
    float pos[3];
    float norm[3];
    float color[4];
    float tex[8][2];
    
    // Track which attributes are valid
    bool has_pos = false;
    bool has_norm = false;
    bool has_color = false;
    bool has_tex[8] = {false};
    
    // Matrix Indices
    uint8_t posMtxIdx = 0;
    uint8_t texMtxIdx[8] = {0};
};

struct GXCommand {
    GXCommandType type;
    
    // For BP, CP registers
    uint16_t reg;
    uint32_t val;
    
    // For XF registers
    uint16_t length;
    
    // For Draw Primitive: raw GX primitive opcode (0x80 quads, 0x90 tris,
    // 0x98 tristrip, 0xA0 trifan, 0xA8 lines, 0xB0 linestrip, 0xB8 points).
    uint8_t prim_type;
    std::vector<VertexData> vertices;
    
    // For payload data like XF matrices
    std::vector<float> payload;
};

} // namespace nwii::runtime::gx
