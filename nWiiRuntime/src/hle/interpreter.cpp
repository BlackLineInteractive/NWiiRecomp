#include "runtime/cpu_context.h"
#include <iostream>
#include <cstdlib>
#include <cstring>
#include <iomanip>

// PPC750 integer-subset interpreter. Used as a dispatcher fallback for
// code that does not exist in the DOL image: routines the game copies
// into low memory at runtime (arena clear, cache helpers, TRK stubs),
// trampolines, and self-modifying code. Executes straight from guest
// memory one instruction at a time until control returns to recompiled
// code.

namespace nwii {
namespace runtime {

// Address ranges covered by statically recompiled code (DOL text sections),
// registered by the loader. Anything outside is interpreter territory:
// low-memory helpers, overlays the game streams into BSS/heap, and so on.
struct CodeRange { uint32_t start, end; };
static CodeRange g_code_ranges[16];
static int g_code_range_count = 0;

void add_recompiled_range(uint32_t start, uint32_t end) {
    if (g_code_range_count < 16)
        g_code_ranges[g_code_range_count++] = {start, end};
}

bool in_recompiled_code(uint32_t pc) {
    for (int i = 0; i < g_code_range_count; ++i)
        if (pc >= g_code_ranges[i].start && pc < g_code_ranges[i].end)
            return true;
    return false;
}

static inline void set_cr0(CPUContext& ctx, int32_t v) {
    ctx.cr[0].lt = v < 0;
    ctx.cr[0].gt = v > 0;
    ctx.cr[0].eq = v == 0;
    ctx.cr[0].so = (ctx.xer >> 31) & 1;
}

static inline void set_crf(CPUContext& ctx, uint32_t crf, bool lt, bool gt, bool eq) {
    ctx.cr[crf].lt = lt;
    ctx.cr[crf].gt = gt;
    ctx.cr[crf].eq = eq;
    ctx.cr[crf].so = (ctx.xer >> 31) & 1;
}

// Executes one instruction at ctx.pc. Returns true if it was handled.
bool interpret_one(CPUContext& ctx) {
    uint32_t pc = ctx.pc;
    uint32_t insn = ctx.mmu.read32(pc);
    uint32_t op = insn >> 26;
    uint32_t rD = (insn >> 21) & 0x1F;
    uint32_t rA = (insn >> 16) & 0x1F;
    uint32_t rB = (insn >> 11) & 0x1F;
    int16_t simm = (int16_t)(insn & 0xFFFF);
    uint16_t uimm = (uint16_t)(insn & 0xFFFF);
    bool rc = insn & 1;

    auto ea_ra0 = [&](int32_t off) {
        return (rA ? ctx.gpr[rA] : 0) + off;
    };

    switch (op) {
    case 7: // mulli
        ctx.gpr[rD] = (int32_t)ctx.gpr[rA] * simm;
        break;
    case 8: { // subfic
        uint64_t r = (uint64_t)(uint32_t)(~ctx.gpr[rA]) + (uint32_t)simm + 1;
        ctx.gpr[rD] = (uint32_t)r;
        if (r >> 32) ctx.xer |= 0x20000000; else ctx.xer &= ~0x20000000;
        break;
    }
    case 10: { // cmpli
        uint32_t a = ctx.gpr[rA];
        uint32_t b = uimm;
        set_crf(ctx, rD >> 2, a < b, a > b, a == b);
        break;
    }
    case 11: { // cmpi
        int32_t a = (int32_t)ctx.gpr[rA];
        set_crf(ctx, rD >> 2, a < simm, a > simm, a == simm);
        break;
    }
    case 12: { // addic
        uint64_t r = (uint64_t)ctx.gpr[rA] + (uint32_t)(int32_t)simm;
        ctx.gpr[rD] = (uint32_t)r;
        if (r >> 32) ctx.xer |= 0x20000000; else ctx.xer &= ~0x20000000;
        break;
    }
    case 13: { // addic.
        uint64_t r = (uint64_t)ctx.gpr[rA] + (uint32_t)(int32_t)simm;
        ctx.gpr[rD] = (uint32_t)r;
        if (r >> 32) ctx.xer |= 0x20000000; else ctx.xer &= ~0x20000000;
        set_cr0(ctx, (int32_t)ctx.gpr[rD]);
        break;
    }
    case 14: // addi
        ctx.gpr[rD] = (rA ? ctx.gpr[rA] : 0) + simm;
        break;
    case 15: // addis
        ctx.gpr[rD] = (rA ? ctx.gpr[rA] : 0) + ((int32_t)simm << 16);
        break;
    case 16: { // bc
        uint32_t BO = rD, BI = rA;
        int32_t bd = (int16_t)(insn & 0xFFFC);
        bool ctr_ok = true;
        if (!(BO & 0x04)) {
            ctx.ctr--;
            ctr_ok = (BO & 0x02) ? (ctx.ctr == 0) : (ctx.ctr != 0);
        }
        bool cond_ok = true;
        if (!(BO & 0x10)) {
            uint32_t ci = BI / 4, cb = BI % 4;
            bool bit = cb == 0 ? ctx.cr[ci].lt : cb == 1 ? ctx.cr[ci].gt
                     : cb == 2 ? ctx.cr[ci].eq : ctx.cr[ci].so;
            cond_ok = (BO & 0x08) ? bit : !bit;
        }
        if (insn & 1) ctx.lr = pc + 4;
        ctx.pc = (ctr_ok && cond_ok) ? ((insn & 2) ? (uint32_t)bd : pc + bd)
                                     : pc + 4;
        return true;
    }
    case 17: // sc
        handle_syscall(ctx);
        break;
    case 18: { // b/bl/ba/bla
        int32_t li = insn & 0x03FFFFFC;
        if (li & 0x02000000) li |= 0xFC000000;
        if (insn & 1) ctx.lr = pc + 4;
        ctx.pc = (insn & 2) ? (uint32_t)li : pc + li;
        return true;
    }
    case 19: { // branch unit / CR ops
        uint32_t xo = (insn >> 1) & 0x3FF;
        if (xo == 16) { // bclr
            uint32_t BO = rD, BI = rA;
            bool ctr_ok = true;
            if (!(BO & 0x04)) {
                ctx.ctr--;
                ctr_ok = (BO & 0x02) ? (ctx.ctr == 0) : (ctx.ctr != 0);
            }
            bool cond_ok = true;
            if (!(BO & 0x10)) {
                uint32_t ci = BI / 4, cb = BI % 4;
                bool bit = cb == 0 ? ctx.cr[ci].lt : cb == 1 ? ctx.cr[ci].gt
                         : cb == 2 ? ctx.cr[ci].eq : ctx.cr[ci].so;
                cond_ok = (BO & 0x08) ? bit : !bit;
            }
            uint32_t target = ctx.lr & ~3u;
            if (insn & 1) ctx.lr = pc + 4;
            ctx.pc = (ctr_ok && cond_ok) ? target : pc + 4;
            return true;
        }
        if (xo == 528) { // bcctr
            uint32_t BO = rD, BI = rA;
            bool cond_ok = true;
            if (!(BO & 0x10)) {
                uint32_t ci = BI / 4, cb = BI % 4;
                bool bit = cb == 0 ? ctx.cr[ci].lt : cb == 1 ? ctx.cr[ci].gt
                         : cb == 2 ? ctx.cr[ci].eq : ctx.cr[ci].so;
                cond_ok = (BO & 0x08) ? bit : !bit;
            }
            uint32_t target = ctx.ctr & ~3u;
            if (insn & 1) ctx.lr = pc + 4;
            ctx.pc = cond_ok ? target : pc + 4;
            return true;
        }
        if (xo == 50) { // rfi
            ctx.msr = ctx.srr1;
            ctx.pc = ctx.srr0;
            return true;
        }
        if (xo == 150) break; // isync
        // CR logic ops: crand/cror/crxor/... rarely matter in stubs
        break;
    }
    case 20: { // rlwimi
        uint32_t sh = rB, mb = (insn >> 6) & 0x1F, me = (insn >> 1) & 0x1F;
        uint32_t r = (ctx.gpr[rD] << sh) | (ctx.gpr[rD] >> ((32 - sh) & 31));
        uint32_t mask = (mb <= me)
            ? (((uint32_t)-1 >> mb) & ((uint32_t)-1 << (31 - me)))
            : (((uint32_t)-1 >> mb) | ((uint32_t)-1 << (31 - me)));
        ctx.gpr[rA] = (r & mask) | (ctx.gpr[rA] & ~mask);
        if (rc) set_cr0(ctx, (int32_t)ctx.gpr[rA]);
        break;
    }
    case 21: { // rlwinm
        uint32_t sh = rB, mb = (insn >> 6) & 0x1F, me = (insn >> 1) & 0x1F;
        uint32_t r = (ctx.gpr[rD] << sh) | (ctx.gpr[rD] >> ((32 - sh) & 31));
        uint32_t mask = (mb <= me)
            ? (((uint32_t)-1 >> mb) & ((uint32_t)-1 << (31 - me)))
            : (((uint32_t)-1 >> mb) | ((uint32_t)-1 << (31 - me)));
        ctx.gpr[rA] = r & mask;
        if (rc) set_cr0(ctx, (int32_t)ctx.gpr[rA]);
        break;
    }
    case 23: { // rlwnm
        uint32_t sh = ctx.gpr[rB] & 0x1F, mb = (insn >> 6) & 0x1F, me = (insn >> 1) & 0x1F;
        uint32_t r = (ctx.gpr[rD] << sh) | (ctx.gpr[rD] >> ((32 - sh) & 31));
        uint32_t mask = (mb <= me)
            ? (((uint32_t)-1 >> mb) & ((uint32_t)-1 << (31 - me)))
            : (((uint32_t)-1 >> mb) | ((uint32_t)-1 << (31 - me)));
        ctx.gpr[rA] = r & mask;
        if (rc) set_cr0(ctx, (int32_t)ctx.gpr[rA]);
        break;
    }
    case 24: ctx.gpr[rA] = ctx.gpr[rD] | uimm; break;            // ori
    case 25: ctx.gpr[rA] = ctx.gpr[rD] | ((uint32_t)uimm << 16); break; // oris
    case 26: ctx.gpr[rA] = ctx.gpr[rD] ^ uimm; break;            // xori
    case 27: ctx.gpr[rA] = ctx.gpr[rD] ^ ((uint32_t)uimm << 16); break; // xoris
    case 28: ctx.gpr[rA] = ctx.gpr[rD] & uimm; set_cr0(ctx, (int32_t)ctx.gpr[rA]); break; // andi.
    case 29: ctx.gpr[rA] = ctx.gpr[rD] & ((uint32_t)uimm << 16); set_cr0(ctx, (int32_t)ctx.gpr[rA]); break; // andis.
    case 31: {
        uint32_t xo = (insn >> 1) & 0x3FF;
        switch (xo) {
        case 0: { // cmp
            int32_t a = (int32_t)ctx.gpr[rA], b = (int32_t)ctx.gpr[rB];
            set_crf(ctx, rD >> 2, a < b, a > b, a == b);
            break;
        }
        case 32: { // cmpl
            uint32_t a = ctx.gpr[rA], b = ctx.gpr[rB];
            set_crf(ctx, rD >> 2, a < b, a > b, a == b);
            break;
        }
        case 266: ctx.gpr[rD] = ctx.gpr[rA] + ctx.gpr[rB]; if (rc) set_cr0(ctx, (int32_t)ctx.gpr[rD]); break; // add
        case 40:  ctx.gpr[rD] = ctx.gpr[rB] - ctx.gpr[rA]; if (rc) set_cr0(ctx, (int32_t)ctx.gpr[rD]); break; // subf
        case 104: ctx.gpr[rD] = -(int32_t)ctx.gpr[rA]; if (rc) set_cr0(ctx, (int32_t)ctx.gpr[rD]); break; // neg
        case 235: ctx.gpr[rD] = (int32_t)ctx.gpr[rA] * (int32_t)ctx.gpr[rB]; if (rc) set_cr0(ctx, (int32_t)ctx.gpr[rD]); break; // mullw
        case 75:  ctx.gpr[rD] = (uint32_t)(((int64_t)(int32_t)ctx.gpr[rA] * (int32_t)ctx.gpr[rB]) >> 32); break; // mulhw
        case 11:  ctx.gpr[rD] = (uint32_t)(((uint64_t)ctx.gpr[rA] * ctx.gpr[rB]) >> 32); break; // mulhwu
        case 491: ctx.gpr[rD] = ctx.gpr[rB] ? (uint32_t)((int32_t)ctx.gpr[rA] / (int32_t)ctx.gpr[rB]) : 0; break; // divw
        case 459: ctx.gpr[rD] = ctx.gpr[rB] ? ctx.gpr[rA] / ctx.gpr[rB] : 0; break; // divwu
        case 28:  ctx.gpr[rA] = ctx.gpr[rD] & ctx.gpr[rB]; if (rc) set_cr0(ctx, (int32_t)ctx.gpr[rA]); break; // and
        case 60:  ctx.gpr[rA] = ctx.gpr[rD] & ~ctx.gpr[rB]; if (rc) set_cr0(ctx, (int32_t)ctx.gpr[rA]); break; // andc
        case 444: ctx.gpr[rA] = ctx.gpr[rD] | ctx.gpr[rB]; if (rc) set_cr0(ctx, (int32_t)ctx.gpr[rA]); break; // or
        case 412: ctx.gpr[rA] = ctx.gpr[rD] | ~ctx.gpr[rB]; if (rc) set_cr0(ctx, (int32_t)ctx.gpr[rA]); break; // orc
        case 316: ctx.gpr[rA] = ctx.gpr[rD] ^ ctx.gpr[rB]; if (rc) set_cr0(ctx, (int32_t)ctx.gpr[rA]); break; // xor
        case 476: ctx.gpr[rA] = ~(ctx.gpr[rD] & ctx.gpr[rB]); if (rc) set_cr0(ctx, (int32_t)ctx.gpr[rA]); break; // nand
        case 124: ctx.gpr[rA] = ~(ctx.gpr[rD] | ctx.gpr[rB]); if (rc) set_cr0(ctx, (int32_t)ctx.gpr[rA]); break; // nor
        case 284: ctx.gpr[rA] = ~(ctx.gpr[rD] ^ ctx.gpr[rB]); if (rc) set_cr0(ctx, (int32_t)ctx.gpr[rA]); break; // eqv
        case 954: ctx.gpr[rA] = (int32_t)(int8_t)ctx.gpr[rD]; if (rc) set_cr0(ctx, (int32_t)ctx.gpr[rA]); break; // extsb
        case 922: ctx.gpr[rA] = (int32_t)(int16_t)ctx.gpr[rD]; if (rc) set_cr0(ctx, (int32_t)ctx.gpr[rA]); break; // extsh
        case 26: { // cntlzw
            uint32_t v = ctx.gpr[rD];
            ctx.gpr[rA] = v ? __builtin_clz(v) : 32;
            if (rc) set_cr0(ctx, (int32_t)ctx.gpr[rA]);
            break;
        }
        case 24: { // slw
            uint32_t sh = ctx.gpr[rB] & 0x3F;
            ctx.gpr[rA] = sh > 31 ? 0 : ctx.gpr[rD] << sh;
            if (rc) set_cr0(ctx, (int32_t)ctx.gpr[rA]);
            break;
        }
        case 536: { // srw
            uint32_t sh = ctx.gpr[rB] & 0x3F;
            ctx.gpr[rA] = sh > 31 ? 0 : ctx.gpr[rD] >> sh;
            if (rc) set_cr0(ctx, (int32_t)ctx.gpr[rA]);
            break;
        }
        case 792: { // sraw
            uint32_t sh = ctx.gpr[rB] & 0x3F;
            int32_t v = (int32_t)ctx.gpr[rD];
            ctx.gpr[rA] = sh > 31 ? (v < 0 ? 0xFFFFFFFF : 0) : (uint32_t)(v >> sh);
            if (rc) set_cr0(ctx, (int32_t)ctx.gpr[rA]);
            break;
        }
        case 824: { // srawi
            uint32_t sh = rB;
            ctx.gpr[rA] = (uint32_t)((int32_t)ctx.gpr[rD] >> sh);
            if (rc) set_cr0(ctx, (int32_t)ctx.gpr[rA]);
            break;
        }
        case 339: { // mfspr
            uint32_t spr = ((insn >> 16) & 0x1F) | (((insn >> 11) & 0x1F) << 5);
            if (spr == 8) ctx.gpr[rD] = ctx.lr;
            else if (spr == 9) ctx.gpr[rD] = ctx.ctr;
            else if (spr == 1) ctx.gpr[rD] = ctx.xer;
            else if (spr == 22) ctx.gpr[rD] = ctx.read_dec();
            else if (spr == 26) ctx.gpr[rD] = ctx.srr0;
            else if (spr == 27) ctx.gpr[rD] = ctx.srr1;
            else if (spr >= 912 && spr <= 919) ctx.gpr[rD] = ctx.gqr[spr - 912];
            else ctx.gpr[rD] = 0;
            break;
        }
        case 467: { // mtspr
            uint32_t spr = ((insn >> 16) & 0x1F) | (((insn >> 11) & 0x1F) << 5);
            if (spr == 8) ctx.lr = ctx.gpr[rD];
            else if (spr == 9) ctx.ctr = ctx.gpr[rD];
            else if (spr == 1) ctx.xer = ctx.gpr[rD];
            else if (spr == 22) ctx.write_dec(ctx.gpr[rD]);
            else if (spr == 26) ctx.srr0 = ctx.gpr[rD];
            else if (spr == 27) ctx.srr1 = ctx.gpr[rD];
            else if (spr >= 912 && spr <= 919) ctx.gqr[spr - 912] = ctx.gpr[rD];
            break;
        }
        case 371: { // mftb
            uint32_t spr = ((insn >> 16) & 0x1F) | (((insn >> 11) & 0x1F) << 5);
            ctx.gpr[rD] = (spr == 269) ? (uint32_t)(ctx.inst_count >> 32)
                                       : (uint32_t)ctx.inst_count;
            break;
        }
        case 83:  ctx.gpr[rD] = ctx.msr; break;  // mfmsr
        case 146: ctx.msr = ctx.gpr[rD]; break;  // mtmsr
        case 19: { // mfcr
            uint32_t v = 0;
            for (int i = 0; i < 8; i++) {
                v |= (ctx.cr[i].lt ? 8u : 0) << (28 - i * 4);
                v |= (ctx.cr[i].gt ? 4u : 0) << (28 - i * 4);
                v |= (ctx.cr[i].eq ? 2u : 0) << (28 - i * 4);
                v |= (ctx.cr[i].so ? 1u : 0) << (28 - i * 4);
            }
            ctx.gpr[rD] = v;
            break;
        }
        case 144: { // mtcrf
            uint32_t crm = (insn >> 12) & 0xFF;
            uint32_t v = ctx.gpr[rD];
            for (int i = 0; i < 8; i++) {
                if (crm & (0x80 >> i)) {
                    uint32_t nib = (v >> (28 - i * 4)) & 0xF;
                    ctx.cr[i].lt = nib & 8;
                    ctx.cr[i].gt = nib & 4;
                    ctx.cr[i].eq = nib & 2;
                    ctx.cr[i].so = nib & 1;
                }
            }
            break;
        }
        case 23:  ctx.gpr[rD] = ctx.mmu.read32(ea_ra0(0) + ctx.gpr[rB]); break;  // lwzx
        case 87:  ctx.gpr[rD] = ctx.mmu.read8(ea_ra0(0) + ctx.gpr[rB]); break;   // lbzx
        case 279: ctx.gpr[rD] = ctx.mmu.read16(ea_ra0(0) + ctx.gpr[rB]); break;  // lhzx
        case 343: ctx.gpr[rD] = (int32_t)(int16_t)ctx.mmu.read16(ea_ra0(0) + ctx.gpr[rB]); break; // lhax
        case 151: ctx.mmu.write32(ea_ra0(0) + ctx.gpr[rB], ctx.gpr[rD]); break;  // stwx
        case 215: ctx.mmu.write8(ea_ra0(0) + ctx.gpr[rB], (uint8_t)ctx.gpr[rD]); break; // stbx
        case 407: ctx.mmu.write16(ea_ra0(0) + ctx.gpr[rB], (uint16_t)ctx.gpr[rD]); break; // sthx
        case 20: { // lwarx
            ctx.gpr[rD] = ctx.mmu.read32(ea_ra0(0) + ctx.gpr[rB]);
            ctx.reservation_addr = ea_ra0(0) + ctx.gpr[rB];
            break;
        }
        case 150: { // stwcx.
            ctx.mmu.write32(ea_ra0(0) + ctx.gpr[rB], ctx.gpr[rD]);
            ctx.cr[0].lt = ctx.cr[0].gt = false;
            ctx.cr[0].eq = true;
            ctx.cr[0].so = (ctx.xer >> 31) & 1;
            break;
        }
        case 1014: { // dcbz: zero the 32-byte cache line
            uint32_t ea = (ea_ra0(0) + ctx.gpr[rB]) & ~31u;
            for (int i = 0; i < 32; i += 4) ctx.mmu.write32(ea + i, 0);
            break;
        }
        // Cache/sync: no-ops in HLE
        case 54: case 86: case 246: case 470: case 598: case 982: case 854:
            break;
        default:
            std::cerr << "[Interp] Unhandled op31 xo=" << xo << " at 0x"
                      << std::hex << pc << std::dec << " (nop)\n";
            break;
        }
        break;
    }
    case 32: ctx.gpr[rD] = ctx.mmu.read32(ea_ra0(simm)); break;           // lwz
    case 33: ctx.gpr[rD] = ctx.mmu.read32(ctx.gpr[rA] + simm); ctx.gpr[rA] += simm; break; // lwzu
    case 34: ctx.gpr[rD] = ctx.mmu.read8(ea_ra0(simm)); break;            // lbz
    case 35: ctx.gpr[rD] = ctx.mmu.read8(ctx.gpr[rA] + simm); ctx.gpr[rA] += simm; break;  // lbzu
    case 36: ctx.mmu.write32(ea_ra0(simm), ctx.gpr[rD]); break;           // stw
    case 37: ctx.mmu.write32(ctx.gpr[rA] + simm, ctx.gpr[rD]); ctx.gpr[rA] += simm; break; // stwu
    case 38: ctx.mmu.write8(ea_ra0(simm), (uint8_t)ctx.gpr[rD]); break;   // stb
    case 39: ctx.mmu.write8(ctx.gpr[rA] + simm, (uint8_t)ctx.gpr[rD]); ctx.gpr[rA] += simm; break; // stbu
    case 40: ctx.gpr[rD] = ctx.mmu.read16(ea_ra0(simm)); break;           // lhz
    case 41: ctx.gpr[rD] = ctx.mmu.read16(ctx.gpr[rA] + simm); ctx.gpr[rA] += simm; break; // lhzu
    case 42: ctx.gpr[rD] = (int32_t)(int16_t)ctx.mmu.read16(ea_ra0(simm)); break; // lha
    case 44: ctx.mmu.write16(ea_ra0(simm), (uint16_t)ctx.gpr[rD]); break; // sth
    case 45: ctx.mmu.write16(ctx.gpr[rA] + simm, (uint16_t)ctx.gpr[rD]); ctx.gpr[rA] += simm; break; // sthu
    case 46: { // lmw
        uint32_t ea = ea_ra0(simm);
        for (uint32_t r = rD; r < 32; r++, ea += 4) ctx.gpr[r] = ctx.mmu.read32(ea);
        break;
    }
    case 47: { // stmw
        uint32_t ea = ea_ra0(simm);
        for (uint32_t r = rD; r < 32; r++, ea += 4) ctx.mmu.write32(ea, ctx.gpr[r]);
        break;
    }
    case 48: ctx.fpr[rD] = ctx.mmu.read_f32(ea_ra0(simm)); break;         // lfs
    case 49: { // lfsu: load float single with update
        uint32_t ea = ctx.gpr[rA] + simm;
        ctx.fpr[rD] = ctx.mmu.read_f32(ea);
        ctx.gpr[rA] = ea;
        break;
    }
    case 50: ctx.fpr[rD] = ctx.mmu.read_f64(ea_ra0(simm)); break;         // lfd
    case 51: { // lfdu: load float double with update
        uint32_t ea = ctx.gpr[rA] + simm;
        ctx.fpr[rD] = ctx.mmu.read_f64(ea);
        ctx.gpr[rA] = ea;
        break;
    }
    case 52: ctx.mmu.write_f32(ea_ra0(simm), (float)ctx.fpr[rD]); break;  // stfs
    case 53: { // stfsu: store float single with update
        uint32_t ea = ctx.gpr[rA] + simm;
        ctx.mmu.write_f32(ea, (float)ctx.fpr[rD]);
        ctx.gpr[rA] = ea;
        break;
    }
    case 54: ctx.mmu.write_f64(ea_ra0(simm), ctx.fpr[rD]); break;         // stfd
    case 55: { // stfdu: store float double with update
        uint32_t ea = ctx.gpr[rA] + simm;
        ctx.mmu.write_f64(ea, ctx.fpr[rD]);
        ctx.gpr[rA] = ea;
        break;
    }
    default:
        std::cerr << "[Interp] Unhandled opcode " << op << " at 0x"
                  << std::hex << pc << " insn=0x" << insn << std::dec
                  << " (nop)\n";
        break;
    }

    ctx.pc = pc + 4;
    return true;
}

// Dispatcher fallback: interpret at ctx.pc. Keeps executing while control
// stays inside the OS low-memory region (runtime-generated helpers live
// below the 0x80004000 application base); once the code branches back to
// the application region the dispatcher re-enters recompiled functions.
void interpret_step(CPUContext& ctx) {
    static int announce_budget = 200;
    if (announce_budget > 0) {
        announce_budget--;
        std::cout << "[Interp] Interpreting at 0x" << std::hex << ctx.pc
                  << " lr=0x" << ctx.lr << " r1=0x" << ctx.gpr[1]
                  << std::dec << "\n";
    }
    for (uint64_t i = 0; i < 100000000ULL; ++i) {
        uint32_t before = ctx.pc;
        ++ctx.inst_count;
        interpret_one(ctx);
        if (ctx.pc != before + 4) {
            if ((ctx.pc & 0x3FFFFFFF) < 0x100) {
                std::cout << "[Interp] Jump to low address 0x" << std::hex
                          << ctx.pc << " from 0x" << before << " insn=0x"
                          << ctx.mmu.read32(before) << " lr=0x" << ctx.lr
                          << " ctr=0x" << ctx.ctr << std::dec << "\n";
            }
            // Control transfer back into recompiled code: let the
            // dispatcher take over. Otherwise keep interpreting (helpers,
            // streamed overlays, generated code).
            if (in_recompiled_code(ctx.pc))
                return;
            // Give interrupts/callbacks a chance during long stretches
            if ((i & 0xFFF) == 0 && process_pending_callbacks(ctx))
                return;
        }
    }
    std::cout << "[Interp] Budget exceeded at 0x" << std::hex << ctx.pc
              << std::dec << "\n";
}

// Kept for compatibility with older generated code
void micro_interpret(CPUContext& ctx, uint32_t opcode, uint32_t pc) {
    ctx.pc = pc;
    interpret_one(ctx);
}

}
}
