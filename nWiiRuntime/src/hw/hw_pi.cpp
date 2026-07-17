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



static uint32_t pi_fifo_base = 0;
static uint32_t pi_fifo_end = 0;
static uint32_t pi_fifo_wptr = 0;

void pi_fifo_get(uint32_t& base, uint32_t& end, uint32_t& wptr) {
    base = pi_fifo_base; end = pi_fifo_end; wptr = pi_fifo_wptr;
}
void pi_fifo_set_wptr(uint32_t wptr) { pi_fifo_wptr = wptr; }

void register_pi(MMIODispatcher& dispatcher) {{
    dispatcher.register_region(0xCC003000, 0xCC0030FF,
        [](uint32_t addr) -> uint32_t {
            if (addr == 0xCC003000) return pi_intsr;
            if (addr == 0xCC003004) return pi_intmr;
            if (addr == 0xCC00300C) return pi_fifo_base;
            if (addr == 0xCC003010) return pi_fifo_end;
            if (addr == 0xCC003014) return pi_fifo_wptr;
            if (addr == 0xCC00302C) {
                if (nwii::runtime::Config::get().platform == nwii::runtime::Platform::GameCube)
                    return 0x00000001; 
                else
                    return 0x00000023; 
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
            if (addr == 0xCC00300C) pi_fifo_base = val;
            if (addr == 0xCC003010) pi_fifo_end = val;
            if (addr == 0xCC003014) pi_fifo_wptr = val;
        }
    );
}}

} 
