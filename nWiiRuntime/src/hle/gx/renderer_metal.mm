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
        m_device = MTLCreateSystemDefaultDevice();
        if (!m_device) {
            std::cerr << "[Metal] No device found" << std::endl;
            return;
        }
        m_commandQueue = [m_device newCommandQueue];
        std::cout << "[Metal] " << [[m_device name] UTF8String] << std::endl;
    }

    void Render(const std::vector<GXCommand>& commands) override {
        
    }

    void Present() override {}

private:
    void* m_window = nullptr;
    id<MTLDevice> m_device;
    id<MTLCommandQueue> m_commandQueue;
};

std::unique_ptr<IRenderer> CreateRendererMetal() {
    return std::make_unique<RendererMetal>();
}

} 
#endif
