#include "runtime/devices.h"
#include <iostream>

namespace nwii::runtime::devices {

class USBDevice : public IDevice {
public:
    const char* get_name() const override { return "/dev/usb"; }
    bool matches_path(const std::string& path) const override {
        return path.find("/dev/usb") == 0;
    }
    
    int32_t ioctl(CPUContext& ctx, const IpcRequest& req) override {
        std::cout << "[USB] ioctl cmd=" << req.ioctl_cmd << std::endl;
        return IPC_OK;
    }
    int32_t ioctlv(CPUContext& ctx, const IpcRequest& req) override {
        std::cout << "[USB] ioctlv cmd=" << req.ioctl_cmd << std::endl;
        return IPC_OK;
    }
};

std::unique_ptr<IDevice> create_usb_device() {
    return std::make_unique<USBDevice>();
}

} // namespace nwii::runtime::devices
