#include "runtime/hw/hw.h"
#include "runtime/config.h"
#include "runtime/cpu_context.h"
#include <iostream>
#include <cstring>
#include <algorithm>

namespace nwii::runtime::hw {

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

void register_si(MMIODispatcher& dispatcher) {{
    dispatcher.register_region(0xCC006400, 0xCC0064FF,
        [](uint32_t addr) -> uint32_t {
            if (addr <= 0xCC00642C) {
                int ch = (addr - 0xCC006400) / 0x0C;
                int reg = (addr - 0xCC006400) % 0x0C;
                if (ch >= 0 && ch < 4) {
                    if (reg == 0x00) return si_chan[ch].out;
                    if (reg == 0x04) {
                        if (ch == 0) return 0x08000000 | si_chan[ch].in_hi;
                        return si_chan[ch].in_hi | 0x80000000;
                    }
                    if (reg == 0x08) return si_chan[ch].in_lo;
                }
            }
            if (addr == 0xCC006430) return si_poll;
            if (addr == 0xCC006434) return si_com_csr;
            if (addr == 0xCC006438) return si_status;
            if (addr == 0xCC00643C) return si_exi_clock;
            return 0;
        },
        [](uint32_t addr, uint32_t val) {
            if (addr <= 0xCC00642C) {
                int ch = (addr - 0xCC006400) / 0x0C;
                int reg = (addr - 0xCC006400) % 0x0C;
                if (ch >= 0 && ch < 4) {
                    if (reg == 0x00) si_chan[ch].out = val;
                    if (reg == 0x04) si_chan[ch].in_hi = val;
                    if (reg == 0x08) si_chan[ch].in_lo = val;
                }
            }
            if (addr == 0xCC006430) si_poll = val;
            if (addr == 0xCC006434) {
                si_com_csr = val;
                if (val & 1) {
                    si_com_csr &= ~1;
                    si_com_csr |= 0x80000000;
                    if ((si_com_csr & 0x40000000) || (si_com_csr & 0x08000000))
                        trigger_pi_interrupt(0x08);
                }
            }
            if (addr == 0xCC006438) {
                uint32_t clear_mask = val & 0x0F0F0F0F;
                si_status &= ~clear_mask;
                if (val & 0x80000000) {
                    si_status &= ~0x80000000;
                    si_status |= 0x20000000;
                }
            }
            if (addr == 0xCC00643C) si_exi_clock = val;
        }
    );
}}

} // namespace nwii::runtime::hw
