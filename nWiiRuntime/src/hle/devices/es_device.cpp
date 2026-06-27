#include "runtime/devices.h"
#include "runtime/ios_kernel.h"
#include <iostream>

namespace nwii::runtime::devices {

class ESDevice : public IDevice {
public:
    const char* get_name() const override { return "/dev/es"; }
    bool matches_path(const std::string& path) const override {
        return path == get_name();
    }
    
    int32_t ioctl(CPUContext& ctx, const IpcRequest& req) override {
        std::cout << "[ES] ioctl cmd=" << req.ioctl_cmd << std::endl;
        return IPC_OK;
    }
    int32_t ioctlv(CPUContext& ctx, const IpcRequest& req) override {
        std::cout << "[ES] ioctlv cmd=" << req.ioctl_cmd << std::endl;
        return IPC_OK;
    }
};

std::unique_ptr<IDevice> create_es_device() {
    return std::make_unique<ESDevice>();
}

void register_all() {
    auto& kernel = IOSKernel::get();
    kernel.register_device(create_di_device());
    kernel.register_device(create_fs_device());
    kernel.register_device(create_stm_device());
    kernel.register_device(create_usb_device());
    kernel.register_device(create_es_device());
}

} // namespace nwii::runtime::devices
