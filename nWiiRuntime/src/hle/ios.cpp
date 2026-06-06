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
    
    // This hook is actually iosQueueMessage(queue_id, IPCMessage*)
    uint32_t queue_id = ctx.gpr[3];
    uint32_t ipc_ptr = ctx.gpr[4];

    if (ipc_ptr >= 0x80000000) {
        uint32_t cmd = ctx.mmu.read32(ipc_ptr + 0x00);
        int32_t fd = ctx.mmu.read32(ipc_ptr + 0x08);
        
        if (cmd == 1) { // IOS_Open
            uint32_t arg0 = ctx.mmu.read32(ipc_ptr + 0x0C);
            uint32_t arg1 = ctx.mmu.read32(ipc_ptr + 0x10);
            uint32_t arg2 = ctx.mmu.read32(ipc_ptr + 0x14);
            uint32_t arg3 = ctx.mmu.read32(ipc_ptr + 0x18);
            uint32_t arg4 = ctx.mmu.read32(ipc_ptr + 0x1C);
            
            // Translate physical address to virtual cached address
            uint32_t path_ptr = arg0;
            if (path_ptr < 0x80000000) {
                if (path_ptr < 0x01800000 || (path_ptr >= 0x10000000 && path_ptr < 0x14000000)) {
                    path_ptr |= 0x80000000;
                }
            }

            std::string path;
            if (path_ptr >= 0x80000000 && path_ptr < 0x94000000) {
                uint32_t p = path_ptr;
                for(int i=0; i<64; ++i) {
                    char c = ctx.mmu.read8(p++);
                    if (c == '\0') break;
                    if (c >= 32 && c <= 126) path += c;
                }
            }
            
            std::cout << "[HLE IOS] iosQueueMessage OPEN: path='" << path << "' (raw arg0=" << std::hex << arg0 << ")\n" << std::dec;
            
            if (path == "/dev/di") {
                ctx.mmu.write32(ipc_ptr + 0x04, 2); // result = fd
            } else {
                ctx.mmu.write32(ipc_ptr + 0x04, 3); // some other fd
            }
        } else if (cmd == 2) { // IOS_Close
            std::cout << "[HLE IOS] iosQueueMessage CLOSE: fd=" << fd << "\n";
            ctx.mmu.write32(ipc_ptr + 0x04, 0); // result = 0 (Success)
        } else if (cmd == 6) { // IOS_Ioctl
            uint32_t ioctl_cmd = ctx.mmu.read32(ipc_ptr + 0x0C);
            std::cout << "[HLE IOS] iosQueueMessage IOCTL: fd=" << (int)fd << " cmd=" << std::hex << ioctl_cmd << std::dec << "\n";
            ctx.mmu.write32(ipc_ptr + 0x04, 0); // result = 0 (Success)
        } else if (cmd == 7) { // IOS_Ioctlv
            uint32_t ioctl_cmd = ctx.mmu.read32(ipc_ptr + 0x0C);
            std::cout << "[HLE IOS] iosQueueMessage IOCTLV: fd=" << fd << " cmd=" << std::hex << ioctl_cmd << std::dec << "\n";
            ctx.mmu.write32(ipc_ptr + 0x04, 0); // result = 0 (Success)
        } else {
            std::cout << "[HLE IOS] iosQueueMessage UNKNOWN CMD: " << cmd << " fd=" << fd << "\n";
            ctx.mmu.write32(ipc_ptr + 0x04, 0); // result = 0 (Success)
        }

        // --- MANUALLY INVOKE CALLBACK ---
        uint32_t callback = ctx.mmu.read32(ipc_ptr + 0x20);
        uint32_t userdata = ctx.mmu.read32(ipc_ptr + 0x24);
        int32_t result = ctx.mmu.read32(ipc_ptr + 0x04);
        
        std::cout << "  -> callback=0x" << std::hex << callback << " userdata=0x" << userdata << " result=" << std::dec << result << "\n";
        
        if (callback != 0 && callback != 0xFFFFFFFF) {
            std::cout << "  -> Invoking callback directly!\n";
            uint32_t saved_lr = ctx.lr;
            uint32_t saved_pc = ctx.pc;
            uint32_t saved_r3 = ctx.gpr[3];
            uint32_t saved_r4 = ctx.gpr[4];
            
            ctx.gpr[3] = result;
            ctx.gpr[4] = userdata;
            ctx.lr = 0x802438B4; // dummy return
            ctx.pc = callback;
            
            // Wait, we can't just invoke it synchronously if it does a context switch.
            // But we will try!
        }
    }

    ctx.gpr[3] = 0; // return IPC_OK
    ctx.pc = ctx.lr;
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

void handle_syscall(CPUContext& ctx) {
    uint32_t syscall_id = ctx.gpr[0]; // Syscall number is always in r0

    if (syscall_id == 0x61) { 
        // 0x61 = IOS_Open. Game requests to open a device (e.g., "/dev/di")
        // We simply say "Ok, here is descriptor number 2"
        std::cout << "[HLE IOS] IOS_Open via sc\n";
        ctx.gpr[3] = 2; 

    } else if (syscall_id == 0x62) {
        // 0x62 = IOS_Close
        std::cout << "[HLE IOS] IOS_Close via sc\n";
        ctx.gpr[3] = 0; // Success

    } else if (syscall_id == 0x6B || syscall_id == 0x6C) {
        // 0x6B = IOS_IoctlAsync, 0x6C = IOS_IoctlvAsync (Async requests to DVD)
        // Here is the most important magic: we must invoke the game's Callback!
        std::cout << "[HLE IOS] IOS_Ioctl(v)Async via sc: " << std::hex << syscall_id << std::dec << "\n";
        uint32_t callback = (syscall_id == 0x6B) ? ctx.gpr[9] : ctx.gpr[8];
        uint32_t userdata = (syscall_id == 0x6B) ? ctx.gpr[10] : ctx.gpr[9];

        if (callback != 0) {
            // Prepare arguments for the callback
            ctx.gpr[3] = 0; // 0 = Success (IPC_OK)
            ctx.gpr[4] = userdata;
            
            // Perform a tail-call jump directly to the game's function!
            ctx.pc = callback; 
        } else {
            ctx.gpr[3] = 0;
            ctx.pc = ctx.lr;
        }

    } else {
        // For all other syscalls (thread switching, etc.) simulate success
        // std::cout << "[HLE IOS] Unknown sc: " << std::hex << syscall_id << std::dec << "\n";
        // Do NOT overwrite r3 because the `sc` could be a cache flush workaround that isn't a real syscall
        // and shouldn't corrupt the registers.
        // ctx.gpr[3] = 0; 
    }
}

}
}
