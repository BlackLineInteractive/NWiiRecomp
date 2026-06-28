#include "runtime/cpu_context.h"
#include <iostream>
#include <cstdlib>
#include <iomanip>

namespace nwii {
namespace runtime {

void micro_interpret(CPUContext& ctx, uint32_t opcode, uint32_t pc) {
    uint32_t op = opcode >> 26;
    
    if (op == 11) { // cmpwi
        uint32_t crfD = (opcode >> 23) & 0x7;
        uint32_t rA = (opcode >> 16) & 0x1F;
        int16_t simm = opcode & 0xFFFF;
        
        int32_t valA = ctx.gpr[rA];
        int32_t valB = simm;
        
        ctx.cr[crfD].lt = (valA < valB);
        ctx.cr[crfD].gt = (valA > valB);
        ctx.cr[crfD].eq = (valA == valB);
        ctx.cr[crfD].so = (ctx.xer >> 31) & 1;
        ctx.pc = pc + 4;
        return;
    }
    else if (op == 16) { // bc (Branch Conditional)
        uint32_t BO = (opcode >> 21) & 0x1F;
        uint32_t BI = (opcode >> 16) & 0x1F;
        int16_t bd = opcode & 0xFFFC;
        uint32_t AA = (opcode >> 1) & 1;
        uint32_t LK = opcode & 1;
        
        bool ctr_ok = true;
        if ((BO & 0x4) == 0) {
            ctx.ctr--;
            ctr_ok = ((BO & 0x2) ? (ctx.ctr == 0) : (ctx.ctr != 0));
        }
        
        bool cond_ok = true;
        if ((BO & 0x10) == 0) {
            uint32_t cr_idx = BI / 4;
            uint32_t cr_bit = BI % 4;
            bool bit_val = false;
            if (cr_bit == 0) bit_val = ctx.cr[cr_idx].lt;
            else if (cr_bit == 1) bit_val = ctx.cr[cr_idx].gt;
            else if (cr_bit == 2) bit_val = ctx.cr[cr_idx].eq;
            else if (cr_bit == 3) bit_val = ctx.cr[cr_idx].so;
            cond_ok = ((BO & 0x8) ? bit_val : !bit_val);
        }
        
        if (ctr_ok && cond_ok) {
            uint32_t target = AA ? (int32_t)bd : (pc + (int32_t)bd);
            if (LK) ctx.lr = pc + 4;
            ctx.pc = target;
        } else {
            ctx.pc = pc + 4;
        }
        return;
    }
    else if (op == 18) { // b/bl
        uint32_t li = opcode & 0x03FFFFFC;
        if (li & 0x02000000) li |= 0xFC000000; // sign extend
        uint32_t AA = (opcode >> 1) & 1;
        uint32_t LK = opcode & 1;
        
        uint32_t target = AA ? li : (pc + li);
        if (LK) ctx.lr = pc + 4;
        ctx.pc = target;
        return;
    }
    else if (op == 31) {
        uint32_t xo = (opcode >> 1) & 0x3FF;
        if (xo == 339) { // mfspr
            uint32_t rD = (opcode >> 21) & 0x1F;
            uint32_t spr = (opcode >> 11) & 0x3FF;
            uint32_t mapped_spr = ((spr & 0x1F) << 5) | ((spr >> 5) & 0x1F);
            if (mapped_spr == 8) { // mflr
                ctx.gpr[rD] = ctx.lr;
                ctx.pc = pc + 4;
                return;
            }
        }
    }

    std::cerr << "[MICRO-INTERPRETER] FATAL! Unknown instruction 0x" 
              << std::hex << std::setfill('0') << std::setw(8) << opcode 
              << " at PC: 0x" << pc << std::dec << std::endl;
    std::exit(1);
}

}
}
