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
    
    // Quick hack to replace the first %d or %x with the value of r4
    size_t pos = format_str.find("%d");
    if (pos != std::string::npos) {
        format_str.replace(pos, 2, std::to_string((int32_t)ctx.gpr[4]));
    } else {
        pos = format_str.find("%x");
        if (pos != std::string::npos) {
            char hex_buf[32];
            snprintf(hex_buf, sizeof(hex_buf), "%x", ctx.gpr[4]);
            format_str.replace(pos, 2, hex_buf);
        }
    }
    
    std::cout << "[OSReport] " << format_str;
}

// Basic stubs for other OS functions to prevent linker errors
void OSInit(CPUContext& ctx) {
    std::cout << "[OSInit] System initialized." << std::endl;
}

extern "C" {

static uint32_t vi_vblank_counter = 0;

static uint16_t dsp_mbox_cpu_hi = 0;
static uint16_t dsp_mbox_cpu_lo = 0;
static uint16_t dsp_mbox_dsp_hi = 0;
static uint16_t dsp_mbox_dsp_lo = 0;

uint16_t HW_Reg_Read16(uint32_t addr) {
    return (uint16_t)HW_Reg_Read32(addr);
}

uint32_t HW_Reg_Read32(uint32_t addr) {
    addr = (addr & 0x00FFFFFF) | 0xCC000000;
    // PI (Processor Interface) Registers - Interrupts
    if (addr >= 0xCC003000 && addr <= 0xCC0030FF) {
        if (addr == 0xCC003000 || addr == 0xCC003004) {
            // PI Interrupt Cause / Mask. 
            // Return 0 so game doesn't think it has unhandled hardware interrupts.
            return 0; 
        }
        return 0;
    }
    
    // VI (Video Interface) Registers
    if (addr >= 0xCC002000 && addr <= 0xCC0020FF) {
        if (addr == 0xCC00202C || addr == 0xCC002030) {
            // VBlank / Retrace counter. We increment it so the game thinks frames are passing.
            static uint32_t vi_vblank_counter = 0;
            return vi_vblank_counter++;
        }
        return 0;
    }
    
    // AI/DSP (Audio Interface / Digital Signal Processor) Registers
    if (addr >= 0xCC005000 && addr <= 0xCC0050FF) {
        if (addr == 0xCC005000 || addr == 0xCC005002) return dsp_mbox_cpu_hi;
        if (addr == 0xCC005004) return dsp_mbox_dsp_hi;
        if (addr == 0xCC005006) {
            uint16_t val = dsp_mbox_dsp_lo;
            dsp_mbox_dsp_hi &= ~0x8000; // Reading Mailbox Lo acknowledges and clears the message bit
            return val;
        }
        if (addr == 0xCC005008 || addr == 0xCC00500A) return 0x0020; // Always return 0x20 (Init)
        return 0;
    }
    
    // EXI (Expansion Interface) Registers
    if (addr >= 0xCC006800 && addr <= 0xCC0068FF) {
        // EXI Control Registers (Channel 0, 1, 2)
        if (addr == 0xCC00680C || addr == 0xCC006820 || addr == 0xCC006834) {
            // Bit 0 is 'START'. Returning 0 indicates transfer is complete.
            // This prevents EXI hardware polling spinlocks.
            return 0;
        }
        return 0;
    }
    
    // DI (DVD Interface) Registers
    if (addr >= 0xCC006000 && addr <= 0xCC0060FF) {
        if (addr == 0xCC006000) return 0; // Status: Ready / No Error
        if (addr == 0xCC006004) return 1; // Cover Status: 1 = Closed
        if (addr == 0xCC006008) return 0; // Command Status
        return 0;
    }

    // Default to 0 for unknown hardware registers
    return 0;
}

void HW_Reg_Write16(uint32_t addr, uint16_t val) {
    HW_Reg_Write32(addr, val);
}

void HW_Reg_Write32(uint32_t addr, uint32_t val) {
    addr = (addr & 0x00FFFFFF) | 0xCC000000;
    
    if (addr >= 0xCC005000 && addr <= 0xCC0050FF) {
        if (addr == 0xCC005000 || addr == 0xCC005002) {
            dsp_mbox_cpu_hi = (val & 0x7FFF) | 0x8000; // Bit 15 set when CPU writes
            
            // FAKE DSP SYNCHRONOUS PROCESSING
            // The DSP consumes the message instantly:
            dsp_mbox_cpu_hi &= ~0x8000; 
            
            // The DSP replies with a dummy message immediately:
            dsp_mbox_dsp_hi = 0x8000 | 0; // Bit 15 set: DSP has replied
        }
        else if (addr == 0xCC005004 || addr == 0xCC005006) {
            // CPU acknowledging DSP message by writing 1 to bit 15
            if (val & 0x8000) {
                dsp_mbox_dsp_hi &= ~0x8000; // Clear the bit
            }
        }
        // Ignores writes to 0xCC005008 and 0xCC00500A
        return;
    }
    
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

