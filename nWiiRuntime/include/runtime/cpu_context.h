#pragma once
#include <array>
#include <cmath>
#include <csetjmp>
#include <cstdint>
#include <cstring>
#include <vector>
#include <iostream>

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
void HW_Reg_Write16(uint32_t addr, uint16_t val);
void HW_Reg_Write32(uint32_t addr, uint32_t val);
}

struct CPUContext;
void syscall_handler(CPUContext& ctx);

struct MMU {
  std::vector<uint8_t> mem1;
  std::vector<uint8_t> mem2;

  uint64_t inst_count = 0;

  MMU() {
    mem1.resize(24 * 1024 * 1024); // 24MB MEM1
    mem2.resize(64 * 1024 * 1024); // 64MB MEM2
  }

  uint8_t *get_ptr(uint32_t addr) {
    if ((addr & 0xF0000000) == 0x80000000 ||
        (addr & 0xF0000000) == 0xC0000000) {
      uint32_t offset = addr & 0x01FFFFFF;
      if (offset < mem1.size())
        return &mem1[offset];
    }
    if ((addr & 0xF0000000) == 0x90000000 ||
        (addr & 0xF0000000) == 0xD0000000) {
      uint32_t offset = addr & 0x03FFFFFF;
      if (offset < mem2.size())
        return &mem2[offset];
    }
    return nullptr;
  }

  uint8_t read8(uint32_t addr) {
    uint8_t *ptr = get_ptr(addr);
    return ptr ? *ptr : 0;
  }

  void write8(uint32_t addr, uint8_t value) {
    if (addr == 0xCC008000) {
      GX_WGPIPE_Write8(value);
      return;
    }
    uint8_t *ptr = get_ptr(addr);
    if (ptr)
      *ptr = value;
  }

  uint16_t read16(uint32_t addr) {
    if ((addr & 0xFFFF0000) == 0xCC000000 || (addr & 0xFFFF0000) == 0xCD000000)
      return HW_Reg_Read16(addr);
    uint8_t *ptr = get_ptr(addr);
    if (!ptr)
      return 0;
    return (uint16_t)(ptr[0] << 8 | ptr[1]);
  }

  void write16(uint32_t addr, uint16_t value) {
    if (addr == 0xCC008000) {
      GX_WGPIPE_Write16(value);
      return;
    }
    if ((addr & 0xFFFF0000) == 0xCC000000 || (addr & 0xFFFF0000) == 0xCD000000) {
      HW_Reg_Write16(addr, value);
      return;
    }
    uint8_t *ptr = get_ptr(addr);
    if (!ptr)
      return;
    ptr[0] = (value >> 8) & 0xFF;
    ptr[1] = value & 0xFF;
  }

  uint32_t read32(uint32_t addr) {
    uint32_t paddr = addr & 0x3FFFFFFF;
    // Hardcode OS globals to fix MEM2 0MB issue
    if (paddr == 0x00000024) return 0x00000002u;          // Console Type = Wii Retail
    if (paddr == 0x00000028) return 24u * 1024u * 1024u;  // 24MB MEM1
    if (paddr >= 0x3100 && paddr <= 0x3140) {
      std::cout << "[DEBUG] Game read from OS Global: 0x" << std::hex << paddr << std::dec << "\n";
    }

    if (paddr == 0x00003118) return 64u * 1024u * 1024u;  // Physical MEM2
    if (paddr == 0x0000311C) return 64u * 1024u * 1024u;  // Simulated MEM2
    if (paddr == 0x00003124) return 0x90000000u;          // Usable MEM2 Start
    if (paddr == 0x00003128) return 0x93E00000u;          // Usable MEM2 End
    // IOS IPC arena
    if (paddr == 0x00003130) return 0x93E00000u;          // IOS ArenaLo
    if (paddr == 0x00003134) return 0x94000000u;          // IOS ArenaHi

    // Route hardware registers (GC: 0xCC, Wii IOS IPC: 0xCD)
    if ((addr & 0xFF000000) == 0xCC000000 || (addr & 0xFF000000) == 0xCD000000)
      return HW_Reg_Read32(addr);

    uint8_t *ptr = get_ptr(addr);
    if (!ptr)
      return 0;

    return ((uint32_t)ptr[0] << 24) | ((uint32_t)ptr[1] << 16) |
           ((uint32_t)ptr[2] << 8)  | (uint32_t)ptr[3];
  }

  void write32(uint32_t addr, uint32_t value) {
    uint32_t paddr = addr & 0x3FFFFFFF;

    // Prevent OSInit from overwriting our hardcoded globals
    if (paddr == 0x00000024 || paddr == 0x00000028 ||
        paddr == 0x00003118 || paddr == 0x0000311C || paddr == 0x00003124 ||
        paddr == 0x00003128 || paddr == 0x00003130 || paddr == 0x00003134) {
      return; 
    }

    // HW IOS IPC hack
    // Without HLE symbols, we have to fake the response right at the hardware level.
    if (paddr == 0x0D000000) { // 0xCD000000 (HW_IPC_REQ)
      uint32_t req_addr = value | 0x80000000; // Convert physical buffer address to virtual
      
      // Read the command the game is sending
      uint32_t cmd = read32(req_addr); 
      std::cout << "[HW_IPC] Received IOS request! Command: " << cmd << " at 0x" << std::hex << req_addr << std::dec << "\n";
      
      // Fake an immediate success by writing 1 to the result field
      write32(req_addr + 4, 1);
      
      // Acknowledge the request to the hardware
      write32(0xCD000004, value);
      return;
    }

    if (addr == 0xCC008000) {
      GX_WGPIPE_Write32(value);
      return;
    }
    // Route hardware registers (GC: 0xCC, Wii IOS IPC: 0xCD)
    if ((addr & 0xFF000000) == 0xCC000000 || (addr & 0xFF000000) == 0xCD000000) {
      HW_Reg_Write32(addr, value);
      return;
    }

    uint8_t *ptr = get_ptr(addr);
    if (!ptr)
      return;
    ptr[0] = (value >> 24) & 0xFF;
    ptr[1] = (value >> 16) & 0xFF;
    ptr[2] = (value >> 8)  & 0xFF;
    ptr[3] = value & 0xFF;
  }

  float read_f32(uint32_t addr) {
    uint32_t val = read32(addr);
    float f;
    std::memcpy(&f, &val, 4);
    return f;
  }

  void write_f32(uint32_t addr, float value) {
    if (addr == 0xCC008000) {
      GX_WGPIPE_WriteF32(value);
      return;
    }
    uint32_t val;
    std::memcpy(&val, &value, 4);
    write32(addr, val);
  }

  uint64_t read64(uint32_t addr) {
    uint8_t *ptr = get_ptr(addr);
    if (!ptr)
      return 0;
    return ((uint64_t)ptr[0] << 56) | ((uint64_t)ptr[1] << 48) |
           ((uint64_t)ptr[2] << 40) | ((uint64_t)ptr[3] << 32) |
           ((uint64_t)ptr[4] << 24) | ((uint64_t)ptr[5] << 16) |
           ((uint64_t)ptr[6] << 8) | (uint64_t)ptr[7];
  }

  void write64(uint32_t addr, uint64_t value) {
    uint8_t *ptr = get_ptr(addr);
    if (!ptr)
      return;
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

  // Floating Point Registers (f0-f31, which act as ps0 for Paired Singles)
  std::array<double, 32> fpr;

  // Paired Single 1 Registers (ps1) - second half of the 64-bit paired single
  std::array<double, 32> ps1;

  // Condition Registers (cr0-cr7)
  std::array<ConditionField, 8> cr;

  // Special Purpose Registers
  uint32_t pc;    // Program Counter
  uint32_t lr;    // Link Register
  uint32_t ctr;   // Count Register
  uint32_t xer;   // Fixed-Point Exception Register
  uint32_t msr;   // Machine State Register
  uint32_t fpscr; // Floating-Point Status and Control Register
  uint32_t srr0;  // Save/Restore Register 0
  uint32_t srr1;  // Save/Restore Register 1

  // Graphics Quantization Registers (GQR0-GQR7) for Paired Singles Load/Store
  std::array<uint32_t, 8> gqr;

  // Memory Management Unit
  MMU mmu;

  // Exception handling for control flow
  uint32_t exception_pc;
  jmp_buf jump_env;

  // Default constructor to zero init
  CPUContext()
      : gpr{0}, fpr{0.0}, ps1{0.0}, cr{}, pc(0), lr(0), ctr(0), xer(0), msr(0),
        fpscr(0), srr0(0), srr1(0), gqr{0}, exception_pc(0) {}
  uint64_t inst_count = 0;

  // Paired Single Quantized Load
  void psq_load(uint32_t frD, uint32_t addr, uint32_t W, uint32_t I) {
    uint32_t gqr_val = gqr[I];
    uint32_t ld_type = (gqr_val >> 16) & 0x7;
    uint32_t ld_scale = (gqr_val >> 24) & 0x3F;
    float scale = std::pow(2.0f, -((int)ld_scale));

    auto load_element = [&](uint32_t a) -> double {
      switch (ld_type) {
      case 0:
        return mmu.read_f32(a);
      case 4:
        return mmu.read8(a) * scale;
      case 5:
        return mmu.read16(a) * scale;
      case 6:
        return (int8_t)mmu.read8(a) * scale;
      case 7:
        return (int16_t)mmu.read16(a) * scale;
      default:
        return 0.0;
      }
    };

    uint32_t elem_size =
        (ld_type == 0) ? 4 : ((ld_type == 4 || ld_type == 6) ? 1 : 2);

    fpr[frD] = load_element(addr);
    if (W == 0) { // W=0 means 2 elements
      ps1[frD] = load_element(addr + elem_size);
    } else { // W=1 means 1 element, ps1 gets 1.0
      ps1[frD] = 1.0;
    }
  }

  // Paired Single Quantized Store
  void psq_store(uint32_t frS, uint32_t addr, uint32_t W, uint32_t I) {
    uint32_t gqr_val = gqr[I];
    uint32_t st_type = gqr_val & 0x7;
    uint32_t st_scale = (gqr_val >> 8) & 0x3F;
    float scale = std::pow(2.0f, (int)st_scale);

    auto store_element = [&](uint32_t a, double val) {
      switch (st_type) {
      case 0:
        mmu.write_f32(a, (float)val);
        break;
      case 4:
        mmu.write8(a, (uint8_t)(val * scale));
        break;
      case 5:
        mmu.write16(a, (uint16_t)(val * scale));
        break;
      case 6:
        mmu.write8(a, (int8_t)(val * scale));
        break;
      case 7:
        mmu.write16(a, (int16_t)(val * scale));
        break;
      }
    };

    uint32_t elem_size =
        (st_type == 0) ? 4 : ((st_type == 4 || st_type == 6) ? 1 : 2);

    store_element(addr, fpr[frS]);
    store_element(addr, fpr[frS]);
    if (W == 0) {
      store_element(addr + elem_size, ps1[frS]);
    }
  }

  // SIMD Paired Singles Math
  inline void ps_add(uint32_t D, uint32_t A, uint32_t B) {
    fpr[D] = fpr[A] + fpr[B];
    ps1[D] = ps1[A] + ps1[B];
  }
  inline void ps_sub(uint32_t D, uint32_t A, uint32_t B) {
    fpr[D] = fpr[A] - fpr[B];
    ps1[D] = ps1[A] - ps1[B];
  }
  inline void ps_mul(uint32_t D, uint32_t A, uint32_t C) {
    fpr[D] = fpr[A] * fpr[C];
    ps1[D] = ps1[A] * ps1[C];
  }
  inline void ps_madd(uint32_t D, uint32_t A, uint32_t C, uint32_t B) {
    fpr[D] = std::fma(fpr[A], fpr[C], fpr[B]);
    ps1[D] = std::fma(ps1[A], ps1[C], ps1[B]);
  }
  inline void ps_msub(uint32_t D, uint32_t A, uint32_t C, uint32_t B) {
    fpr[D] = std::fma(fpr[A], fpr[C], -fpr[B]);
    ps1[D] = std::fma(ps1[A], ps1[C], -ps1[B]);
  }
  inline void ps_nmadd(uint32_t D, uint32_t A, uint32_t C, uint32_t B) {
    fpr[D] = -std::fma(fpr[A], fpr[C], fpr[B]);
    ps1[D] = -std::fma(ps1[A], ps1[C], ps1[B]);
  }
  inline void ps_nmsub(uint32_t D, uint32_t A, uint32_t C, uint32_t B) {
    fpr[D] = -std::fma(fpr[A], fpr[C], -fpr[B]);
    ps1[D] = -std::fma(ps1[A], ps1[C], -ps1[B]);
  }
  inline void ps_div(uint32_t D, uint32_t A, uint32_t B) {
    fpr[D] = fpr[A] / fpr[B];
    ps1[D] = ps1[A] / ps1[B];
  }
  inline void ps_sum0(uint32_t D, uint32_t A, uint32_t C, uint32_t B) {
    fpr[D] = fpr[A] + fpr[B];
    ps1[D] = ps1[C] + ps1[B];
  }
  inline void ps_sum1(uint32_t D, uint32_t A, uint32_t C, uint32_t B) {
    fpr[D] = fpr[A] + ps1[B];
    ps1[D] = ps1[C] + ps1[B];
  }
  inline void ps_muls0(uint32_t D, uint32_t A, uint32_t C) {
    fpr[D] = fpr[A] * fpr[C];
    ps1[D] = ps1[A] * fpr[C];
  }
  inline void ps_muls1(uint32_t D, uint32_t A, uint32_t C) {
    fpr[D] = fpr[A] * ps1[C];
    ps1[D] = ps1[A] * ps1[C];
  }
  inline void ps_madds0(uint32_t D, uint32_t A, uint32_t C, uint32_t B) {
    fpr[D] = std::fma(fpr[A], fpr[C], fpr[B]);
    ps1[D] = std::fma(ps1[A], fpr[C], ps1[B]);
  }
  inline void ps_madds1(uint32_t D, uint32_t A, uint32_t C, uint32_t B) {
    fpr[D] = std::fma(fpr[A], ps1[C], fpr[B]);
    ps1[D] = std::fma(ps1[A], ps1[C], ps1[B]);
  }
  inline void ps_sel(uint32_t D, uint32_t A, uint32_t C, uint32_t B) {
    fpr[D] = (fpr[A] >= 0.0) ? fpr[C] : fpr[B];
    ps1[D] = (ps1[A] >= 0.0) ? ps1[C] : ps1[B];
  }
  inline void ps_mr(uint32_t D, uint32_t B) {
    fpr[D] = fpr[B];
    ps1[D] = ps1[B];
  }
  inline void ps_neg(uint32_t D, uint32_t B) {
    fpr[D] = -fpr[B];
    ps1[D] = -ps1[B];
  }
  inline void ps_abs(uint32_t D, uint32_t B) {
    fpr[D] = std::abs(fpr[B]);
    ps1[D] = std::abs(ps1[B]);
  }
  inline void ps_nabs(uint32_t D, uint32_t B) {
    fpr[D] = -std::abs(fpr[B]);
    ps1[D] = -std::abs(ps1[B]);
  }
  inline void ps_merge00(uint32_t D, uint32_t A, uint32_t B) {
    fpr[D] = fpr[A];
    ps1[D] = fpr[B];
  }
  inline void ps_merge01(uint32_t D, uint32_t A, uint32_t B) {
    fpr[D] = fpr[A];
    ps1[D] = ps1[B];
  }
  inline void ps_merge10(uint32_t D, uint32_t A, uint32_t B) {
    fpr[D] = ps1[A];
    ps1[D] = fpr[B];
  }
  inline void ps_merge11(uint32_t D, uint32_t A, uint32_t B) {
    fpr[D] = ps1[A];
    ps1[D] = ps1[B];
  }
  inline void ps_cmpu0(uint32_t crD, uint32_t A, uint32_t B) {
    cr[crD].lt = fpr[A] < fpr[B];
    cr[crD].gt = fpr[A] > fpr[B];
    cr[crD].eq = fpr[A] == fpr[B];
  }
  inline void ps_cmpo0(uint32_t crD, uint32_t A, uint32_t B) {
    cr[crD].lt = fpr[A] < fpr[B];
    cr[crD].gt = fpr[A] > fpr[B];
    cr[crD].eq = fpr[A] == fpr[B];
  }
  inline void ps_cmpu1(uint32_t crD, uint32_t A, uint32_t B) {
    cr[crD].lt = ps1[A] < ps1[B];
    cr[crD].gt = ps1[A] > ps1[B];
    cr[crD].eq = ps1[A] == ps1[B];
  }
  inline void ps_cmpo1(uint32_t crD, uint32_t A, uint32_t B) {
    cr[crD].lt = ps1[A] < ps1[B];
    cr[crD].gt = ps1[A] > ps1[B];
    cr[crD].eq = ps1[A] == ps1[B];
  }
};

} // namespace runtime
} // namespace nwii
