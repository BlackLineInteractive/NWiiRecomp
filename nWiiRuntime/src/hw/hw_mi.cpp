#include "runtime/hw/hw.h"
#include <array>
#include <cstdint>

namespace nwii::runtime::hw {

static std::array<uint32_t, 64> mi_regs{};

void register_mi(MMIODispatcher &dispatcher) {
  dispatcher.register_region(
      0xCC004000, 0xCC0040FF,
      [](uint32_t addr) -> uint32_t {
        uint32_t idx = (addr - 0xCC004000) / 4;
        return (idx < mi_regs.size()) ? mi_regs[idx] : 0;
      },
      [](uint32_t addr, uint32_t val) {
        uint32_t idx = (addr - 0xCC004000) / 4;
        if (idx < mi_regs.size())
          mi_regs[idx] = val;
      });
}

} // namespace nwii::runtime::hw