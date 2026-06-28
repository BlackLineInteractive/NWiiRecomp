#include "runtime/config.h"
#include "runtime/cpu_context.h"
#include "runtime/hw/mmio_dispatcher.h"
#include <chrono>
#include <cstring>
#include <iostream>
#include <string>

using namespace nwii::runtime;

namespace nwii::runtime {
    uint64_t get_os_time();
}

uint64_t nwii::runtime::get_os_time() {
  auto now = std::chrono::steady_clock::now();
  return std::chrono::duration_cast<std::chrono::microseconds>(
             now.time_since_epoch())
      .count();
}

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

extern "C" void OSReport(CPUContext &ctx) {
  uint32_t format_addr = ctx.gpr[3];
  std::string format_str = read_guest_string(ctx, format_addr);

  std::string result;
  int arg_idx = 4;
  int fpr_idx = 1;

  for (size_t i = 0; i < format_str.length(); ++i) {
    if (format_str[i] == '%') {
      if (i + 1 < format_str.length() && format_str[i + 1] == '%') {
        result += '%';
        i++;
        continue;
      }

      std::string specifier = "%";
      i++;
      while (i < format_str.length() &&
             std::string("0123456789.-+ #hlz").find(format_str[i]) != std::string::npos) {
        specifier += format_str[i];
        i++;
      }

      if (i < format_str.length()) {
        char type = format_str[i];
        specifier += type;

        if (type == 'd' || type == 'i' || type == 'c') {
          char buf[64];
          std::snprintf(buf, sizeof(buf), specifier.c_str(), (int32_t)ctx.gpr[arg_idx++]);
          result += buf;
        } else if (type == 'u' || type == 'x' || type == 'X' || type == 'p') {
          char buf[64];
          std::snprintf(buf, sizeof(buf), specifier.c_str(), (uint32_t)ctx.gpr[arg_idx++]);
          result += buf;
        } else if (type == 's') {
          uint32_t str_ptr = ctx.gpr[arg_idx++];
          if (str_ptr != 0) result += read_guest_string(ctx, str_ptr);
          else result += "(null)";
        } else if (type == 'f') {
          char buf[64];
          if (fpr_idx <= 8) std::snprintf(buf, sizeof(buf), specifier.c_str(), ctx.fpr[fpr_idx++]);
          else std::snprintf(buf, sizeof(buf), "[float_spilled]");
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

namespace nwii::runtime {
uint32_t HW_Reg_Read32(uint32_t addr) {
    if ((addr & 0xFF000000) != 0xCD000000 && (addr & 0xFF000000) != 0x0D000000) {
        addr = (addr & 0x00FFFFFF) | 0xCC000000;
    }
    return MMIODispatcher::get().read32(addr);
}
uint16_t HW_Reg_Read16(uint32_t addr) { return (uint16_t)HW_Reg_Read32(addr); }
void HW_Reg_Write32(uint32_t addr, uint32_t val) {
    if ((addr & 0xFF000000) != 0xCD000000 && (addr & 0xFF000000) != 0x0D000000) {
        addr = (addr & 0x00FFFFFF) | 0xCC000000;
    }
    MMIODispatcher::get().write32(addr, val);
}
void HW_Reg_Write16(uint32_t addr, uint16_t val) { HW_Reg_Write32(addr, val); }
}
