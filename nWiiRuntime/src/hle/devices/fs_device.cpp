#include "runtime/devices.h"
#include <iostream>

namespace nwii::runtime::devices {

class FSDevice : public IDevice {
public:
    const char* get_name() const override { return "/dev/fs"; }
    bool matches_path(const std::string& path) const override {
        return path == get_name();
    }
};

std::unique_ptr<IDevice> create_fs_device() {
    return std::make_unique<FSDevice>();
}

} // namespace nwii::runtime::devices
