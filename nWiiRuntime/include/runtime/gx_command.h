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
};

struct GXCommand {
    GXCommandType type;
    
    // For BP, CP registers
    uint8_t reg;
    uint32_t val;
    
    // For XF registers
    uint16_t length;
    
    // For Draw Primitive
    int gl_mode;
    std::vector<VertexData> vertices;
};

} // namespace nwii::runtime::gx
