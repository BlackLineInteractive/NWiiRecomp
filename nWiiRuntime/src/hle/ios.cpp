#include "runtime/cpu_context.h"
#include "runtime/config.h"
#include <iostream>
#include <string>

using namespace nwii::runtime;

// IOS IPC Stubs for Wii compatibility (not strictly needed for GameCube)

void IOS_Open(CPUContext& ctx) {
    if (Config::get().platform == Platform::GameCube) {
        ctx.gpr[3] = -1; // Return error or unsupported
        return;
    }
    uint32_t path_ptr = ctx.gpr[3];
    uint32_t mode = ctx.gpr[4];
    
    std::string path;
    uint32_t addr = path_ptr;
    char c;
    while ((c = ctx.mmu.read8(addr++)) != '\0' && path.length() < 256) {
        path += c;
    }
    
    std::cout << "[HLE IOS] IOS_Open: path=" << path << ", r4=" << ctx.gpr[4] << ", r5=" << ctx.gpr[5] << ", r6=" << ctx.gpr[6] << ", r7=" << ctx.gpr[7] << std::endl;
    
    ctx.gpr[3] = 1;
    ctx.pc = ctx.lr;
}

void IOS_OpenAsync(CPUContext& ctx) {
    if (Config::get().platform == Platform::GameCube) {
        ctx.gpr[3] = -1; // Return error or unsupported
        return;
    }
    uint32_t path_ptr = ctx.gpr[3];
    
    std::string path;
    uint32_t addr = path_ptr;
    char c;
    while ((c = ctx.mmu.read8(addr++)) != '\0' && path.length() < 256) {
        path += c;
    }
    
    std::cout << "[HLE IOS] IOS_OpenAsync: path=" << path << ", r4=" << ctx.gpr[4] << ", r5=" << ctx.gpr[5] << ", r6=" << ctx.gpr[6] << ", r7=" << ctx.gpr[7] << std::endl;
    
    // Assume r5 is callback, r6 is userdata for now if r5 looks like a pointer
    uint32_t callback = ctx.gpr[5];
    uint32_t userdata = ctx.gpr[6];
    
    if (callback >= 0x80000000 && callback < 0x82000000) {
        std::cout << "[HLE IOS] IOS_OpenAsync: using callback " << std::hex << callback << std::dec << "\n";
        ctx.gpr[3] = 0; // Success code for callback
        ctx.gpr[4] = userdata;
        ctx.pc = callback;
    } else {
        ctx.gpr[3] = 1; // Return fake fd synchronously
        ctx.pc = ctx.lr;
    }
}

void IOS_Close(CPUContext& ctx) {
    if (Config::get().platform == Platform::GameCube) {
        ctx.gpr[3] = -1;
        return;
    }
    uint32_t fd = ctx.gpr[3];
    std::cout << "[HLE IOS] IOS_Close: fd=" << fd << std::endl;
    ctx.gpr[3] = 0; // Success
    ctx.pc = ctx.lr;
}

void IOS_Read(CPUContext& ctx) {
    if (Config::get().platform == Platform::GameCube) {
        ctx.gpr[3] = -1;
        return;
    }
    uint32_t fd = ctx.gpr[3];
    uint32_t buf_ptr = ctx.gpr[4];
    uint32_t length = ctx.gpr[5];
    
    std::cout << "[HLE IOS] IOS_Read: fd=" << fd << ", buf=0x" << std::hex << buf_ptr << ", len=" << std::dec << length << std::endl;
    ctx.gpr[3] = length; // Pretend we read the requested length
    ctx.pc = ctx.lr;
}

void IOS_Write(CPUContext& ctx) {
    if (Config::get().platform == Platform::GameCube) {
        ctx.gpr[3] = -1;
        return;
    }
    uint32_t fd = ctx.gpr[3];
    uint32_t buf_ptr = ctx.gpr[4];
    uint32_t length = ctx.gpr[5];
    
    std::cout << "[HLE IOS] IOS_Write: fd=" << fd << ", buf=0x" << std::hex << buf_ptr << ", len=" << std::dec << length << std::endl;
    ctx.gpr[3] = length; // Pretend we wrote the requested length
    ctx.pc = ctx.lr;
}

void IOS_Ioctl(CPUContext& ctx) {
    if (Config::get().platform == Platform::GameCube) {
        ctx.gpr[3] = -1;
        return;
    }
    uint32_t fd = ctx.gpr[3];
    uint32_t ioctl_cmd = ctx.gpr[4];
    
    std::cout << "[HLE IOS] IOS_Ioctl: r3=" << fd << ", r4=" << ioctl_cmd << ", r5=" << ctx.gpr[5] << ", r6=" << ctx.gpr[6] << ", r7=" << ctx.gpr[7] << ", r8=" << ctx.gpr[8] << std::endl;
    ctx.gpr[3] = 0; // Success
    ctx.pc = ctx.lr;
}

void IOS_Ioctlv(CPUContext& ctx) {
    if (Config::get().platform == Platform::GameCube) {
        ctx.gpr[3] = -1;
        return;
    }
    uint32_t fd = ctx.gpr[3];
    uint32_t ioctl_cmd = ctx.gpr[4];
    
    std::cout << "[HLE IOS] IOS_Ioctlv: r3=" << fd << ", r4=" << ioctl_cmd << ", r5=" << ctx.gpr[5] << ", r6=" << ctx.gpr[6] << ", r7=" << ctx.gpr[7] << ", r8=" << ctx.gpr[8] << std::endl;
    ctx.gpr[3] = 0; // Success
    ctx.pc = ctx.lr;
}

void IOS_IoctlAsync(CPUContext& ctx) {
    if (Config::get().platform == Platform::GameCube) {
        ctx.gpr[3] = -1;
        return;
    }
    
    std::cout << "[HLE IOS] IOS_IoctlAsync: r3=" << ctx.gpr[3] << " r4=" << ctx.gpr[4] << " r5=" << ctx.gpr[5] << " r6=" << ctx.gpr[6] << " r7=" << ctx.gpr[7] << " r8=" << ctx.gpr[8] << " r9=" << std::hex << ctx.gpr[9] << " r10=" << ctx.gpr[10] << std::dec << "\n";

    uint32_t callback = ctx.gpr[9];
    uint32_t userdata = ctx.gpr[10];

    // If it's not a pointer, maybe the signature is different? Try r6/r7?
    if (callback < 0x80000000 || callback >= 0x82000000) {
        if (ctx.gpr[5] >= 0x80000000 && ctx.gpr[5] < 0x82000000) {
            callback = ctx.gpr[5];
            userdata = ctx.gpr[6];
        }
    }

    if (callback >= 0x80000000 && callback < 0x82000000) {
        ctx.gpr[3] = 0; 
        ctx.gpr[4] = userdata;
        ctx.pc = callback; 
    } else {
        ctx.gpr[3] = 0;
        ctx.pc = ctx.lr;
    }
}

void IOS_IoctlvAsync(CPUContext& ctx) {
    if (Config::get().platform == Platform::GameCube) {
        ctx.gpr[3] = -1;
        return;
    }
    
    std::cout << "[HLE IOS] IOS_IoctlvAsync: r3=" << ctx.gpr[3] << " r4=" << ctx.gpr[4] << " r5=" << ctx.gpr[5] << " r6=" << ctx.gpr[6] << " r7=" << ctx.gpr[7] << " r8=" << std::hex << ctx.gpr[8] << " r9=" << ctx.gpr[9] << std::dec << "\n";

    uint32_t callback = ctx.gpr[8];
    uint32_t userdata = ctx.gpr[9];

    if (callback < 0x80000000 || callback >= 0x82000000) {
        if (ctx.gpr[5] >= 0x80000000 && ctx.gpr[5] < 0x82000000) {
            callback = ctx.gpr[5];
            userdata = ctx.gpr[6];
        }
    }

    if (callback >= 0x80000000 && callback < 0x82000000) {
        ctx.gpr[3] = 0; // IPC_OK
        ctx.gpr[4] = userdata;
        ctx.pc = callback;
    } else {
        ctx.gpr[3] = 0;
        ctx.pc = ctx.lr;
    }
}

void IOS_Seek(CPUContext& ctx) {
    if (Config::get().platform == Platform::GameCube) { ctx.gpr[3] = -1; return; }
    std::cout << "[HLE IOS] IOS_Seek: fd=" << ctx.gpr[3] << ", where=" << ctx.gpr[4] << ", whence=" << ctx.gpr[5] << "\n";
    ctx.gpr[3] = 0;
    ctx.pc = ctx.lr;
}

void IOS_CloseAsync(CPUContext& ctx) {
    if (Config::get().platform == Platform::GameCube) { ctx.gpr[3] = -1; return; }
    std::cout << "[HLE IOS] IOS_CloseAsync: fd=" << ctx.gpr[3] << "\n";
    uint32_t callback = ctx.gpr[4];
    uint32_t userdata = ctx.gpr[5];
    if (callback >= 0x80000000 && callback < 0x82000000) {
        ctx.gpr[3] = 0; ctx.gpr[4] = userdata; ctx.pc = callback;
    } else {
        ctx.gpr[3] = 0; ctx.pc = ctx.lr;
    }
}

void IOS_ReadAsync(CPUContext& ctx) {
    if (Config::get().platform == Platform::GameCube) { ctx.gpr[3] = -1; return; }
    std::cout << "[HLE IOS] IOS_ReadAsync: fd=" << ctx.gpr[3] << ", buf=" << ctx.gpr[4] << ", len=" << ctx.gpr[5] << "\n";
    uint32_t callback = ctx.gpr[6];
    uint32_t userdata = ctx.gpr[7];
    if (callback >= 0x80000000 && callback < 0x82000000) {
        ctx.gpr[3] = ctx.gpr[5]; ctx.gpr[4] = userdata; ctx.pc = callback;
    } else {
        ctx.gpr[3] = ctx.gpr[5]; ctx.pc = ctx.lr;
    }
}

void IOS_WriteAsync(CPUContext& ctx) {
    if (Config::get().platform == Platform::GameCube) { ctx.gpr[3] = -1; return; }
    std::cout << "[HLE IOS] IOS_WriteAsync: fd=" << ctx.gpr[3] << ", buf=" << ctx.gpr[4] << ", len=" << ctx.gpr[5] << "\n";
    uint32_t callback = ctx.gpr[6];
    uint32_t userdata = ctx.gpr[7];
    if (callback >= 0x80000000 && callback < 0x82000000) {
        ctx.gpr[3] = ctx.gpr[5]; ctx.gpr[4] = userdata; ctx.pc = callback;
    } else {
        ctx.gpr[3] = ctx.gpr[5]; ctx.pc = ctx.lr;
    }
}

void IOS_SeekAsync(CPUContext& ctx) {
    if (Config::get().platform == Platform::GameCube) { ctx.gpr[3] = -1; return; }
    std::cout << "[HLE IOS] IOS_SeekAsync: fd=" << ctx.gpr[3] << ", where=" << ctx.gpr[4] << ", whence=" << ctx.gpr[5] << "\n";
    uint32_t callback = ctx.gpr[6];
    uint32_t userdata = ctx.gpr[7];
    if (callback >= 0x80000000 && callback < 0x82000000) {
        ctx.gpr[3] = 0; ctx.gpr[4] = userdata; ctx.pc = callback;
    } else {
        ctx.gpr[3] = 0; ctx.pc = ctx.lr;
    }
}

namespace nwii {
namespace runtime {

void syscall_handler(CPUContext& ctx) {
    uint32_t syscall_id = ctx.gpr[0];
    switch (syscall_id) {
        case 0x02: IOS_Open(ctx); break;
        case 0x03: IOS_Close(ctx); break;
        case 0x04: IOS_Read(ctx); break;
        case 0x05: IOS_Write(ctx); break;
        case 0x06: IOS_Seek(ctx); break;
        case 0x07: IOS_Ioctl(ctx); break;
        case 0x08: IOS_Ioctlv(ctx); break;
        case 0x09: IOS_OpenAsync(ctx); break;
        case 0x0A: IOS_CloseAsync(ctx); break;
        case 0x0B: IOS_ReadAsync(ctx); break;
        case 0x0C: IOS_WriteAsync(ctx); break;
        case 0x0D: IOS_SeekAsync(ctx); break;
        case 0x0E: IOS_IoctlAsync(ctx); break;
        case 0x0F: IOS_IoctlvAsync(ctx); break;
        case 0x40000:
        case 0x80212d04:
            // Ignore OSYieldThread / OSDisableInterrupts syscalls
            break;
        default:
            std::cout << "[HLE IOS] Unknown syscall 0x" << std::hex << syscall_id << std::dec << "\n";
            ctx.gpr[3] = -1;
            break;
    }
}

}
}
