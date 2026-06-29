#include "runtime/hw/hw.h"
#include "runtime/config.h"
#include "runtime/cpu_context.h"
#include <iostream>
#include <cstring>
#include <algorithm>

namespace nwii::runtime::hw {

static uint32_t di_cmd[3] = {0, 0, 0};
static uint32_t di_mar = 0;
static uint32_t di_len = 0;
static uint32_t di_cr = 0;
static uint32_t di_imm = 0;
static uint32_t di_cfg = 0;
static uint32_t di_sr = 0x00000008;

void register_di(MMIODispatcher& dispatcher) {{
    dispatcher.register_region(0xCC006000, 0xCC0060FF,
        [](uint32_t addr) -> uint32_t {
            if (addr == 0xCC006000) return di_sr;
            if (addr == 0xCC006004) return 1;
            if (addr == 0xCC006008) return di_cmd[0];
            if (addr == 0xCC00600C) return di_cmd[1];
            if (addr == 0xCC006010) return di_cmd[2];
            if (addr == 0xCC006014) return di_mar;
            if (addr == 0xCC006018) return di_len;
            if (addr == 0xCC00601C) return di_cr;
            if (addr == 0xCC006020) return di_imm;
            if (addr == 0xCC006024) return di_cfg;
            return 0;
        },
        [](uint32_t addr, uint32_t val) {
            if (addr == 0xCC006000) {
                di_sr &= ~val;
                if (!(di_sr & 0x0A)) clear_pi_interrupt(0x01);
            } else if (addr == 0xCC006008) { di_cmd[0] = val; }
            else if (addr == 0xCC00600C) { di_cmd[1] = val; }
            else if (addr == 0xCC006010) { di_cmd[2] = val; }
            else if (addr == 0xCC006014) { di_mar = val; }
            else if (addr == 0xCC006018) { di_len = val; }
            else if (addr == 0xCC00601C) {
                di_cr = val;
                if (val & 1) {
                    di_cr &= ~1;
                    uint32_t cmd = di_cmd[0] >> 24;
                    if (cmd == 0x12) di_imm = 0;
                    else if (cmd == 0xAB) di_imm = 0;
                    else if (cmd == 0xE0) di_imm = 0;
                    di_sr |= 0x08;
                    if (di_sr & 0x04) trigger_pi_interrupt(0x04);
                }
            }
        }
    );
}}

} // namespace nwii::runtime::hw
