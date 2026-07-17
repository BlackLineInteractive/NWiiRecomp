#include "runtime/platform/platform.h"
#include "runtime/platform/gc_platform.h"
#include "runtime/platform/wii_platform.h"
#include "runtime/config.h"

namespace nwii::runtime::platform {

static std::unique_ptr<IPlatform> g_platform;

std::unique_ptr<IPlatform> IPlatform::create() {
    if (Config::get().platform == Platform::GameCube) {
        return std::make_unique<GCPlatform>();
    } else {
        return std::make_unique<WiiPlatform>();
    }
}

IPlatform& IPlatform::get() {
    if (!g_platform) {
        g_platform = create();
    }
    return *g_platform;
}

void GCPlatform::init() {
    
}

void WiiPlatform::init() {
    
}

} 
