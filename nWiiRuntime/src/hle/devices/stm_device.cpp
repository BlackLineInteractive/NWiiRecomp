#include "runtime/devices.h"
#include <iostream>

namespace nwii::runtime::devices {

class STMDevice : public IDevice {
public:
    const char* get_name() const override { return "/dev/stm"; }
    bool matches_path(const std::string& path) const override {
        return path.find("/dev/stm") == 0;
    }
    
    int32_t ioctl(CPUContext& ctx, const IpcRequest& req) override {
        std::cout << "[STM] ioctl cmd=" << req.ioctl_cmd << std::endl;
        if (req.ioctl_cmd == 0x1000) {
            return 1; // IPC_PENDING
        }
        return IPC_OK;
    }
    int32_t ioctlv(CPUContext& ctx, const IpcRequest& req) override {
        std::cout << "[STM] ioctlv cmd=" << req.ioctl_cmd << std::endl;
        return IPC_OK;
    }
};

std::unique_ptr<IDevice> create_stm_device() {
    return std::make_unique<STMDevice>();
}

} // namespace nwii::runtime::devices
