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

    bool has_pos = false;
    bool has_norm = false;
    bool has_color = false;
    bool has_tex[8] = {false};

    uint8_t posMtxIdx = 0;
    uint8_t texMtxIdx[8] = {0};
};

struct GXCommand {
    GXCommandType type;

    uint16_t reg;
    uint32_t val;

    uint16_t length;

    
    uint8_t prim_type;
    std::vector<VertexData> vertices;

    std::vector<float> payload;
};

} 
