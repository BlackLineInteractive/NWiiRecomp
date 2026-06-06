#include "runtime/cpu_context.h"
#include <iostream>
#include <string>

using namespace nwii::runtime;

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

// OSReport  is the standard NN SDK print function.
// Signature: void OSReport(const char* msg, ...);
// The format string address is passed in r3 (gpr[3]).
void OSReport(CPUContext &ctx) {
  uint32_t format_addr = ctx.gpr[3];
  std::string format_str = read_guest_string(ctx, format_addr);

  std::string result;
  int arg_idx = 4; // PowerPC arguments start at r4

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
          result += "[float_FPR]"; // Floats are passed in registers f1-f8,
                                   // temporarily ignoring
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
void OSInit(CPUContext &ctx) {
  std::cout << "[OSInit] System initialized." << std::endl;
}

namespace nwii::runtime {
extern MMU *g_mmu;
}

extern "C" {

static uint32_t vi_vblank_counter = 0;

static uint16_t dsp_mbox_cpu_hi = 0;
static uint16_t dsp_mbox_cpu_lo = 0;
static uint16_t dsp_mbox_dsp_hi = 0;
static uint16_t dsp_mbox_dsp_lo = 0;

static uint32_t ipc_arm_msg = 0;  // HW_IPC_ARMMSG  (0xCD000008)
static uint32_t ipc_arm_ctrl = 0; // HW_IPC_ARMCTRL (0xCD00000C)

// g_mmu declaration moved outside extern "C"
static void ipc_fake_ack(uint32_t request_addr) {

  // HW_IPC_ARMMSG
  ipc_arm_msg = request_addr & 0x1FFFFFFF;
  ipc_arm_ctrl = 0x00000002; // bit1 = Y2

  if (nwii::runtime::g_mmu) {
    uint32_t cmd = nwii::runtime::g_mmu->read32(request_addr);
    int32_t result = 0; // IPC_OK
    if (cmd == 1 || cmd == 2) {
      result = 1; // Return fake FD 1 for Open/Close
    } else if (cmd == 3 || cmd == 4) {
      // Read/Write: return length (from offset 20 usually, but let's just
      // return what was requested if possible)
      uint32_t length = nwii::runtime::g_mmu->read32(
          request_addr + 20); // usually length is at offset 20
      result = length;
    }

    std::cout << "[HW IPC] Processing command " << cmd << " at 0x" << std::hex
              << request_addr << std::dec << ", returning result " << result
              << "\n";

    nwii::runtime::g_mmu->write32(request_addr + 4, (uint32_t)result);
  }
}

uint16_t HW_Reg_Read16(uint32_t addr) { return (uint16_t)HW_Reg_Read32(addr); }

uint32_t HW_Reg_Read32(uint32_t addr) {
  // --- IOS IPC registers (Wii, 0xCD000000–0xCD00FFFF) ---
  if ((addr & 0xFF000000) == 0xCD000000) {
    switch (addr & 0x00FFFFFF) {
    case 0x000000:
      return 0; // HW_IPC_PPCMSG
    case 0x000004:
      return 0; // HW_IPC_PPCCTRL
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
    if (addr == 0xCC00302C)
      return 0x00000020; // PI_CPUREV (Hardware Revision)
    return 0;            // PI Interrupt Cause/Mask = 0 (no pending interrupts)
  }

  // VI (Video Interface) - VBlank counter
  if (addr >= 0xCC002000 && addr <= 0xCC0020FF) {
    if (addr == 0xCC00202C || addr == 0xCC002030) {
      return vi_vblank_counter++;
    }
    return 0;
  }

  // AI/DSP mailbox
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
      return 0x0020;
    return 0;
  }

  // EXI (Expansion Interface) - transfer "free"
  if (addr >= 0xCC006800 && addr <= 0xCC0068FF) {
    return 0;
  }

  // DI (DVD Interface)
  if (addr >= 0xCC006000 && addr <= 0xCC0060FF) {
    if (addr == 0xCC006000)
      return 0;
    if (addr == 0xCC006004)
      return 1; // Cover closed
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
      // HW_IPC_PPCCTRL: game sets 1 meaning "request sent" (ignored)
      break;
    case 0x00000C:
      // HW_IPC_ARMCTRL: game writes 0 to confirm receipt of response
      if (val == 0)
        ipc_arm_ctrl = 0;
      break;
    default:
      break;
    }
    return;
  }

  // --- GC hardware registers (0xCC000000) ---
  addr = (addr & 0x00FFFFFF) | 0xCC000000;

  if (addr >= 0xCC005000 && addr <= 0xCC0050FF) {
    if (addr == 0xCC005000 || addr == 0xCC005002) {
      dsp_mbox_cpu_hi = (val & 0x7FFF) | 0x8000;
      dsp_mbox_cpu_hi &= ~0x8000; // DSP immediately accepts the message
      dsp_mbox_dsp_hi = 0x8000;   // DSP replies instantly
    } else if (addr == 0xCC005004 || addr == 0xCC005006) {
      if (val & 0x8000)
        dsp_mbox_dsp_hi &= ~0x8000;
    } else if (addr == 0xCC005008 || addr == 0xCC00500A) {
      dsp_mbox_dsp_hi = 0xDCD1;
      dsp_mbox_dsp_lo = 0x0000;
    }
    return;
  }
  // Other GC registers - ignored
}

} // extern "C"

#include <chrono>

static uint64_t get_os_time() {
  auto now = std::chrono::high_resolution_clock::now();
  return std::chrono::duration_cast<std::chrono::microseconds>(
             now.time_since_epoch())
      .count();
}

extern "C" {

// Interrupt Management
uint32_t OSDisableInterrupts(CPUContext &ctx) {
  uint32_t old_msr = ctx.msr;
  ctx.msr &= ~(1 << 15); // Clear EE (External Interrupt Enable) bit
  ctx.gpr[3] = old_msr;
  return old_msr;
}

uint32_t OSEnableInterrupts(CPUContext &ctx) {
  uint32_t old_msr = ctx.msr;
  ctx.msr |= (1 << 15); // Set EE bit
  ctx.gpr[3] = old_msr;
  return old_msr;
}

uint32_t OSRestoreInterrupts(CPUContext &ctx) {
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

} // extern "C"
