#include "runtime/hw/hw.h"
#include "runtime/config.h"
#include "runtime/cpu_context.h"
#include <iostream>
#include <cstring>
#include <algorithm>

namespace nwii::runtime::hw {

uint32_t pi_intsr = 0;

void register_pi(MMIODispatcher& dispatcher) {{
    dispatcher.register_region(0xCC003000, 0xCC0030FF, 
        [](uint32_t addr) -> uint32_t {
            if (addr == 0xCC003000) return pi_intsr;
            if (addr == 0xCC003004) return 0;
            if (addr == 0xCC00302C) {
                if (nwii::runtime::Config::get().platform == nwii::runtime::Platform::GameCube)
                    return 0x00000001;
                else
                    return 0x00000002; // Retail Wii 1
            }
            return 0;
        },
        [](uint32_t addr, uint32_t val) {
            if (addr == 0xCC003000) clear_pi_interrupt(val);
        }
    );
}}

} // namespace nwii::runtime::hw
