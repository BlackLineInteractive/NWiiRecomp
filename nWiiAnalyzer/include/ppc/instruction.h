#pragma once

#include <cstdint>

namespace nwii {
namespace ppc {

// PowerPC instructions are always 32 bits
class Instruction {
public:
    explicit Instruction(uint32_t value) : value_(value) {}

    uint32_t value() const { return value_; }

    // Primary opcode (bits 0-5)
    uint32_t opcode() const { return (value_ >> 26) & 0x3F; }

    uint32_t rd() const { return (value_ >> 21) & 0x1F; }
    uint32_t rs() const { return rd(); }
    uint32_t bo() const { return rd(); }
    uint32_t ra() const { return (value_ >> 16) & 0x1F; }
    uint32_t bi() const { return ra(); }
    uint32_t rb() const { return (value_ >> 11) & 0x1F; }
    uint32_t rc() const { return (value_ >> 6) & 0x1F; }
    int16_t simm() const { return static_cast<int16_t>(value_ & 0xFFFF); }
    uint16_t uimm() const { return static_cast<uint16_t>(value_ & 0xFFFF); }
    uint32_t extended_opcode() const { return (value_ >> 1) & 0x3FF; }

    // Is it a branch and link? (e.g. bl)
    bool is_branch_link() const {
        return (opcode() == 18) && (value_ & 1); // LK bit is 1
    }

    // Is it an unconditional branch? (e.g. b, bl)
    bool is_unconditional_branch() const {
        return opcode() == 18;
    }

    // Is it a branch to link register? (e.g. blr)
    bool is_branch_to_lr() const {
        // Opcode 19, Extended Opcode 16
        if (opcode() != 19) return false;
        uint32_t xo = extended_opcode();
        return xo == 16;
    }

    // Is it an unconditional indirect branch that terminates a block? (e.g. blr, bctr)
    bool is_unconditional_indirect_branch() const {
        if (opcode() != 19) return false;
        uint32_t xo = extended_opcode();
        // 16 = bclr (blr), 528 = bcctr (bctr), 50 = rfi
        if (xo == 16 || xo == 528 || xo == 50) {
            if (xo == 50) return true; // rfi is always unconditional
            // BO field determines if it's conditional. 
            // Branch always if BO[0] (16) and BO[2] (4) are set.
            return (bo() & 0x14) == 0x14;
        }
        return false;
    }

    // Get branch target address
    // `current_pc` is needed because most branches are relative
    uint32_t branch_target(uint32_t current_pc) const {
        if (opcode() == 18) {
            // b, bl, ba, bla
            uint32_t li = (value_ >> 2) & 0xFFFFFF; // 24 bits
            // sign extend to 32 bits
            int32_t offset = (li & 0x800000) ? (li | 0xFF000000) : li;
            offset <<= 2; // multiply by 4

            bool aa = (value_ >> 1) & 1; // Absolute address bit
            if (aa) {
                return static_cast<uint32_t>(offset);
            } else {
                return current_pc + offset;
            }
        }
        if (opcode() == 16) {
            // bc, bca, bcl, bcla (conditional branches)
            uint32_t bd = (value_ >> 2) & 0x3FFF; // 14 bits
            // sign extend to 32 bits
            int32_t offset = (bd & 0x2000) ? (bd | 0xFFFFC000) : bd;
            offset <<= 2;
            
            bool aa = (value_ >> 1) & 1;
            if (aa) {
                return static_cast<uint32_t>(offset);
            } else {
                return current_pc + offset;
            }
        }
        return 0; // Not a supported branch for target extraction yet
    }

private:
    uint32_t value_;
};

} // namespace ppc
} // namespace nwii
