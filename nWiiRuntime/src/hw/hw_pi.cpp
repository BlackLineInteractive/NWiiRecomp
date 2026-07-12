#include "runtime/hw/hw.h"
#include "runtime/config.h"
#include "runtime/cpu_context.h"
#include <iostream>
#include <cstring>
#include <algorithm>

#include <atomic>

namespace nwii::runtime::hw {

std::atomic<uint32_t> pi_intsr = 0;
std::atomic<uint32_t> pi_intmr = 0;

void register_pi(MMIODispatcher& dispatcher) {{
    dispatcher.register_region(0xCC003000, 0xCC0030FF, 
        [](uint32_t addr) -> uint32_t {
            if (addr == 0xCC003000) return pi_intsr;
            if (addr == 0xCC003004) return pi_intmr;
            if (addr == 0xCC00302C) {
                if (nwii::runtime::Config::get().platform == nwii::runtime::Platform::GameCube)
                    return 0x00000001; // GC chip
                else
                    return 0x00000023; // Hollywood retail chip revision
            }
            return 0;
        },
        [](uint32_t addr, uint32_t val) {
            if (addr == 0xCC003000) clear_pi_interrupt(val);
            if (addr == 0xCC003004) {
                if (pi_intmr != val)
                    std::cout << "[HW PI] INTMR change: 0x" << std::hex << pi_intmr
                              << " -> 0x" << val << std::dec << std::endl;
                pi_intmr = val;
            }
        }
    );
}}

} // namespace nwii::runtime::hw
