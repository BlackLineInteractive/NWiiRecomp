#include "runtime/hw/hw.h"
#include "runtime/config.h"
#include "runtime/cpu_context.h"
#include <iostream>
#include <cstring>
#include <algorithm>

namespace nwii::runtime::hw {

static uint32_t vi_vblank_counter = 0;
static uint32_t vi_dcr = 0;

void vi_trigger_interrupt() {
    vi_dcr |= 0x80000000;
    trigger_pi_interrupt(0x00000100);
}

void register_vi(MMIODispatcher& dispatcher) {{
    dispatcher.register_region(0xCC002000, 0xCC0020FF,
        [](uint32_t addr) -> uint32_t {
            if (addr == 0xCC00202C) return vi_vblank_counter++;
            if (addr == 0xCC002030) {
                uint32_t current_line = (vi_vblank_counter % 262);
                uint32_t target_vct = (vi_dcr >> 16) & 0x3FF;
                if (current_line == target_vct) {
                    vi_dcr |= 0x80000000;
                    trigger_pi_interrupt(0x00000100);
                }
                return vi_dcr;
            }
            return 0;
        },
        [](uint32_t addr, uint32_t val) {
            if (addr == 0xCC002030) {
                if ((val & 0x80000000) == 0) {
                    vi_dcr &= ~0x80000000;
                    clear_pi_interrupt(0x00000100);
                }
                vi_dcr = (vi_dcr & 0x80000000) | (val & ~0x80000000);
            }
        }
    );
}}

} // namespace nwii::runtime::hw
