#include "runtime/hw/hw.h"
#include "runtime/config.h"
#include "runtime/cpu_context.h"
#include <iostream>
#include <cstring>
#include <algorithm>

namespace nwii::runtime::hw {

static uint32_t ai_cr = 0;
static uint32_t ai_vr = 0;
static uint32_t ai_scnt = 0;
static uint32_t ai_it = 0;
static uint64_t ai_start_time = 0;

void register_ai(MMIODispatcher& dispatcher) {{
    dispatcher.register_region(0xCC006C00, 0xCC006CFF,
        [](uint32_t addr) -> uint32_t {
            if (ai_cr & 0x20) {
                uint64_t current = nwii::runtime::get_os_time();
                uint32_t samples = ((current - ai_start_time) * 48) / 1000;
                uint32_t cur_scnt = ai_scnt + samples;
                if (ai_it > 0 && cur_scnt >= ai_it) {
                    ai_cr |= 0x04;
                    if (ai_cr & 0x02) trigger_pi_interrupt(0x20);
                }
            }
            if (addr == 0xCC006C00) return ai_cr;
            if (addr == 0xCC006C04) return ai_vr;
            if (addr == 0xCC006C08) return ai_scnt;
            if (addr == 0xCC006C0C) return ai_it;
            return 0; 
        },
        [](uint32_t addr, uint32_t val) {
            if (addr == 0xCC006C00) {
                if (val & 0x04) { ai_cr &= ~0x04; clear_pi_interrupt(0x20); }
                if (val & 0x08) { ai_scnt = 0; ai_start_time = nwii::runtime::get_os_time(); }
                if ((val & 0x20) && !(ai_cr & 0x20)) { ai_start_time = nwii::runtime::get_os_time(); }
                ai_cr = (ai_cr & 0x04) | (val & ~0x04);
            } else if (addr == 0xCC006C04) {
                ai_vr = val;
            } else if (addr == 0xCC006C08) {
                ai_scnt = val;
                ai_start_time = nwii::runtime::get_os_time();
            } else if (addr == 0xCC006C0C) {
                ai_it = val;
            }
        }
    );
}}

} // namespace nwii::runtime::hw
