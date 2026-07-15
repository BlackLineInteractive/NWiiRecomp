#pragma once
#include <vector>
#include <memory>
#include "runtime/gx_command.h"

namespace nwii::runtime::gx {

class IRenderer {
public:
    virtual ~IRenderer() = default;
    virtual void Initialize(void* window) = 0;
    virtual void Render(const std::vector<GXCommand>& commands) = 0;
    virtual void Present() = 0;
    
    static std::unique_ptr<IRenderer> Create();
};

} // namespace nwii::runtime::gx
