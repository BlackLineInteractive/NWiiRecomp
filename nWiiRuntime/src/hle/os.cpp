#include "runtime/config.h"
#include "runtime/cpu_context.h"
#include "runtime/mmio_dispatcher.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <iostream>
#include <string>

using namespace nwii::runtime;

uint64_t get_os_time() {
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
  static bool os_initialized = false;
  if (!os_initialized) {
    std::cout << "[OSInit] System initialized." << std::endl;
    os_initialized = true;
  }
}

namespace nwii {
namespace runtime {
extern MMU *g_mmu;
}
} // namespace nwii

namespace nwii {
namespace runtime {
uint16_t HW_Reg_Read16(uint32_t addr) { return (uint16_t)HW_Reg_Read32(addr); }

uint32_t HW_Reg_Read32(uint32_t addr) {
    if ((addr & 0xFF000000) != 0xCD000000 && (addr & 0xFF000000) != 0x0D000000) {
        addr = (addr & 0x00FFFFFF) | 0xCC000000;
    }
    return MMIODispatcher::get().read32(addr);
}

void HW_Reg_Write16(uint32_t addr, uint16_t val) { HW_Reg_Write32(addr, val); }

void HW_Reg_Write32(uint32_t addr, uint32_t val) {
    if ((addr & 0xFF000000) != 0xCD000000 && (addr & 0xFF000000) != 0x0D000000) {
        addr = (addr & 0x00FFFFFF) | 0xCC000000;
    }
    MMIODispatcher::get().write32(addr, val);
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
  uint64_t micros = get_os_time();
  uint64_t tb_freq = (nwii::runtime::Config::get().platform == nwii::runtime::Platform::GameCube) ? 40500000 : 60750000;
  uint64_t ticks = (micros * tb_freq) / 1000000;
  ctx.gpr[3] = (uint32_t)(ticks >> 32);
  ctx.gpr[4] = (uint32_t)(ticks & 0xFFFFFFFF);
}

void OSTicksToMilliseconds(CPUContext &ctx) {
  uint64_t ticks = ((uint64_t)ctx.gpr[3] << 32) | ctx.gpr[4];
  uint64_t tb_freq = (nwii::runtime::Config::get().platform == nwii::runtime::Platform::GameCube) ? 40500000 : 60750000;
  uint64_t ms = (ticks * 1000) / tb_freq;
  ctx.gpr[3] = (uint32_t)(ms >> 32);
  ctx.gpr[4] = (uint32_t)(ms & 0xFFFFFFFF);
}

// --- MEMORY ARENA MANAGEMENT ---
void OSGetArenaLo(CPUContext &ctx) { ctx.gpr[3] = ctx.mmu.read32(0x80000030); }

void OSGetArenaHi(CPUContext &ctx) { ctx.gpr[3] = ctx.mmu.read32(0x80000034); }

void OSSetArenaLo(CPUContext &ctx) { ctx.mmu.write32(0x80000030, ctx.gpr[3]); }

void OSSetArenaHi(CPUContext &ctx) { ctx.mmu.write32(0x80000034, ctx.gpr[3]); }

void VISetBlack(CPUContext &ctx) {
  // Unused stub
}

void VIInit(CPUContext &ctx) {
  // Initialize VI (Video Interface)
}

void VIConfigure(CPUContext &ctx) {
  // Configure VI (Video Interface)
}

void VIConfigurePan(CPUContext &ctx) {
  // Configure VI Pan
}

void VIGetNextField(CPUContext &ctx) { ctx.gpr[3] = 0; }

void DVD_Callback(CPUContext &ctx) {
  // DVD callback stub
}

}
