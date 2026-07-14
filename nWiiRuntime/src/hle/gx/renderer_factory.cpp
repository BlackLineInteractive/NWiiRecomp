#include "runtime/gx/renderer.h"
#include "runtime/config.h"

namespace nwii::runtime::gx {

class RendererGL; // Forward declaration (or we can just include renderer_gl.h)

// We declare the creation functions here
std::unique_ptr<IRenderer> CreateRendererGL();
#ifdef __APPLE__
std::unique_ptr<IRenderer> CreateRendererMetal();
#endif

std::unique_ptr<IRenderer> IRenderer::Create() {
    auto backend = Config::get().backend;
#ifdef __APPLE__
    if (backend == Backend::Metal) {
        return CreateRendererMetal();
    }
#endif
    return CreateRendererGL();
}

} // namespace nwii::runtime::gx
