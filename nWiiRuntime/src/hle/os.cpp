#include "runtime/cpu_context.h"
#include <iostream>
#include <string>

using namespace nwii::runtime;

// Read a null-terminated string from the guest MMU memory
static std::string read_guest_string(CPUContext& ctx, uint32_t addr) {
    std::string str;
    while (true) {
        uint8_t c = ctx.mmu.read8(addr++);
        if (c == 0) break;
        str += (char)c;
    }
    return str;
}

// OSReport  is the standard Nintendo SDK print function.
// Signature: void OSReport(const char* msg, ...);
// The format string address is passed in r3 (gpr[3]).
void OSReport(CPUContext& ctx) {
    uint32_t format_addr = ctx.gpr[3];
    std::string format_str = read_guest_string(ctx, format_addr);
    
    // In a real HLE, we would parse the format string and varargs.
    // For now, we'll just print the format string raw.
    std::cout << "[OSReport] " << format_str;
}

// Basic stubs for other OS functions to prevent linker errors
void OSInit(CPUContext& ctx) {
    std::cout << "[OSInit] System initialized." << std::endl;
}

extern "C" {

static uint32_t vi_vblank_counter = 0;

uint16_t HW_Reg_Read16(uint32_t addr) {
    return (uint16_t)HW_Reg_Read32(addr);
}

uint32_t HW_Reg_Read32(uint32_t addr) {
    // VI (Video Interface) Registers
    if (addr == 0xCC00302C || addr == 0xCC003030) {
        // VBlank / Retrace counter. Toggling this breaks spinlocks.
        vi_vblank_counter++;
        return vi_vblank_counter;
    }
    
    // EXI (Expansion Interface) Registers
    if ((addr & 0xFFFFFF00) == 0xCC005000) {
        static uint32_t exi_seed = 1;
        exi_seed = exi_seed * 1664525 + 1013904223;
        return exi_seed;
    }
    
    // DI (DVD Interface) Registers
    if (addr == 0xCC006000) {
        // Status register. Return 0 for success / ready.
        return 0;
    }
    
    if (addr == 0xCC006004) {
        // Cover register. 1 = Closed.
        return 1;
    }

    if (addr == 0xCC006008) {
        // Command register.
        return 0;
    }

    // Default to 0 for unknown hardware registers
    return 0;
}

void HW_Reg_Write16(uint32_t addr, uint16_t val) {
    HW_Reg_Write32(addr, val);
}

void HW_Reg_Write32(uint32_t addr, uint32_t val) {
    // Ignore writes to hardware registers for now,
    // to prevent crashes, unless they are specific handled registers.
}

} // extern "C"

#include <chrono>

static uint64_t get_os_time() {
    auto now = std::chrono::high_resolution_clock::now();
    return std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
}

extern "C" {

// Interrupt Management
uint32_t OSDisableInterrupts(CPUContext& ctx) {
    uint32_t old_msr = ctx.msr;
    ctx.msr &= ~(1 << 15); // Clear EE (External Interrupt Enable) bit
    ctx.gpr[3] = old_msr;
    return old_msr;
}

uint32_t OSEnableInterrupts(CPUContext& ctx) {
    uint32_t old_msr = ctx.msr;
    ctx.msr |= (1 << 15); // Set EE bit
    ctx.gpr[3] = old_msr;
    return old_msr;
}

uint32_t OSRestoreInterrupts(CPUContext& ctx) {
    uint32_t prev_state = ctx.gpr[3];
    uint32_t old_msr = ctx.msr;
    if (prev_state & (1 << 15)) {
        ctx.msr |= (1 << 15);
    } else {
        ctx.msr &= ~(1 << 15);
    }
    ctx.gpr[3] = old_msr;
    return old_msr;
}

// Timer Management
void OSGetTime(CPUContext& ctx) {
    uint64_t t = get_os_time();
    // Return 64-bit time: r3 = upper 32 bits, r4 = lower 32 bits
    ctx.gpr[3] = (uint32_t)(t >> 32);
    ctx.gpr[4] = (uint32_t)(t & 0xFFFFFFFF);
}

void OSTicksToMilliseconds(CPUContext& ctx) {
    uint64_t ticks = ((uint64_t)ctx.gpr[3] << 32) | ctx.gpr[4];
    // In Gekko, timebase ticks at 1/4 of bus speed (Bus = 162 MHz -> TB = 40.5 MHz)
    // 40,500 ticks per millisecond
    uint64_t ms = ticks / 40500;
    ctx.gpr[3] = (uint32_t)(ms >> 32);
    ctx.gpr[4] = (uint32_t)(ms & 0xFFFFFFFF);
}

} // extern "C"

