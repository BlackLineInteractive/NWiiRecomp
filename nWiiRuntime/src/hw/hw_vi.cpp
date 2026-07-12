#include "runtime/hw/hw.h"
#include <cstdlib>
#include "runtime/config.h"
#include "runtime/cpu_context.h"
#include <iostream>
#include <cstring>
#include <algorithm>

namespace nwii::runtime::hw {

static uint32_t vi_vblank_counter = 0;
static uint32_t vi_dcr = 0;
static uint32_t vi_vtr[4] = {0, 0, 0, 0};

void vi_trigger_interrupt() {
    if (std::getenv("NWII_VITRACE")) {
        static int n = 0;
        if (n++ < 40) std::cout << "[VItrig] #" << std::dec << n << "\n";
    }
    vi_vtr[0] |= 0x80000000;
    // VI = PI_INTSR bit 8 = 0x00000100 (Dolphin ProcessorInterface INT_CAUSE_VI)
    trigger_pi_interrupt(0x00000100);
}

void register_vi(MMIODispatcher& dispatcher) {{
    dispatcher.register_region(0xCC002000, 0xCC0020FF,
        [](uint32_t addr) -> uint32_t {
            if (addr == 0xCC00202C) return vi_vblank_counter++;
            if (addr == 0xCC002030) {
                // A register READ must never raise an interrupt — doing so let
                // the SDK VI ISR re-trigger VI while it was still reading VI
                // registers, nesting the handler until the guest stack wrapped.
                return vi_dcr;
            }
            if (addr >= 0xCC002034 && addr <= 0xCC002040 && ((addr - 0xCC002034) % 4 == 0)) {
                return vi_vtr[(addr - 0xCC002034) / 4] >> 16;
            }
            if (addr >= 0xCC002036 && addr <= 0xCC002042 && ((addr - 0xCC002036) % 4 == 0)) {
                return vi_vtr[(addr - 0xCC002036) / 4] & 0xFFFF;
            }
            return 0;
        },
        [](uint32_t addr, uint32_t val) {
            if (addr == 0xCC002030) {
                vi_dcr = val;
            }
            else if (addr >= 0xCC002034 && addr <= 0xCC002040 && ((addr - 0xCC002034) % 4 == 0)) {
                int idx = (addr - 0xCC002034) / 4;
                if ((val & 0x8000) == 0) {
                    vi_vtr[idx] &= ~0x80000000;
                    if (idx == 0) clear_pi_interrupt(0x00000100);
                }
                vi_vtr[idx] = (vi_vtr[idx] & 0x8000FFFF) | ((val & ~0x8000) << 16);
            }
            else if (addr >= 0xCC002036 && addr <= 0xCC002042 && ((addr - 0xCC002036) % 4 == 0)) {
                int idx = (addr - 0xCC002036) / 4;
                vi_vtr[idx] = (vi_vtr[idx] & 0xFFFF0000) | (val & 0xFFFF);
            }
        }
    );
}}

} // namespace nwii::runtime::hw
