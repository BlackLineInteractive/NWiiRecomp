#pragma once
#include "runtime/gx_state.h"
#include <string>

namespace nwii::runtime::gx {

struct GeneratedShader {
    std::string vertex_source;
    std::string fragment_source;
};

// Generates a GLSL 330 core shader pair for the current GX state and primitive type.
GeneratedShader GenerateTEVShader(const GXState& state, uint8_t prim_type);

} // namespace nwii::runtime::gx
