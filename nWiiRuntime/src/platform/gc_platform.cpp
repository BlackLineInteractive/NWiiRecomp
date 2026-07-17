#include "runtime/platform/gc_platform.h"

namespace nwii::runtime::platform {

void GCPlatform::ios_open(CPUContext& ctx) {
    ctx.gpr[3] = -1;
    ctx.pc = ctx.lr;
}

void GCPlatform::ios_open_async(CPUContext& ctx) {
    ctx.gpr[3] = -1;
    ctx.pc = ctx.lr;
}

void GCPlatform::ios_close(CPUContext& ctx) {
    ctx.gpr[3] = -1;
    ctx.pc = ctx.lr;
}

void GCPlatform::ios_close_async(CPUContext& ctx) {
    ctx.gpr[3] = -1;
    ctx.pc = ctx.lr;
}

void GCPlatform::ios_read(CPUContext& ctx) {
    ctx.gpr[3] = -1;
    ctx.pc = ctx.lr;
}

void GCPlatform::ios_read_async(CPUContext& ctx) {
    ctx.gpr[3] = -1;
    ctx.pc = ctx.lr;
}

void GCPlatform::ios_write(CPUContext& ctx) {
    ctx.gpr[3] = -1;
    ctx.pc = ctx.lr;
}

void GCPlatform::ios_write_async(CPUContext& ctx) {
    ctx.gpr[3] = -1;
    ctx.pc = ctx.lr;
}

void GCPlatform::ios_ioctl(CPUContext& ctx) {
    ctx.gpr[3] = -1;
    ctx.pc = ctx.lr;
}

void GCPlatform::ios_ioctl_async(CPUContext& ctx) {
    ctx.gpr[3] = -1;
    ctx.pc = ctx.lr;
}

void GCPlatform::ios_ioctlv(CPUContext& ctx) {
    ctx.gpr[3] = -1;
    ctx.pc = ctx.lr;
}

void GCPlatform::ios_ioctlv_async(CPUContext& ctx) {
    ctx.gpr[3] = -1;
    ctx.pc = ctx.lr;
}

} 
