#include "runtime/hw/hw.h"
#include "runtime/config.h"
#include "runtime/cpu_context.h"
#include <iostream>
#include <cstring>
#include <algorithm>

namespace nwii::runtime {
    extern MMU* g_mmu;
}

namespace nwii::runtime::hw {

static uint16_t dsp_mbox_cpu_hi = 0;
static uint16_t dsp_mbox_cpu_lo = 0;
static uint16_t dsp_mbox_dsp_hi = 0;
static uint16_t dsp_mbox_dsp_lo = 0;
static uint32_t ar_mmaddr = 0;
static uint32_t ar_araddr = 0;
static uint32_t ar_cnt = 0;
static uint16_t dsp_control = 0x0800; // HW boots with DSP HALTED


void register_dsp(MMIODispatcher& dispatcher) {{
    dispatcher.register_region(0xCC005000, 0xCC0050FF,
        [](uint32_t addr) -> uint32_t {
            if (addr == 0xCC005000 || addr == 0xCC005002) return dsp_mbox_cpu_hi;
            if (addr == 0xCC005004) return dsp_mbox_dsp_hi;
            if (addr == 0xCC005006) {
                uint16_t val = dsp_mbox_dsp_lo;
                dsp_mbox_dsp_hi &= ~0x8000;
                return val;
            }
            if (addr == 0xCC005008 || addr == 0xCC00500A) return dsp_control;
            if (addr == 0xCC005020) return ar_mmaddr;
            if (addr == 0xCC005024) return ar_araddr;
            if (addr == 0xCC005028) return ar_cnt;
            return 0;
        },
        [](uint32_t addr, uint32_t val) {
            if (addr == 0xCC005000 || addr == 0xCC005002) {
                dsp_mbox_cpu_hi = (val & 0x7FFF) | 0x8000;
                dsp_mbox_cpu_hi &= ~0x8000;
                dsp_mbox_dsp_hi = 0x8000;
            } else if (addr == 0xCC005004 || addr == 0xCC005006) {
                if (val & 0x8000) dsp_mbox_dsp_hi &= ~0x8000;
            } else if (addr == 0xCC005008 || addr == 0xCC00500A) {
                uint16_t val16 = val & 0xFFFF;
                if (val16 & 0x0008) dsp_control &= ~0x0008;
                if (val16 & 0x0020) dsp_control &= ~0x0020;
                if (val16 & 0x0080) dsp_control &= ~0x0080;
                if (val16 & 0x0001) {
                    dsp_mbox_dsp_hi = 0xDCD1;
                    dsp_mbox_dsp_lo = 0x0000;
                    dsp_control |= 0x0080;
                }
                bool was_halted = (dsp_control & 0x0800) != 0;
                bool is_halted = (val16 & 0x0800) != 0;
                dsp_control = (dsp_control & 0x00A8) | (val16 & ~0x00A9);
                if (was_halted && !is_halted) {
                    dsp_mbox_dsp_hi = 0xDCD1;
                    dsp_mbox_dsp_lo = 0x0000;
                    dsp_control |= 0x0080;
                }
                bool pi_int = false;
                if ((dsp_control & 0x0008) && (dsp_control & 0x0010)) pi_int = true;
                if ((dsp_control & 0x0020) && (dsp_control & 0x0040)) pi_int = true;
                if ((dsp_control & 0x0080) && (dsp_control & 0x0100)) pi_int = true;
                if (pi_int) trigger_pi_interrupt(0x40);
                else clear_pi_interrupt(0x40);
            } else if (addr == 0xCC005020) {
                ar_mmaddr = val;
            } else if (addr == 0xCC005024) {
                ar_araddr = val;
            } else if (addr == 0xCC005028) {
                ar_cnt = val;
                bool dir = (val & 0x80000000) != 0;
                uint32_t count = val & 0x7FFFFFFF;
                uint32_t mm = ar_mmaddr & 0x01FFFFFF;
                uint32_t ar_a = ar_araddr & 0x00FFFFFF;
                if (g_mmu && count > 0) {
                    uint8_t *mem1 = nwii::runtime::g_mmu->mem1.data();
                    uint8_t *mem2 = nwii::runtime::g_mmu->mem2.data();
                    uint32_t mem1_size = nwii::runtime::g_mmu->mem1.size();
                    uint32_t mem2_size = nwii::runtime::g_mmu->mem2.size();
                    
                    if (mm <= 0x30AC && mm + count > 0x30AC) {
                        std::cout << "[DSP DMA] Corrupting IPC Handler! dir=" << dir << " mm=0x" << std::hex << mm << " ar_a=0x" << ar_a << " count=0x" << count << std::dec << "\n";
                    }

                    if (mm < mem1_size && ar_a < mem2_size) {
                        uint32_t copy_count = std::min({count, mem1_size - mm, mem2_size - ar_a});
                        if (!dir) std::memcpy(mem2 + ar_a, mem1 + mm, copy_count);
                        else std::memcpy(mem1 + mm, mem2 + ar_a, copy_count);
                    }
                }
                ar_cnt = 0;
                dsp_control |= 0x0020;
                if (dsp_control & 0x0040) trigger_pi_interrupt(0x40);
            }
        }
    );
}}

} // namespace nwii::runtime::hw
