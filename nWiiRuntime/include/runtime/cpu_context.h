#pragma once
#include <cstdint>
#include <array>

namespace nwii {
namespace runtime {

struct ConditionField {
    bool lt; // Less Than
    bool gt; // Greater Than
    bool eq; // Equal
    bool so; // Summary Overflow
    
    ConditionField() : lt(false), gt(false), eq(false), so(false) {}
};

struct MMU {
    // Very basic placeholder MMU
    uint8_t* ram;
    uint32_t ram_size;
    
    MMU() : ram(nullptr), ram_size(0) {}
    uint8_t read8(uint32_t addr) { return 0; /* stub */ }
    void write8(uint32_t addr, uint8_t value) { /* stub */ }
    
    uint16_t read16(uint32_t addr) { return 0; /* stub */ }
    void write16(uint32_t addr, uint16_t value) { /* stub */ }
    
    uint32_t read32(uint32_t addr) { return 0; /* stub */ }
    void write32(uint32_t addr, uint32_t value) { /* stub */ }
    
    float read_f32(uint32_t addr) { return 0.0f; /* stub */ }
    void write_f32(uint32_t addr, float value) { /* stub */ }
    
    double read_f64(uint32_t addr) { return 0.0; /* stub */ }
    void write_f64(uint32_t addr, double value) { /* stub */ }
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
