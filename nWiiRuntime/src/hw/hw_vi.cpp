#include "runtime/hw/hw.h"
#include <cstdlib>
#include "runtime/config.h"
#include "runtime/cpu_context.h"
#include <iostream>
#include <cstring>
#include <algorithm>

namespace nwii::runtime::hw {

static uint32_t vi_dcr = 0;
// VI Top and Bottom Field Base Left registers (0xCC00201C, 0xCC002024)
uint32_t g_vi_top_field_base = 0;
uint32_t g_vi_btm_field_base = 0;

// Display interrupt registers DI0..DI3 at 0xCC002030/34/38/3C (32-bit each,
// games access them as HI/LO halfwords). Bit31 = interrupt status (halfword
// bit15 of HI), bit28 = enable. Layout per Dolphin VideoInterface.h:
// PRERETRACE=0x30, POSTRETRACE=0x34, DI2=0x38, DI3=0x3C.
static uint32_t vi_di[4] = {0, 0, 0, 0};

static void vi_update_interrupt() {
    for (int i = 0; i < 4; ++i) {
        if (vi_di[i] & 0x80000000) {
            trigger_pi_interrupt(0x00000100);
            return;
        }
    }
    clear_pi_interrupt(0x00000100);
}

void vi_trigger_interrupt() {
    if (std::getenv("NWII_VITRACE")) {
        static int n = 0;
        if (n++ < 40) std::cout << "[VItrig] #" << std::dec << n << "\n";
    }
    // One retrace: assert pre-retrace (DI0) and post-retrace (DI1) status.
    // DI2/DI3 stay clear — SDK retrace handlers treat them as special and
    // early-exit when they fire.
    vi_di[0] |= 0x80000000;
    vi_di[1] |= 0x80000000;
    // VI = PI_INTSR bit 8 = 0x00000100 (Dolphin ProcessorInterface
    // INT_CAUSE_VI). The caller gates on the PI mask (pi_intmr & 0x100).
    trigger_pi_interrupt(0x00000100);
}

void register_vi(MMIODispatcher& dispatcher) {{
    dispatcher.register_region(0xCC002000, 0xCC0020FF,
        [](uint32_t addr) -> uint32_t {
            // Beam position must be a function of TIME, not of reads: the
            // SDK's getCurrentPosition (vi.c) reads the vertical position
            // twice and spins with interrupts off until both reads match.
            // A per-read counter never matches; a time-derived line is
            // stable across back-to-back reads yet sweeps a full field
            // (~64us per line, 313-line wrap covers PAL and NTSC use).
            if (addr == 0xCC00202C)
                return (uint32_t)((nwii::runtime::get_os_time() / 64) % 313) + 1;
            // Horizontal beam position (1-based).
            if (addr == 0xCC00202E) return 1;
            // VI Display Config Register (0xCC002002, halfword): report ENB=1
            // (bit0) so the SDK's VIInit sees VI already enabled by the bootrom
            // and skips its hardcoded NTSC default configure. Without this, a
            // PAL title's later VIConfigure(PAL) is seen as an illegal
            // NTSC->PAL switch and the SDK asserts+halts (vi.c).
            if (addr == 0xCC002002) return vi_dcr | 0x0001;
            // VI Framebuffer addresses
            if (addr == 0xCC00201C) return g_vi_top_field_base;
            if (addr == 0xCC002024) return g_vi_btm_field_base;
            // DI0..DI3 HI halfwords (0x30/34/38/3C). A register READ must
            // never raise or re-arm an interrupt.
            if (addr >= 0xCC002030 && addr <= 0xCC00203C && ((addr - 0xCC002030) % 4 == 0)) {
                return vi_di[(addr - 0xCC002030) / 4] >> 16;
            }
            // DI0..DI3 LO halfwords (0x32/36/3A/3E).
            if (addr >= 0xCC002032 && addr <= 0xCC00203E && ((addr - 0xCC002032) % 4 == 0)) {
                return vi_di[(addr - 0xCC002032) / 4] & 0xFFFF;
            }
            return 0;
        },
        [](uint32_t addr, uint32_t val) {
            if (addr == 0xCC002002) {
                vi_dcr = val;
            }
            else if (addr == 0xCC00201C) {
                g_vi_top_field_base = val;
            }
            else if (addr == 0xCC002024) {
                g_vi_btm_field_base = val;
            }
            else if (addr >= 0xCC002030 && addr <= 0xCC00203C && ((addr - 0xCC002030) % 4 == 0)) {
                int idx = (addr - 0xCC002030) / 4;
                // Writing HI with bit15 clear acknowledges this register's
                // interrupt; PI line follows the OR of all four statuses.
                if ((val & 0x8000) == 0) {
                    vi_di[idx] &= ~0x80000000;
                }
                vi_di[idx] = (vi_di[idx] & 0x8000FFFF) | ((val & ~0x8000u) << 16);
                vi_update_interrupt();
            }
            else if (addr >= 0xCC002032 && addr <= 0xCC00203E && ((addr - 0xCC002032) % 4 == 0)) {
                int idx = (addr - 0xCC002032) / 4;
                vi_di[idx] = (vi_di[idx] & 0xFFFF0000) | (val & 0xFFFF);
            }
        }
    );
}}

} // namespace nwii::runtime::hw
