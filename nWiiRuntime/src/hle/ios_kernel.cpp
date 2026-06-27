#include "runtime/ios_kernel.h"
#include <iostream>

namespace nwii::runtime {

void IOSKernel::init() {
    m_devices.clear();
    m_fds.clear();
    // Reserve fd 0 as invalid
    m_fds.push_back({nullptr, true});
}

void IOSKernel::register_device(std::unique_ptr<IDevice> device) {
    std::cout << "[IOSKernel] Registered device: " << device->get_name() << std::endl;
    m_devices.push_back(std::move(device));
}

int32_t IOSKernel::allocate_fd(IDevice* device) {
    for (size_t i = 1; i < m_fds.size(); ++i) {
        if (!m_fds[i].in_use) {
            m_fds[i] = {device, true};
            return (int32_t)i;
        }
    }
    m_fds.push_back({device, true});
    return (int32_t)(m_fds.size() - 1);
}

void IOSKernel::free_fd(uint32_t fd) {
    if (fd > 0 && fd < m_fds.size()) {
        m_fds[fd].in_use = false;
        m_fds[fd].device = nullptr;
    }
}

IDevice* IOSKernel::get_device_by_fd(uint32_t fd) {
    if (fd > 0 && fd < m_fds.size() && m_fds[fd].in_use) {
        return m_fds[fd].device;
    }
    return nullptr;
}

int32_t IOSKernel::open(CPUContext& ctx, const std::string& path, uint32_t mode) {
    for (const auto& dev : m_devices) {
        if (dev->matches_path(path)) {
            int32_t res = dev->open(ctx, path, mode);
            if (res >= 0) {
                return allocate_fd(dev.get());
            }
            return res;
        }
    }
    std::cout << "[IOSKernel] open: device not found for path: " << path << std::endl;
    return IPC_ENOENT;
}

int32_t IOSKernel::close(CPUContext& ctx, uint32_t fd) {
    IDevice* dev = get_device_by_fd(fd);
    if (!dev) return IPC_EINVAL;
    
    int32_t res = dev->close(ctx, fd);
    free_fd(fd);
    return res;
}

int32_t IOSKernel::read(CPUContext& ctx, uint32_t fd, uint32_t buf, uint32_t len) {
    IDevice* dev = get_device_by_fd(fd);
    if (!dev) return IPC_EINVAL;
    return dev->read(ctx, fd, buf, len);
}

int32_t IOSKernel::write(CPUContext& ctx, uint32_t fd, uint32_t buf, uint32_t len) {
    IDevice* dev = get_device_by_fd(fd);
    if (!dev) return IPC_EINVAL;
    return dev->write(ctx, fd, buf, len);
}

int32_t IOSKernel::seek(CPUContext& ctx, uint32_t fd, int32_t offset, int32_t whence) {
    IDevice* dev = get_device_by_fd(fd);
    if (!dev) return IPC_EINVAL;
    return dev->seek(ctx, fd, offset, whence);
}

int32_t IOSKernel::ioctl(CPUContext& ctx, const IpcRequest& req) {
    IDevice* dev = get_device_by_fd(req.fd);
    if (!dev) return IPC_EINVAL;
    return dev->ioctl(ctx, req);
}

int32_t IOSKernel::ioctlv(CPUContext& ctx, const IpcRequest& req) {
    IDevice* dev = get_device_by_fd(req.fd);
    if (!dev) return IPC_EINVAL;
    return dev->ioctlv(ctx, req);
}

} // namespace nwii::runtime
