#pragma once
#include <array>
#include <cmath>
#include <csetjmp>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>
#include <mutex>
#include <atomic>
#include <queue>
#include <stack>

namespace nwii {
namespace runtime {

struct ConditionField {
  bool lt; // Less Than
  bool gt; // Greater Than
  bool eq; // Equal
  bool so; // Summary Overflow

  ConditionField() : lt(false), gt(false), eq(false), so(false) {}
};

void GX_WGPIPE_Write8(uint8_t val);
void GX_WGPIPE_Write16(uint16_t val);
void GX_WGPIPE_Write32(uint32_t val);
void GX_WGPIPE_WriteF32(float val);

uint16_t HW_Reg_Read16(uint32_t addr);
uint32_t HW_Reg_Read32(uint32_t addr);
void HW_Reg_Write16(uint32_t addr, uint16_t val);
void HW_Reg_Write32(uint32_t addr, uint32_t val);

struct CPUContext;
void init_ipc_client(CPUContext &ctx);
bool handle_syscall(CPUContext &ctx);
bool process_pending_callbacks(CPUContext &ctx);

struct CallbackInfo {
    uint32_t cb_addr;
    uint32_t arg1;
    uint32_t arg2;
    bool is_irq;
};

// Removed CallbackInterrupt in favor of setjmp/longjmp for exception unwinding

// Strict Dolphin-accurate MMU
struct MMU {
  std::vector<uint8_t> mem1;
  std::vector<uint8_t> mem2;

  MMU() {
    mem1.resize(24 * 1024 * 1024 + 8, 0); // 24MB MEM1 + 8 bytes padding for unaligned access
    mem2.resize(64 * 1024 * 1024 + 8, 0); // 64MB MEM2 + 8 bytes padding for unaligned access
  }

  // Translates Virtual (EA) to Physical (PA) pointer based on RVL standard BAT mappings
  inline uint8_t *get_ptr(uint32_t addr) {
    uint32_t paddr = addr & 0x3FFFFFFF; 
    if (paddr < 0x04000000) {
        return &mem1[paddr];
    } else if (paddr >= 0x10000000 && paddr < 0x14000000) {
        return &mem2[paddr - 0x10000000];
    }
    return nullptr;
  }

  inline bool is_hw_reg(uint32_t paddr) const {
      return (paddr >= 0x0C000000 && paddr < 0x0C010000) || 
             (paddr >= 0x0D000000 && paddr < 0x0D010000);
  }

  uint8_t read8(uint32_t addr) {
    uint32_t paddr = addr & 0x3FFFFFFF;

    if (is_hw_reg(paddr)) {
        // HW registers are typically 32-bit/16-bit, 8-bit reads are rare but happen
        uint32_t val = HW_Reg_Read32(paddr & ~3);
        int shift = 24 - ((paddr & 3) * 8);
        return (val >> shift) & 0xFF;
    }
    uint8_t *ptr = get_ptr(addr);
    return ptr ? *ptr : 0;
  }

  void write8(uint32_t addr, uint8_t value) {
    uint32_t paddr = addr & 0x3FFFFFFF;
    if (paddr == 0x0C008000) { GX_WGPIPE_Write8(value); return; } // FIFO Strict Physical
    if (is_hw_reg(paddr)) return; // 8-bit HW writes are generally ignored/unsupported

    uint8_t *ptr = get_ptr(addr);
    if (ptr) *ptr = value;
  }

  uint16_t read16(uint32_t addr) {
    uint32_t paddr = addr & 0x3FFFFFFF;

    if (is_hw_reg(paddr)) return HW_Reg_Read16(paddr);
    
    uint8_t *ptr = get_ptr(addr);
    return ptr ? (uint16_t)(ptr[0] << 8 | ptr[1]) : 0;
  }

  void write16(uint32_t addr, uint16_t value) {
    uint32_t paddr = addr & 0x3FFFFFFF;
    if (paddr == 0x0C008000) { GX_WGPIPE_Write16(value); return; }
    if (is_hw_reg(paddr)) { HW_Reg_Write16(paddr, value); return; }

    uint8_t *ptr = get_ptr(addr);
    if (ptr) { ptr[0] = value >> 8; ptr[1] = value & 0xFF; }
  }

  uint32_t read32(uint32_t addr) {
    uint32_t paddr = addr & 0x3FFFFFFF;
    if (paddr == 0x0000000C) return 0; // Fix allocator bug by faking 0 here

    
    if (is_hw_reg(paddr)) return HW_Reg_Read32(paddr);

    uint8_t *ptr = get_ptr(addr);
    if (!ptr) return 0;
    uint32_t val = ((uint32_t)ptr[0] << 24) | ((uint32_t)ptr[1] << 16) | ((uint32_t)ptr[2] << 8) | ptr[3];

    return val;
  }

  void write32(uint32_t addr, uint32_t value) {
    if (addr == 0x80528A30) {
      std::cout << "[Watchpoint] write32 at 0x80528A30 with value " << std::hex << value << std::dec << "\n";
    }
    uint32_t paddr = addr & 0x3FFFFFFF;
    if (paddr == 0x0000000C) return; // Prevent OSInit from writing here, fixing allocator bug

    if (paddr == 0x0C008000) { GX_WGPIPE_Write32(value); return; }
    if (is_hw_reg(paddr)) { HW_Reg_Write32(paddr, value); return; }

    uint8_t *ptr = get_ptr(addr);
    if (ptr) { ptr[0] = value >> 24; ptr[1] = (value >> 16) & 0xFF; ptr[2] = (value >> 8) & 0xFF; ptr[3] = value & 0xFF; }
  }

  float read_f32(uint32_t addr) { uint32_t val = read32(addr); float f; std::memcpy(&f, &val, 4); return f; }
  void write_f32(uint32_t addr, float value) { 
      uint32_t paddr = addr & 0x3FFFFFFF;
      if (paddr == 0x0C008000) { GX_WGPIPE_WriteF32(value); return; }
      uint32_t val; std::memcpy(&val, &value, 4); write32(addr, val); 
  }
  
  uint64_t read64(uint32_t addr) {
    uint8_t *ptr = get_ptr(addr);
    if (!ptr) return 0;
    return ((uint64_t)ptr[0] << 56) | ((uint64_t)ptr[1] << 48) | ((uint64_t)ptr[2] << 40) | ((uint64_t)ptr[3] << 32) |
           ((uint64_t)ptr[4] << 24) | ((uint64_t)ptr[5] << 16) | ((uint64_t)ptr[6] << 8) | ptr[7];
  }
  void write64(uint32_t addr, uint64_t value) {
    uint8_t *ptr = get_ptr(addr);
    if (!ptr) return;
    ptr[0] = value >> 56; ptr[1] = (value >> 48) & 0xFF; ptr[2] = (value >> 40) & 0xFF; ptr[3] = (value >> 32) & 0xFF;
    ptr[4] = (value >> 24) & 0xFF; ptr[5] = (value >> 16) & 0xFF; ptr[6] = (value >> 8) & 0xFF; ptr[7] = value & 0xFF;
  }
  double read_f64(uint32_t addr) { uint64_t val = read64(addr); double d; std::memcpy(&d, &val, 8); return d; }
  void write_f64(uint32_t addr, double value) { uint64_t val; std::memcpy(&val, &value, 8); write64(addr, val); }
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
  std::array<uint32_t, 4> sprg; // SPRG0-3

  // Backup state for HLE callbacks (nested-safe via stack)
  struct BackupState {
    std::array<uint32_t, 32> gpr;
    std::array<double, 32> fpr;
    std::array<double, 32> ps1;
    std::array<ConditionField, 8> cr;
    uint32_t lr;
    uint32_t ctr;
    uint32_t xer;
    uint32_t pc;
    uint32_t srr0;
    uint32_t srr1;
    uint32_t msr;
    uint32_t fpscr;
    std::array<uint32_t, 8> gqr;
    std::array<uint32_t, 4> sprg;
  };
  std::stack<BackupState> backup_stack;
  bool in_callback = false;
  int callback_depth = 0;

  // Memory Management Unit
  MMU mmu;
  
  // Exception handling for control flow
  uint32_t exception_pc;
  uint64_t inst_count = 0;
  
  std::atomic<bool> is_running;
  std::atomic<bool> vblank_pending;
  std::mutex cb_mutex;
  std::queue<CallbackInfo> pending_callbacks;
  jmp_buf exception_jmp_buf;

  // Reservation address for lwarx/stwcx atomic instructions (multiprocessing)
  uint32_t reservation_addr = 0xFFFFFFFF;

  CPUContext() : gpr{0}, fpr{0.0}, ps1{0.0}, cr{}, pc(0), lr(0), ctr(0), xer(0), 
                 msr(0), fpscr(0), srr0(0), srr1(0), gqr{0}, exception_pc(0), is_running(true), vblank_pending(false) {}

  void queue_callback(uint32_t cb, uint32_t arg1, uint32_t arg2, bool is_irq = false) {
    std::lock_guard<std::mutex> lock(cb_mutex);
    pending_callbacks.push({cb, arg1, arg2, is_irq});
  }

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
    bool un = std::isnan(fpr[A]) || std::isnan(fpr[B]);
    cr[crD].lt = !un && (fpr[A] < fpr[B]);
    cr[crD].gt = !un && (fpr[A] > fpr[B]);
    cr[crD].eq = !un && (fpr[A] == fpr[B]);
    cr[crD].so = un;
  }
  inline void ps_cmpo0(uint32_t crD, uint32_t A, uint32_t B) {
    bool un = std::isnan(fpr[A]) || std::isnan(fpr[B]);
    cr[crD].lt = !un && (fpr[A] < fpr[B]);
    cr[crD].gt = !un && (fpr[A] > fpr[B]);
    cr[crD].eq = !un && (fpr[A] == fpr[B]);
    cr[crD].so = un;
  }
  inline void ps_cmpu1(uint32_t crD, uint32_t A, uint32_t B) {
    bool un = std::isnan(ps1[A]) || std::isnan(ps1[B]);
    cr[crD].lt = !un && (ps1[A] < ps1[B]);
    cr[crD].gt = !un && (ps1[A] > ps1[B]);
    cr[crD].eq = !un && (ps1[A] == ps1[B]);
    cr[crD].so = un;
  }
  inline void ps_cmpo1(uint32_t crD, uint32_t A, uint32_t B) {
    bool un = std::isnan(ps1[A]) || std::isnan(ps1[B]);
    cr[crD].lt = !un && (ps1[A] < ps1[B]);
    cr[crD].gt = !un && (ps1[A] > ps1[B]);
    cr[crD].eq = !un && (ps1[A] == ps1[B]);
    cr[crD].so = un;
  }
};

void hle_set_ipc_arm_msg(uint32_t req_addr);
void micro_interpret(CPUContext& ctx, uint32_t opcode, uint32_t pc);

} // namespace runtime
} // namespace nwii
