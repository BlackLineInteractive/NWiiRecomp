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

} // extern "C"


