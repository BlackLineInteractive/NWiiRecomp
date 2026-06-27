#pragma once
#include <vector>
#include "runtime/gx_command.h"

namespace nwii::runtime::gx {

class Renderer {
public:
    // Renders the list of commands using Raylib
    static void Render(const std::vector<GXCommand>& commands);
};

} // namespace nwii::runtime::gx
