#pragma once
#include "runtime/platform/platform.h"

namespace nwii::runtime::platform {

class GCPlatform : public IPlatform {
public:
    void init() override;

    void ios_open(CPUContext& ctx) override;
    void ios_open_async(CPUContext& ctx) override;
    void ios_close(CPUContext& ctx) override;
    void ios_close_async(CPUContext& ctx) override;
    void ios_read(CPUContext& ctx) override;
    void ios_read_async(CPUContext& ctx) override;
    void ios_write(CPUContext& ctx) override;
    void ios_write_async(CPUContext& ctx) override;
    void ios_ioctl(CPUContext& ctx) override;
    void ios_ioctl_async(CPUContext& ctx) override;
    void ios_ioctlv(CPUContext& ctx) override;
    void ios_ioctlv_async(CPUContext& ctx) override;
};

} 
