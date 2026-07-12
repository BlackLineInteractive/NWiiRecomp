#include "runtime/config.h"
#include "runtime/cpu_context.h"
#include "runtime/hw/hw.h"
#include "runtime/hw/mmio_dispatcher.h"
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <set>
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

namespace nwii::runtime {
bool g_trace_calls = []() {
  const char *env = std::getenv("NWII_TRACE_CALLS");
  return env && env[0] == '1';
}();

uint32_t g_watch_addr = []() -> uint32_t {
  const char *env = std::getenv("NWII_WATCH");
  return env ? (uint32_t)std::strtoul(env, nullptr, 16) : 0;
}();

extern CPUContext *g_ctx_ptr;

void watch_hit(uint32_t addr, uint32_t value, int width) {
  CPUContext *c = g_ctx_ptr;
  std::cout << "[Watch] write" << width << " 0x" << std::hex << addr << " = 0x"
            << value;
  if (c)
    std::cout << " pc=0x" << c->pc << " lr=0x" << c->lr;
  std::cout << std::dec << "\n";
}

// Optional allow-list: NWII_TRACE_ONLY=addr,addr,... restricts [CALL] logging
// to those function entries (hex, comma-separated). Empty = trace everything.
static std::set<uint32_t> g_trace_only = []() {
  std::set<uint32_t> s;
  const char *env = std::getenv("NWII_TRACE_ONLY");
  if (env) {
    std::string in(env);
    size_t p = 0;
    while (p < in.size()) {
      size_t c = in.find(',', p);
      if (c == std::string::npos) c = in.size();
      s.insert((uint32_t)std::strtoul(in.substr(p, c - p).c_str(), nullptr, 16));
      p = c + 1;
    }
  }
  return s;
}();

void note_null_call(uint32_t site, uint32_t lr) {
  static int n = 0;
  if (n++ < 20)
    std::cout << "[NullCall] no-op call through null ptr at site 0x" << std::hex
              << site << " lr=0x" << lr << std::dec << "\n";
}

void trace_call(uint32_t func_addr, CPUContext &ctx) {
  if (!g_trace_only.empty() && !g_trace_only.count(func_addr)) return;
  std::cout << "[CALL] 0x" << std::hex << func_addr << " lr=0x" << ctx.lr
            << " r3=0x" << ctx.gpr[3] << " r4=0x" << ctx.gpr[4] << " r5=0x"
            << ctx.gpr[5] << " r6=0x" << ctx.gpr[6] << " r7=0x" << ctx.gpr[7]
            << " r13=0x" << ctx.gpr[13] << " r1=0x" << ctx.gpr[1]
            << std::dec << "\n";
}
} // namespace nwii::runtime

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
          if (str_ptr != 0)
            result += read_guest_string(ctx, str_ptr);
          else
            result += "(null)";
        } else if (type == 'f') {
          char buf[64];
          if (fpr_idx <= 8)
            std::snprintf(buf, sizeof(buf), specifier.c_str(),
                          ctx.fpr[fpr_idx++]);
          else
            std::snprintf(buf, sizeof(buf), "[float_spilled]");
          result += buf;
        } else {
          result += specifier;
        }
      }
    } else {
      result += format_str[i];
    }
  }
  std::cout << "[OSReport] [LR:0x" << std::hex << ctx.lr << std::dec << "] " << result;
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
} // namespace nwii::runtime

extern "C" void DVD_Callback(CPUContext &ctx) {
  std::cout << "[HLE DVD_Callback] Triggered at PC=0x" << std::hex << ctx.pc
            << std::dec << "\n";
}

extern "C" void VIInit(CPUContext &ctx) {
  std::cout << "[HLE VIInit] Triggered at PC=0x" << std::hex << ctx.pc
            << std::dec << "\n";
  // The game unmasks VI (PI_INTMR bit 8 = 0x100) itself via MMIO writes.
}

extern "C" void VIConfigure(CPUContext &ctx) {
  std::cout << "[HLE VIConfigure] Triggered at PC=0x" << std::hex << ctx.pc
            << std::dec << "\n";
}

extern "C" void VIConfigurePan(CPUContext &ctx) {
  std::cout << "[HLE VIConfigurePan] Triggered at PC=0x" << std::hex << ctx.pc
            << std::dec << "\n";
}

extern "C" void VISetBlack(CPUContext &ctx) {
  std::cout << "[HLE VISetBlack] Triggered at PC=0x" << std::hex << ctx.pc
            << std::dec << "\n";
}

extern "C" void VIGetNextField(CPUContext &ctx) {
  static uint32_t field = 0;
  field ^= 1;
  ctx.gpr[3] = field;
}

extern "C" void VIWaitForRetrace(CPUContext &ctx) {
  // Advance the global retrace counter so the caller exits immediately.
  // Without a real VI ISR updating __VIRetraceCount, the game loops forever.
  nwii::runtime::hw::vi_trigger_interrupt();
}