#pragma once
#include <cstdint>
#include <array>
#include <vector>
#include <cstring>

namespace nwii {
namespace runtime {

struct ConditionField {
    bool lt; // Less Than
    bool gt; // Greater Than
    bool eq; // Equal
    bool so; // Summary Overflow
    
    ConditionField() : lt(false), gt(false), eq(false), so(false) {}
};

extern "C" {
    void GX_WGPIPE_Write8(uint8_t val);
    void GX_WGPIPE_Write16(uint16_t val);
    void GX_WGPIPE_Write32(uint32_t val);
    void GX_WGPIPE_WriteF32(float val);
    
    uint16_t HW_Reg_Read16(uint32_t addr);
    uint32_t HW_Reg_Read32(uint32_t addr);
}

struct MMU {
    std::vector<uint8_t> mem1;
    std::vector<uint8_t> mem2;
    
    MMU() {
        mem1.resize(24 * 1024 * 1024); // 24MB MEM1
        mem2.resize(64 * 1024 * 1024); // 64MB MEM2
    }
    
    uint8_t* get_ptr(uint32_t addr) {
        if ((addr & 0xF0000000) == 0x80000000 || (addr & 0xF0000000) == 0xC0000000) {
            uint32_t offset = addr & 0x01FFFFFF;
            if (offset < mem1.size()) return &mem1[offset];
        }
        if ((addr & 0xF0000000) == 0x90000000 || (addr & 0xF0000000) == 0xD0000000) {
            uint32_t offset = addr & 0x03FFFFFF;
            if (offset < mem2.size()) return &mem2[offset];
        }
        return nullptr;
    }

    uint8_t read8(uint32_t addr) { 
        uint8_t* ptr = get_ptr(addr);
        return ptr ? *ptr : 0; 
    }
    
    void write8(uint32_t addr, uint8_t value) { 
        if (addr == 0xCC008000) { GX_WGPIPE_Write8(value); return; }
        uint8_t* ptr = get_ptr(addr);
        if (ptr) *ptr = value; 
    }
    
    uint16_t read16(uint32_t addr) { 
        if ((addr & 0xFFFF0000) == 0xCC000000) return HW_Reg_Read16(addr);
        uint8_t* ptr = get_ptr(addr);
        if (!ptr) return 0;
        return (uint16_t)(ptr[0] << 8 | ptr[1]); 
    }
    
    void write16(uint32_t addr, uint16_t value) { 
        if (addr == 0xCC008000) { GX_WGPIPE_Write16(value); return; }
        uint8_t* ptr = get_ptr(addr);
        if (!ptr) return;
        ptr[0] = (value >> 8) & 0xFF;
        ptr[1] = value & 0xFF;
    }
    
    uint32_t read32(uint32_t addr) { 
        if ((addr & 0xFFFF0000) == 0xCC000000) return HW_Reg_Read32(addr);
        uint8_t* ptr = get_ptr(addr);
        if (!ptr) return 0;
        return ((uint32_t)ptr[0] << 24) | ((uint32_t)ptr[1] << 16) | ((uint32_t)ptr[2] << 8) | (uint32_t)ptr[3]; 
    }
    
    void write32(uint32_t addr, uint32_t value) { 
        if (addr == 0xCC008000) { GX_WGPIPE_Write32(value); return; }
        uint8_t* ptr = get_ptr(addr);
        if (!ptr) return;
        ptr[0] = (value >> 24) & 0xFF;
        ptr[1] = (value >> 16) & 0xFF;
        ptr[2] = (value >> 8) & 0xFF;
        ptr[3] = value & 0xFF;
    }
    
    float read_f32(uint32_t addr) { 
        uint32_t val = read32(addr);
        float f;
        std::memcpy(&f, &val, 4);
        return f;
    }
    
    void write_f32(uint32_t addr, float value) { 
        if (addr == 0xCC008000) { GX_WGPIPE_WriteF32(value); return; }
        uint32_t val;
        std::memcpy(&val, &value, 4);
        write32(addr, val);
    }
    
    uint64_t read64(uint32_t addr) {
        uint8_t* ptr = get_ptr(addr);
        if (!ptr) return 0;
        return ((uint64_t)ptr[0] << 56) | ((uint64_t)ptr[1] << 48) | ((uint64_t)ptr[2] << 40) | ((uint64_t)ptr[3] << 32) |
               ((uint64_t)ptr[4] << 24) | ((uint64_t)ptr[5] << 16) | ((uint64_t)ptr[6] << 8) | (uint64_t)ptr[7];
    }
    
    void write64(uint32_t addr, uint64_t value) {
        uint8_t* ptr = get_ptr(addr);
        if (!ptr) return;
        ptr[0] = (value >> 56) & 0xFF;
        ptr[1] = (value >> 48) & 0xFF;
        ptr[2] = (value >> 40) & 0xFF;
        ptr[3] = (value >> 32) & 0xFF;
        ptr[4] = (value >> 24) & 0xFF;
        ptr[5] = (value >> 16) & 0xFF;
        ptr[6] = (value >> 8) & 0xFF;
        ptr[7] = value & 0xFF;
    }

    double read_f64(uint32_t addr) { 
        uint64_t val = read64(addr);
        double d;
        std::memcpy(&d, &val, 8);
        return d;
    }
    
    void write_f64(uint32_t addr, double value) { 
        uint64_t val;
        std::memcpy(&val, &value, 8);
        write64(addr, val);
    }
};

struct CPUContext {
    // General Purpose Registers (r0-r31)
    std::array<uint32_t, 32> gpr;
    
    // Floating Point Registers (f0-f31)
    std::array<double, 32> fpr;

    // Condition Registers (cr0-cr7)
    std::array<ConditionField, 8> cr;
    
    // Special Purpose Registers
    uint32_t pc;    // Program Counter
    uint32_t lr;    // Link Register
    uint32_t ctr;   // Count Register
    uint32_t xer;   // Fixed-Point Exception Register
    uint32_t fpscr; // Floating-Point Status and Control Register
    
    // Memory Management Unit
    MMU mmu;
    
    // Default constructor to zero init
    CPUContext() : gpr{0}, fpr{0.0}, pc(0), lr(0), ctr(0), xer(0), fpscr(0) {}
};

} // namespace runtime
} // namespace nwii
