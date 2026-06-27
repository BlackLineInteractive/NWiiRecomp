#pragma once
#include "runtime/cpu_context.h"
#include <memory>

namespace nwii::runtime::platform {

class IPlatform {
public:
    virtual ~IPlatform() = default;

    virtual void init() = 0;

    // IOS HLE Interface
    virtual void ios_open(CPUContext& ctx) = 0;
    virtual void ios_open_async(CPUContext& ctx) = 0;
    virtual void ios_close(CPUContext& ctx) = 0;
    virtual void ios_close_async(CPUContext& ctx) = 0;
    virtual void ios_read(CPUContext& ctx) = 0;
    virtual void ios_read_async(CPUContext& ctx) = 0;
    virtual void ios_write(CPUContext& ctx) = 0;
    virtual void ios_write_async(CPUContext& ctx) = 0;
    virtual void ios_ioctl(CPUContext& ctx) = 0;
    virtual void ios_ioctl_async(CPUContext& ctx) = 0;
    virtual void ios_ioctlv(CPUContext& ctx) = 0;
    virtual void ios_ioctlv_async(CPUContext& ctx) = 0;

    static std::unique_ptr<IPlatform> create();
    static IPlatform& get();
};

} // namespace nwii::runtime::platform
