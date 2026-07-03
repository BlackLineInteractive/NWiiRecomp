#pragma once
#include "runtime/ios_device.h"
#include <memory>
#include <vector>

namespace nwii::runtime {

class IOSKernel {
public:
    static IOSKernel& get() {
        static IOSKernel instance;
        return instance;
    }

    void init();
    void register_device(std::unique_ptr<IDevice> device);

    int32_t open(CPUContext& ctx, const std::string& path, uint32_t mode);
    int32_t close(CPUContext& ctx, uint32_t fd);
    int32_t read(CPUContext& ctx, uint32_t fd, uint32_t buf, uint32_t len);
    int32_t write(CPUContext& ctx, uint32_t fd, uint32_t buf, uint32_t len);
    int32_t seek(CPUContext& ctx, uint32_t fd, int32_t offset, int32_t whence);
    int32_t ioctl(CPUContext& ctx, const IpcRequest& req);
    int32_t ioctlv(CPUContext& ctx, const IpcRequest& req);

private:
    IOSKernel() = default;

    std::vector<std::unique_ptr<IDevice>> m_devices;

    struct FdEntry {
        IDevice* device = nullptr;
        // Device-internal handle returned by IDevice::open; passed back to
        // the device on read/write/seek/close instead of the kernel fd.
        int32_t internal_fd = 0;
        bool in_use = false;
    };
    std::vector<FdEntry> m_fds;

    int32_t allocate_fd(IDevice* device, int32_t internal_fd);
    void free_fd(uint32_t fd);
    FdEntry* get_entry(uint32_t fd);
};

} // namespace nwii::runtime
