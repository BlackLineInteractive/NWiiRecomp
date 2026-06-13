#include "runtime/cpu_context.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <iostream>
#include <string>

using namespace nwii::runtime;

static uint64_t get_os_time() {
  auto now = std::chrono::steady_clock::now();
  return std::chrono::duration_cast<std::chrono::microseconds>(
             now.time_since_epoch())
      .count();
}

// Read a null-terminated string from the guest MMU memory
static std::string read_guest_string(CPUContext &ctx, uint32_t addr) {
  std::string str;
  while (true) {
    uint8_t c = ctx.mmu.read8(addr++);
    if (c == 0)
      break;
    str += (char)c;
  }
  return str;
}

// OSReport is the standard NN SDK print function.
// Signature: void OSReport(const char* msg, ...);
// The format string address is passed in r3 (gpr[3]).
extern "C" void OSReport(CPUContext &ctx) {
  uint32_t format_addr = ctx.gpr[3];
  std::string format_str = read_guest_string(ctx, format_addr);

  std::string result;
  int arg_idx = 4; // PowerPC arguments start at r4
  int fpr_idx = 1; // PowerPC float arguments start at f1

  for (size_t i = 0; i < format_str.length(); ++i) {
    if (format_str[i] == '%') {
      if (i + 1 < format_str.length() && format_str[i + 1] == '%') {
        result += '%';
        i++;
        continue;
      }

      std::string specifier = "%";
      i++;
      // Collect all flags, width, and modifiers (e.g. 08, l, h)
      while (i < format_str.length() &&
             std::string("0123456789.-+ #hlz").find(format_str[i]) !=
                 std::string::npos) {
        specifier += format_str[i];
        i++;
      }

      if (i < format_str.length()) {
        char type = format_str[i];
        specifier += type;

        if (type == 'd' || type == 'i' || type == 'c') {
          char buf[64];
          std::snprintf(buf, sizeof(buf), specifier.c_str(),
                        (int32_t)ctx.gpr[arg_idx++]);
          result += buf;
        } else if (type == 'u' || type == 'x' || type == 'X' || type == 'p') {
          char buf[64];
          std::snprintf(buf, sizeof(buf), specifier.c_str(),
                        (uint32_t)ctx.gpr[arg_idx++]);
          result += buf;
        } else if (type == 's') {
          uint32_t str_ptr = ctx.gpr[arg_idx++];
          if (str_ptr != 0) {
            result += read_guest_string(ctx, str_ptr);
          } else {
            result += "(null)";
          }
        } else if (type == 'f') {
          char buf[64];
          if (fpr_idx <= 8) {
            std::snprintf(buf, sizeof(buf), specifier.c_str(),
                          ctx.fpr[fpr_idx++]);
          } else {
            // If more than 8 floats, they are passed on the stack or something,
            // but usually there aren't that many.
            std::snprintf(buf, sizeof(buf), "[float_spilled]");
          }
          result += buf;
        } else {
          result += specifier;
        }
      }
    } else {
      result += format_str[i];
    }
  }
  std::cout << "[OSReport] " << result;
}

// Basic stubs for other OS functions to prevent linker errors
extern "C" void OSInit(CPUContext &ctx) {
  std::cout << "[OSInit] System initialized." << std::endl;
}

namespace nwii {
namespace runtime {
extern MMU *g_mmu;
}
} // namespace nwii

static uint32_t vi_vblank_counter = 0;

static uint16_t dsp_mbox_cpu_hi = 0;
static uint16_t dsp_mbox_cpu_lo = 0;
static uint16_t dsp_mbox_dsp_hi = 0;
static uint16_t dsp_mbox_dsp_lo = 0;

static uint32_t ipc_arm_msg = 0;    // HW_IPC_ARMMSG  (0xCD000008)
static uint32_t ipc_arm_ctrl = 0;   // HW_IPC_ARMCTRL (0xCD00000C)
static uint32_t ipc_ppc_ctrl = 0;   // HW_IPC_PPCCTRL (0xCD000004)
static uint32_t pi_intsr = 0;       // PI_INTSR (0xCC003000)
static uint32_t di_sr = 0x00000008; // DI_SR (0xCC006000)
static uint32_t vi_dcr = 0;         // VI_DCR (0xCC002030)

struct EXIChannel {
  uint32_t status = 0;
  uint32_t dma_addr = 0;
  uint32_t dma_len = 0;
  uint32_t cr = 0;
  uint32_t data = 0;
};
static EXIChannel exi_chan[3];

struct SIChannel {
  uint32_t out = 0;
  uint32_t in_hi = 0;
  uint32_t in_lo = 0;
};
static SIChannel si_chan[4];
static uint32_t si_poll = 0;
static uint32_t si_com_csr = 0;
static uint32_t si_status = 0;
static uint32_t si_exi_clock = 0;

static uint32_t ai_cr = 0;
static uint32_t ai_vr = 0;
static uint32_t ai_scnt = 0;
static uint32_t ai_it = 0;
static uint64_t ai_start_time = 0;

static uint32_t ar_mmaddr = 0;
static uint32_t ar_araddr = 0;
static uint32_t ar_cnt = 0;
static uint16_t dsp_control = 0x0800; // Hardware boots with DSP HALTED

static uint32_t di_cmd[3] = {0, 0, 0};
static uint32_t di_mar = 0;
static uint32_t di_len = 0;
static uint32_t di_cr = 0;
static uint32_t di_imm = 0;
static uint32_t di_cfg = 0;

namespace nwii {
namespace runtime {
extern MMU *g_mmu;

void hle_set_ipc_arm_msg(uint32_t req_addr) {
  ipc_arm_msg = req_addr & 0x1FFFFFFF;
  ipc_arm_ctrl = 0x00000002;  // bit1 = Y2
  ipc_ppc_ctrl |= 0x00000004; // bit2 = X2 (response ready)
  pi_intsr |= 0x00001000; // Trigger PI interrupt for IPC (usually bit 12 or 14)
}
} // namespace runtime
} // namespace nwii

extern "C" void handle_ios_ipc(uint32_t request_addr);
// g_mmu declaration moved outside extern "C"
static void ipc_fake_ack(uint32_t request_addr) {
  // Translate physical address to virtual for MMU access:
  // Physical MEM1: 0x00000000-0x017FFFFF -> Virtual: 0x80000000+
  // Physical MEM2: 0x10000000-0x13FFFFFF -> Virtual: 0x90000000+
  uint32_t virt_addr = request_addr;
  if (virt_addr < 0x01800000) {
    virt_addr |= 0x80000000;
  } else if (virt_addr >= 0x10000000 && virt_addr < 0x14000000) {
    virt_addr = (virt_addr & 0x03FFFFFF) | 0x90000000;
  }

  // Delegate to the real IPC handler in ios.cpp for full command dispatch
  handle_ios_ipc(virt_addr);

  // Set HW IPC registers to signal reply ready
  ipc_arm_msg = request_addr & 0x1FFFFFFF;
  ipc_arm_ctrl = 0x00000003; // bit0 = Y1 (reply ready), bit1 = Y2 (cmd ack)
  pi_intsr |= 0x00004000;   // INT_CAUSE_WII_IPC
}

namespace nwii {
namespace runtime {
uint16_t HW_Reg_Read16(uint32_t addr) { return (uint16_t)HW_Reg_Read32(addr); }

uint32_t HW_Reg_Read32(uint32_t addr) {
  // --- IOS IPC registers (Wii, 0xCD000000–0xCD00FFFF) ---
  if ((addr & 0xFF000000) == 0xCD000000) {
    switch (addr & 0x00FFFFFF) {
    case 0x000000:
      return 0; // HW_IPC_PPCMSG
    case 0x000004:
      return ipc_ppc_ctrl; // HW_IPC_PPCCTRL
    case 0x000008:
      return ipc_arm_msg; // HW_IPC_ARMMSG
    case 0x00000C:
      return ipc_arm_ctrl; // HW_IPC_ARMCTRL
    default:
      return 0;
    }
  }

  // --- Hardware registers GC (0xCC000000) ---
  addr = (addr & 0x00FFFFFF) | 0xCC000000;

  // PI (Processor Interface) - interrupts
  if (addr >= 0xCC003000 && addr <= 0xCC0030FF) {
    if (addr == 0xCC003000) {
      // Dynamic interrupt checks
      if (ai_cr & 0x20) { // PSTAT (Audio Playing)
        uint64_t current = get_os_time();
        uint32_t samples = ((current - ai_start_time) * 48) / 1000;
        uint32_t cur_scnt = ai_scnt + samples;
        // Check if sample counter reached interrupt timing
        if (ai_it > 0 && cur_scnt >= ai_it) {
          ai_cr |= 0x04; // Set AIINT
          if (ai_cr & 0x02)
            pi_intsr |= 0x20; // Trigger PI AI interrupt if unmasked
        }
      }
      return pi_intsr;
    }
    if (addr == 0xCC003004)
      return 0; // PI Interrupt Cause/Mask = 0 (no pending interrupts)
    return 0;   // PI Interrupt Cause/Mask = 0 (no pending interrupts)
  }

  // VI (Video Interface)
  if (addr >= 0xCC002000 && addr <= 0xCC0020FF) {
    if (addr == 0xCC00202C)
      return vi_vblank_counter++;
    if (addr == 0xCC002030) {
      // Simulate VCT matching to trigger interrupt
      uint32_t current_line = (vi_vblank_counter % 262);
      uint32_t target_vct = (vi_dcr >> 16) & 0x3FF;
      if (current_line == target_vct) {
        vi_dcr |= 0x80000000;   // Set INT bit
        pi_intsr |= 0x00000100; // Trigger PI_INTSR bit 8
      }
      return vi_dcr;
    }
    return 0;
  }

  // AI/DSP mailbox & ARAM DMA
  if (addr >= 0xCC005000 && addr <= 0xCC0050FF) {
    if (addr == 0xCC005000 || addr == 0xCC005002)
      return dsp_mbox_cpu_hi;
    if (addr == 0xCC005004)
      return dsp_mbox_dsp_hi;
    if (addr == 0xCC005006) {
      uint16_t val = dsp_mbox_dsp_lo;
      dsp_mbox_dsp_hi &= ~0x8000;
      return val;
    }
    if (addr == 0xCC005008 || addr == 0xCC00500A)
      return dsp_control;
    if (addr == 0xCC005020)
      return ar_mmaddr;
    if (addr == 0xCC005024)
      return ar_araddr;
    if (addr == 0xCC005028)
      return ar_cnt;
    return 0;
  }

  // EXI (Expansion Interface)
  if (addr >= 0xCC006800 && addr <= 0xCC00682C) {
    int ch = (addr - 0xCC006800) / 0x14;
    int reg = (addr - 0xCC006800) % 0x14;
    if (ch >= 0 && ch < 3) {
      if (reg == 0x00)
        return exi_chan[ch].status | 0x1000; // Bit 12 = Device connected
      if (reg == 0x04)
        return exi_chan[ch].dma_addr;
      if (reg == 0x08)
        return exi_chan[ch].dma_len;
      if (reg == 0x0C)
        return exi_chan[ch].cr;
      if (reg == 0x10)
        return exi_chan[ch].data;
    }
    return 0;
  }

  // SI (Serial Interface)
  if (addr >= 0xCC006400 && addr <= 0xCC0064FF) {
    if (addr >= 0xCC006400 && addr <= 0xCC00642C) {
      int ch = (addr - 0xCC006400) / 0x0C;
      int reg = (addr - 0xCC006400) % 0x0C;
      if (ch >= 0 && ch < 4) {
        if (reg == 0x00)
          return si_chan[ch].out;
        if (reg == 0x04) {
          // Return dummy controller data for ch0: 0x08000000 (standard
          // controller ID)
          if (ch == 0)
            return 0x08000000 | si_chan[ch].in_hi;
          return si_chan[ch].in_hi | 0x80000000; // No device error for others
        }
        if (reg == 0x08)
          return si_chan[ch].in_lo;
      }
    }
    if (addr == 0xCC006430)
      return si_poll;
    if (addr == 0xCC006434)
      return si_com_csr;
    if (addr == 0xCC006438)
      return si_status;
    if (addr == 0xCC00643C)
      return si_exi_clock;
    return 0;
  }

  // DI (DVD Interface)
  if (addr >= 0xCC006000 && addr <= 0xCC0060FF) {
    if (addr == 0xCC006000)
      return di_sr;
    if (addr == 0xCC006004)
      return 1; // DI_COVER: cover closed, disc present
    if (addr == 0xCC006008)
      return di_cmd[0];
    if (addr == 0xCC00600C)
      return di_cmd[1];
    if (addr == 0xCC006010)
      return di_cmd[2];
    if (addr == 0xCC006014)
      return di_mar;
    if (addr == 0xCC006018)
      return di_len;
    if (addr == 0xCC00601C)
      return di_cr;
    if (addr == 0xCC006020)
      return di_imm;
    if (addr == 0xCC006024)
      return di_cfg;
    return 0;
  }

  return 0;
}

void HW_Reg_Write16(uint32_t addr, uint16_t val) { HW_Reg_Write32(addr, val); }

void HW_Reg_Write32(uint32_t addr, uint32_t val) {
  // --- IOS IPC registers (Wii, 0xCD000000) ---
  // When the game writes to HW_IPC_PPCMSG, it means it sent an IPC request to
  // IOS. We immediately synthesize an instant reply to release the spin-lock.
  if ((addr & 0xFF000000) == 0xCD000000) {
    switch (addr & 0x00FFFFFF) {
    case 0x000000: {
      // HW_IPC_PPCMSG: game sends IPC request
      // val — physical address of IPCRequest buffer (without 0x80 prefix)
      std::cout << "[HW IPC] Game sent IPC request to IOS. Phys addr=0x"
                << std::hex << val << std::dec << "\n";
      ipc_fake_ack(val);
      break;
    }
    case 0x000004:
      // HW_IPC_PPCCTRL: game clears bits or sets them
      // In a real Wii, writing 1 to X1/Y1 clears them. We'll simplify and just
      // clear X2 if requested.
      if (val & 0x00000008)
        ipc_ppc_ctrl &= ~0x00000004; // Clear X2 on Ack?
      break;
    case 0x00000C:
      // HW_IPC_ARMCTRL: game writes 1 to clear Y1, 2 to clear Y2
      ipc_arm_ctrl &= ~val;
      if (!(ipc_arm_ctrl & 3))
        pi_intsr &= ~0x00004000;
      break;
    default:
      break;
    }
    return;
  }

  // --- GC hardware registers (0xCC000000) ---
  addr = (addr & 0x00FFFFFF) | 0xCC000000;

  if (addr >= 0xCC003000 && addr <= 0xCC0030FF) {
    if (addr == 0xCC003000) {
      // Writing to PI_INTSR clears bits
      pi_intsr &= ~val;
    }
    return;
  }

  if (addr >= 0xCC002000 && addr <= 0xCC0020FF) {
    if (addr == 0xCC002030) {
      // Writing 0 to bit 31 clears the interrupt
      if ((val & 0x80000000) == 0) {
        vi_dcr &= ~0x80000000;
        pi_intsr &= ~0x00000100; // Clear PI_INTSR bit 8
      }
      // Update other bits (VCT, HCT, ENB)
      vi_dcr = (vi_dcr & 0x80000000) | (val & ~0x80000000);
    }
    return;
  }

  // DI (DVD Interface)
  if (addr >= 0xCC006000 && addr <= 0xCC0060FF) {
    if (addr == 0xCC006000) {
      di_sr &= ~val; // Clear interrupts
      if (!(di_sr & 0x0A))
        pi_intsr &= ~0x01; // Clear PI DI int (if TCINT and DEINT are clear)
    } else if (addr == 0xCC006008) {
      di_cmd[0] = val;
    } else if (addr == 0xCC00600C) {
      di_cmd[1] = val;
    } else if (addr == 0xCC006010) {
      di_cmd[2] = val;
    } else if (addr == 0xCC006014) {
      di_mar = val;
    } else if (addr == 0xCC006018) {
      di_len = val;
    } else if (addr == 0xCC00601C) {
      di_cr = val;
      if (val & 1) { // TSTART
        di_cr &= ~1; // Complete instantly
        uint32_t cmd = di_cmd[0] >> 24;
        if (cmd == 0x12) { // Inquiry
          di_imm = 0;
        } else if (cmd == 0xAB) { // Request Error
          di_imm = 0;             // No error
        } else if (cmd == 0xE0) { // Audio Status
          di_imm = 0;
        }
        // Dummy read/seek success
        di_sr |= 0x08; // TCINT (Transfer Complete)
        if (di_sr & 0x04)
          pi_intsr |= 0x01; // Trigger PI DI interrupt if TCINTMASK=1
      }
    }
    return;
  }

  if (addr >= 0xCC005000 && addr <= 0xCC0050FF) {
    if (addr == 0xCC005000 || addr == 0xCC005002) {
      dsp_mbox_cpu_hi = (val & 0x7FFF) | 0x8000;
      dsp_mbox_cpu_hi &= ~0x8000; // DSP immediately accepts the message
      dsp_mbox_dsp_hi = 0x8000;   // DSP replies instantly
    } else if (addr == 0xCC005004 || addr == 0xCC005006) {
      if (val & 0x8000)
        dsp_mbox_dsp_hi &= ~0x8000;
    } else if (addr == 0xCC005008 || addr == 0xCC00500A) {
      uint16_t val16 = val & 0xFFFF;
      if (val16 & 0x0008)
        dsp_control &= ~0x0008; // Clear AID int
      if (val16 & 0x0020)
        dsp_control &= ~0x0020; // Clear ARAM int
      if (val16 & 0x0080)
        dsp_control &= ~0x0080; // Clear DSP int

      // DSPReset (bit 0): self-clearing — write 1 triggers reset, HW clears to
      // 0.
      if (val16 & 0x0001) {
        // When DSP is reset, it sends an init message (0xDCD1) to the CPU
        dsp_mbox_dsp_hi = 0xDCD1;
        dsp_mbox_dsp_lo = 0x0000;
        dsp_control |= 0x0080; // Set DSP interrupt flag
      }

      bool was_halted = (dsp_control & 0x0800) != 0;
      bool is_halted = (val16 & 0x0800) != 0;

      // Simulate by never storing bit 0 (mask it out with ~0x00A9).
      dsp_control =
          (dsp_control & 0x00A8) | (val16 & ~0x00A9); // bit 0 not stored

      // If DSP was un-halted, send the INIT message
      if (was_halted && !is_halted) {
        dsp_mbox_dsp_hi = 0xDCD1;
        dsp_mbox_dsp_lo = 0x0000;
        dsp_control |= 0x0080;
      }

      bool pi_int = false;
      if ((dsp_control & 0x0008) && (dsp_control & 0x0010))
        pi_int = true; // AID
      if ((dsp_control & 0x0020) && (dsp_control & 0x0040))
        pi_int = true; // ARAM
      if ((dsp_control & 0x0080) && (dsp_control & 0x0100))
        pi_int = true; // DSP
      if (pi_int)
        pi_intsr |= 0x40; // Trigger PI DSP interrupt
      else
        pi_intsr &= ~0x40;
    } else if (addr == 0xCC005020) {
      ar_mmaddr = val;
    } else if (addr == 0xCC005024) {
      ar_araddr = val;
    } else if (addr == 0xCC005028) {
      // DMA starts on ANY write to AR_CNT.
      // Bit 31 = direction (0=MRAM→ARAM, 1=ARAM→MRAM). Bits [30:0] = byte
      // count.
      ar_cnt = val;
      {
        bool dir = (val & 0x80000000) != 0;
        uint32_t count = val & 0x7FFFFFFF;
        uint32_t mm = ar_mmaddr & 0x01FFFFFF;
        uint32_t ar_a = ar_araddr & 0x00FFFFFF;
        if (nwii::runtime::g_mmu && count > 0) {
          uint8_t *mem1 = nwii::runtime::g_mmu->mem1.data();
          uint8_t *mem2 = nwii::runtime::g_mmu->mem2.data();
          if (!dir)
            std::memcpy(mem2 + ar_a, mem1 + mm, count); // MRAM->ARAM
          else
            std::memcpy(mem1 + mm, mem2 + ar_a, count); // ARAM->MRAM
        }
        ar_cnt = 0;            // DMA complete instantly
        dsp_control |= 0x0020; // Set ARAM interrupt flag (bit 5)
        if (dsp_control & 0x0040)
          pi_intsr |= 0x40; // Trigger PI if ARAM_mask set
      }
    }
    return;
  }

  // EXI (Expansion Interface)
  if (addr >= 0xCC006800 && addr <= 0xCC00682C) {
    int ch = (addr - 0xCC006800) / 0x14;
    int reg = (addr - 0xCC006800) % 0x14;
    if (ch >= 0 && ch < 3) {
      if (reg == 0x00) {
        if (val & 2)
          exi_chan[ch].status &= ~2; // Clear EXIINT
        if (val & 8)
          exi_chan[ch].status &= ~8; // Clear TCINT
        if (val & 0x800)
          exi_chan[ch].status &= ~0x800; // Clear EXTINT
        exi_chan[ch].status = (exi_chan[ch].status & 0x80A) | (val & ~0x80A);
      }
      if (reg == 0x04)
        exi_chan[ch].dma_addr = val;
      if (reg == 0x08)
        exi_chan[ch].dma_len = val;
      if (reg == 0x0C) {
        exi_chan[ch].cr = val;
        if (val & 1) {              // TSTART
          exi_chan[ch].cr &= ~1;    // Complete instantly
          exi_chan[ch].status |= 8; // Set TCINT
          if (exi_chan[ch].status & 4)
            pi_intsr |= 0x10; // Trigger PI EXI interrupt

          // For reads, just return 0 to bypass cleanly
          if ((val & 2) == 0 && ((val >> 2) & 3) != 1) { // Immediate read
            exi_chan[ch].data = 0;
          }
        }
      }
      if (reg == 0x10)
        exi_chan[ch].data = val;
    }
    return;
  }

  // SI (Serial Interface)
  if (addr >= 0xCC006400 && addr <= 0xCC0064FF) {
    if (addr >= 0xCC006400 && addr <= 0xCC00642C) {
      int ch = (addr - 0xCC006400) / 0x0C;
      int reg = (addr - 0xCC006400) % 0x0C;
      if (ch >= 0 && ch < 4) {
        if (reg == 0x00)
          si_chan[ch].out = val;
        if (reg == 0x04)
          si_chan[ch].in_hi = val;
        if (reg == 0x08)
          si_chan[ch].in_lo = val;
      }
    }
    if (addr == 0xCC006430)
      si_poll = val;
    if (addr == 0xCC006434) {
      si_com_csr = val;
      if (val & 1) {              // TSTART
        si_com_csr &= ~1;         // Complete instantly
        si_com_csr |= 0x80000000; // Set TCINT (bit 31)
        // Also set RDSTINT (bit 28)
        if ((si_com_csr & 0x40000000) || (si_com_csr & 0x08000000))
          pi_intsr |= 0x08; // Trigger PI SI int (bit 3)
      }
    }
    if (addr == 0xCC006438) {
      uint32_t clear_mask = val & 0x0F0F0F0F; // Write-1-to-clear error bits
      si_status &= ~clear_mask;
      if (val & 0x80000000) { // WR (bit 31)
        // Game requesting to poll channels manually
        si_status &= ~0x80000000;
        si_status |= 0x20000000; // RDST0 (bit 29) set - data ready
      }
    }
    if (addr == 0xCC00643C)
      si_exi_clock = val;
    return;
  }

  // AI (Audio Interface)
  if (addr >= 0xCC006C00 && addr <= 0xCC006C1F) {
    if (addr == 0xCC006C00) {
      if (val & 0x04) {
        ai_cr &= ~0x04;    // Write 1 to clear AIINT
        pi_intsr &= ~0x20; // Clear PI AI interrupt
      }
      if (val & 0x08) {
        // SCRESET
        ai_scnt = 0;
        ai_start_time = get_os_time();
      }
      if ((val & 0x20) && !(ai_cr & 0x20)) {
        // PSTAT (Play Status) turned on
        ai_start_time = get_os_time();
      }
      ai_cr = (ai_cr & 0x04) | (val & ~0x04);
    } else if (addr == 0xCC006C04) {
      ai_vr = val;
    } else if (addr == 0xCC006C08) {
      ai_scnt = val;
      ai_start_time = get_os_time();
    } else if (addr == 0xCC006C0C) {
      ai_it = val;
    }
    return;
  }

  // Other GC registers - ignored
}
} // namespace runtime
} // namespace nwii

extern "C" {
// Interrupt Management
void OSDisableInterrupts(CPUContext &ctx) {
  uint32_t old_msr = ctx.msr;
  ctx.msr &= ~(1 << 15); // Clear EE (External Interrupt Enable) bit
  ctx.gpr[3] = old_msr;
}

void OSEnableInterrupts(CPUContext &ctx) {
  uint32_t old_msr = ctx.msr;
  ctx.msr |= (1 << 15); // Set EE bit
  ctx.gpr[3] = old_msr;
}

void OSRestoreInterrupts(CPUContext &ctx) {
  uint32_t prev_state = ctx.gpr[3];
  uint32_t old_msr = ctx.msr;
  if (prev_state & (1 << 15)) {
    ctx.msr |= (1 << 15);
  } else {
    ctx.msr &= ~(1 << 15);
  }
  ctx.gpr[3] = old_msr;
}

// Timer Management
void OSGetTime(CPUContext &ctx) {
  uint64_t t = get_os_time();
  // Return 64-bit time: r3 = upper 32 bits, r4 = lower 32 bits
  ctx.gpr[3] = (uint32_t)(t >> 32);
  ctx.gpr[4] = (uint32_t)(t & 0xFFFFFFFF);
}

void OSTicksToMilliseconds(CPUContext &ctx) {
  uint64_t ticks = ((uint64_t)ctx.gpr[3] << 32) | ctx.gpr[4];
  // In Gekko, timebase ticks at 1/4 of bus speed (Bus = 162 MHz -> TB = 40.5
  // MHz) 40,500 ticks per millisecond
  uint64_t ms = ticks / 40500;
  ctx.gpr[3] = (uint32_t)(ms >> 32);
  ctx.gpr[4] = (uint32_t)(ms & 0xFFFFFFFF);
}

// --- MEMORY ARENA MANAGEMENT ---
void OSGetArenaLo(CPUContext &ctx) { ctx.gpr[3] = ctx.mmu.read32(0x80000030); }

void OSGetArenaHi(CPUContext &ctx) { ctx.gpr[3] = ctx.mmu.read32(0x80000034); }

void OSSetArenaLo(CPUContext &ctx) { ctx.mmu.write32(0x80000030, ctx.gpr[3]); }

void OSSetArenaHi(CPUContext &ctx) { ctx.mmu.write32(0x80000034, ctx.gpr[3]); }

void VISetBlack(CPUContext &ctx) {
  // Ignore
}

void VIGetNextField(CPUContext &ctx) { ctx.gpr[3] = 0; }

void PADInit(CPUContext &ctx) {
  std::cout << "[HLE PAD] PADInit called" << std::endl;
}
}
