#include "runtime/gx/renderer.h"
#include "runtime/config.h"

namespace nwii::runtime::gx {

std::unique_ptr<IRenderer> CreateRendererGL();
#ifdef __APPLE__
std::unique_ptr<IRenderer> CreateRendererMetal();
#endif

std::unique_ptr<IRenderer> IRenderer::Create() {
#ifdef __APPLE__
    if (Config::get().backend == Backend::Metal)
        return CreateRendererMetal();
#endif
    return CreateRendererGL();
}

} // namespace nwii::runtime::gx
