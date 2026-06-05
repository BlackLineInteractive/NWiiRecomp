#include "runtime/cpu_context.h"
#include "runtime/config.h"
#include <iostream>
#include <string>

using namespace nwii::runtime;

extern "C" {

// IOS IPC Stubs for Wii compatibility (not strictly needed for GameCube)

void IOS_Open(CPUContext& ctx) {
    if (Config::get().platform == Platform::GameCube) {
        ctx.gpr[3] = -1; // Return error or unsupported
        return;
    }
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
    if (Config::get().platform == Platform::GameCube) {
        ctx.gpr[3] = -1;
        return;
    }
    uint32_t fd = ctx.gpr[3];
    std::cout << "[HLE IOS] IOS_Close: fd=" << fd << std::endl;
    ctx.gpr[3] = 0; // Success
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
}

void IOS_Ioctl(CPUContext& ctx) {
    if (Config::get().platform == Platform::GameCube) {
        ctx.gpr[3] = -1;
        return;
    }
    uint32_t fd = ctx.gpr[3];
    uint32_t ioctl_cmd = ctx.gpr[4];
    
    std::cout << "[HLE IOS] IOS_Ioctl: fd=" << fd << ", cmd=" << ioctl_cmd << std::endl;
    ctx.gpr[3] = 0; // Success
}

void IOS_Ioctlv(CPUContext& ctx) {
    if (Config::get().platform == Platform::GameCube) {
        ctx.gpr[3] = -1;
        return;
    }
    uint32_t fd = ctx.gpr[3];
    uint32_t ioctl_cmd = ctx.gpr[4];
    
    std::cout << "[HLE IOS] IOS_Ioctlv: fd=" << fd << ", cmd=" << ioctl_cmd << std::endl;
    ctx.gpr[3] = 0; // Success
}

// --- НОВІ АСИНХРОННІ ВИКЛИКИ (Вирішують зависання на 0x8020def4) ---

void IOS_IoctlAsync(CPUContext& ctx) {
    if (Config::get().platform == Platform::GameCube) {
        ctx.gpr[3] = -1;
        return;
    }
    
    // Сигнатура: fd (r3), cmd (r4), in_buf (r5), in_len (r6), out_buf (r7), out_len (r8), callback (r9), userdata (r10)
    uint32_t callback = ctx.gpr[9];
    uint32_t userdata = ctx.gpr[10];

    std::cout << "[HLE IOS] IOS_IoctlAsync: cmd=" << ctx.gpr[4] << " cb=" << std::hex << callback << std::dec << "\n";

    if (callback != 0) {
        // Готуємо аргументи для колбека: r3 = 0 (Успіх/IPC_OK), r4 = userdata
        ctx.gpr[3] = 0; 
        ctx.gpr[4] = userdata;
        // Tail-call: Переходимо в колбек
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
    
    // Сигнатура: fd (r3), cmd (r4), in_cnt (r5), out_cnt (r6), vec (r7), callback (r8), userdata (r9)
    uint32_t callback = ctx.gpr[8];
    uint32_t userdata = ctx.gpr[9];

    std::cout << "[HLE IOS] IOS_IoctlvAsync: cmd=" << ctx.gpr[4] << " cb=" << std::hex << callback << std::dec << "\n";

    if (callback != 0) {
        ctx.gpr[3] = 0; // IPC_OK
        ctx.gpr[4] = userdata;
        ctx.pc = callback;
    } else {
        ctx.gpr[3] = 0;
        ctx.pc = ctx.lr;
    }
}

} // extern "C"
