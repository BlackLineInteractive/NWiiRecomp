#include "runtime/cpu_context.h"
#include <iostream>
#include <string>

using namespace nwii::runtime;

extern "C" {

// IOS IPC Stubs for Wii compatibility (not strictly needed for GameCube)

void IOS_Open(CPUContext& ctx) {
    // r3 = filepath pointer
    // r4 = mode
    uint32_t path_ptr = ctx.gpr[3];
    uint32_t mode = ctx.gpr[4];
    
    // Read the null-terminated string from memory
    std::string path;
    uint32_t addr = path_ptr;
    char c;
    while ((c = ctx.mmu.read8(addr++)) != '\0' && path.length() < 256) {
        path += c;
    }
    
    std::cout << "[HLE IOS] IOS_Open: path=" << path << ", mode=" << mode << std::endl;
    
    // Return a dummy file descriptor (positive integer)
    ctx.gpr[3] = 1;
}

void IOS_Close(CPUContext& ctx) {
    uint32_t fd = ctx.gpr[3];
    std::cout << "[HLE IOS] IOS_Close: fd=" << fd << std::endl;
    ctx.gpr[3] = 0; // Success
}

void IOS_Read(CPUContext& ctx) {
    uint32_t fd = ctx.gpr[3];
    uint32_t buf_ptr = ctx.gpr[4];
    uint32_t length = ctx.gpr[5];
    
    std::cout << "[HLE IOS] IOS_Read: fd=" << fd << ", buf=0x" << std::hex << buf_ptr << ", len=" << std::dec << length << std::endl;
    ctx.gpr[3] = length; // Pretend we read the requested length
}

void IOS_Write(CPUContext& ctx) {
    uint32_t fd = ctx.gpr[3];
    uint32_t buf_ptr = ctx.gpr[4];
    uint32_t length = ctx.gpr[5];
    
    std::cout << "[HLE IOS] IOS_Write: fd=" << fd << ", buf=0x" << std::hex << buf_ptr << ", len=" << std::dec << length << std::endl;
    ctx.gpr[3] = length; // Pretend we wrote the requested length
}

void IOS_Ioctl(CPUContext& ctx) {
    uint32_t fd = ctx.gpr[3];
    uint32_t ioctl_cmd = ctx.gpr[4];
    
    std::cout << "[HLE IOS] IOS_Ioctl: fd=" << fd << ", cmd=" << ioctl_cmd << std::endl;
    ctx.gpr[3] = 0; // Success
}

void IOS_Ioctlv(CPUContext& ctx) {
    uint32_t fd = ctx.gpr[3];
    uint32_t ioctl_cmd = ctx.gpr[4];
    
    std::cout << "[HLE IOS] IOS_Ioctlv: fd=" << fd << ", cmd=" << ioctl_cmd << std::endl;
    ctx.gpr[3] = 0; // Success
}

} // extern "C"
