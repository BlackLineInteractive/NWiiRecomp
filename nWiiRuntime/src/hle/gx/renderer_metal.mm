#include "runtime/gx/renderer.h"
#include <iostream>

#ifdef __APPLE__
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

namespace nwii::runtime::gx {

class RendererMetal : public IRenderer {
public:
    RendererMetal() = default;
    ~RendererMetal() override = default;

    void Initialize(void* window) override {
        m_window = window;
        std::cout << "[Metal] Initializing Metal Renderer Stub..." << std::endl;
        m_device = MTLCreateSystemDefaultDevice();
        if (!m_device) {
            std::cerr << "[Metal] Failed to find a suitable Metal device." << std::endl;
            return;
        }
        m_commandQueue = [m_device newCommandQueue];
        std::cout << "[Metal] Using device: " << [[m_device name] UTF8String] << std::endl;
    }

    void Render(const std::vector<GXCommand>& commands) override {
        // TODO: Implement Metal rendering
        // 1. Get CAMetalLayer from SDL
        // 2. id<CAMetalDrawable> drawable = [layer nextDrawable];
        // 3. Create render pass descriptor
        // 4. Encode commands
        // 5. [commandBuffer presentDrawable:drawable];
    }

    void Present() override {
        // Implement presentation
    }

private:
    void* m_window = nullptr;
    id<MTLDevice> m_device;
    id<MTLCommandQueue> m_commandQueue;
};

std::unique_ptr<IRenderer> CreateRendererMetal() {
    return std::make_unique<RendererMetal>();
}

} // namespace nwii::runtime::gx
#endif
