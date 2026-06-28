#include "runtime/hw/hw.h"
#include "runtime/config.h"
#include "runtime/cpu_context.h"
#include <iostream>
#include <cstring>
#include <algorithm>

namespace nwii::runtime::hw {

struct EXIChannel {
  uint32_t status = 0;
  uint32_t dma_addr = 0;
  uint32_t dma_len = 0;
  uint32_t cr = 0;
  uint32_t data = 0;
};
static EXIChannel exi_chan[3];

void register_exi(MMIODispatcher& dispatcher) {{
    dispatcher.register_region(0xCC006800, 0xCC0068FF,
        [](uint32_t addr) -> uint32_t {
            if (addr > 0xCC00682C) return 0;
            int ch = (addr - 0xCC006800) / 0x14;
            int reg = (addr - 0xCC006800) % 0x14;
            if (ch >= 0 && ch < 3) {
                if (reg == 0x00) return exi_chan[ch].status | 0x1000;
                if (reg == 0x04) return exi_chan[ch].dma_addr;
                if (reg == 0x08) return exi_chan[ch].dma_len;
                if (reg == 0x0C) return exi_chan[ch].cr;
                if (reg == 0x10) return exi_chan[ch].data;
            }
            return 0;
        },
        [](uint32_t addr, uint32_t val) {
            if (addr > 0xCC00682C) return;
            int ch = (addr - 0xCC006800) / 0x14;
            int reg = (addr - 0xCC006800) % 0x14;
            if (ch >= 0 && ch < 3) {
                if (reg == 0x00) {
                    if (val & 2) exi_chan[ch].status &= ~2;
                    if (val & 8) exi_chan[ch].status &= ~8;
                    if (val & 0x800) exi_chan[ch].status &= ~0x800;
                    exi_chan[ch].status = (exi_chan[ch].status & 0x80A) | (val & ~0x80A);
                }
                if (reg == 0x04) exi_chan[ch].dma_addr = val;
                if (reg == 0x08) exi_chan[ch].dma_len = val;
                if (reg == 0x0C) {
                    exi_chan[ch].cr = val;
                    if (val & 1) {
                        exi_chan[ch].cr &= ~1;
                        exi_chan[ch].status |= 8;
                        if (exi_chan[ch].status & 4) trigger_pi_interrupt(0x10);
                        if ((val & 2) == 0 && ((val >> 2) & 3) != 1) {
                            exi_chan[ch].data = 0;
                        }
                    }
                }
                if (reg == 0x10) exi_chan[ch].data = val;
            }
        }
    );
}}

} // namespace nwii::runtime::hw
