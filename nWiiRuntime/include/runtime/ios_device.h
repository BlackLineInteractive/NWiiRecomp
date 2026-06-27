#pragma once
#include "runtime/cpu_context.h"
#include <string>
#include <vector>

namespace nwii::runtime {

// Return codes for IOS operations
constexpr int32_t IPC_OK = 0;
constexpr int32_t IPC_EINVAL = -4;
constexpr int32_t IPC_ENOENT = -106;

struct IoctlvVector {
    uint32_t addr;
    uint32_t len;
};

struct IpcRequest {
    uint32_t cmd;      // e.g. 1=OPEN, 2=CLOSE, 3=READ, 4=WRITE, 6=IOCTL, 7=IOCTLV
    uint32_t fd;
    // For IOCTL
    uint32_t ioctl_cmd;
    uint32_t in_buf;
    uint32_t in_size;
    uint32_t out_buf;
    uint32_t out_size;
    
    // For IOCTLV
    uint32_t arg_cnt_in;
    uint32_t arg_cnt_out;
    std::vector<IoctlvVector> ioctlv_vecs;
    
    // For OPEN
    std::string path;
    uint32_t mode;
};

class IDevice {
public:
    virtual ~IDevice() = default;

    virtual const char* get_name() const = 0;

    virtual int32_t open(CPUContext& ctx, const std::string& path, uint32_t mode) { return IPC_OK; }
    virtual int32_t close(CPUContext& ctx, uint32_t fd) { return IPC_OK; }
    virtual int32_t read(CPUContext& ctx, uint32_t fd, uint32_t buf, uint32_t len) { return IPC_OK; }
    virtual int32_t write(CPUContext& ctx, uint32_t fd, uint32_t buf, uint32_t len) { return IPC_OK; }
    virtual int32_t seek(CPUContext& ctx, uint32_t fd, int32_t offset, int32_t whence) { return IPC_OK; }
    virtual int32_t ioctl(CPUContext& ctx, const IpcRequest& req) { return IPC_OK; }
    virtual int32_t ioctlv(CPUContext& ctx, const IpcRequest& req) { return IPC_OK; }
    
    // Return true if the device handles the path.
    virtual bool matches_path(const std::string& path) const {
        return path == get_name();
    }
};

} // namespace nwii::runtime
