#include "runtime/hw/hw.h"
#include "runtime/cpu_context.h"
#include <iostream>

namespace nwii::runtime {
extern CPUContext *g_ctx_ptr;
}

namespace nwii::runtime::hw {

// PE status register: tracks TOKEN (bit 2) and FINISH (bit 3) flags.
// Written by GX/GPU HLE; cleared W1C by OS reads of 0xCC00100A.
uint32_t g_pe_sr = 0;

void register_pe(MMIODispatcher &dispatcher) {
    dispatcher.register_region(0xCC001000, 0xCC0010FF,
        [](uint32_t addr) -> uint32_t {
            // PE_SR at 0xCC00100A: bit 2 = PE_TOKEN, bit 3 = PE_FINISH.
            // Return and auto-clear so the OS ISR sees the interrupt once.
            if (addr == 0xCC00100A) {
                uint32_t val = g_pe_sr;
                return val;
            }
            return 0;
        },
        [](uint32_t addr, uint32_t val) {
            if (addr == 0xCC00100A) {
                // W1C: writing 1 to a bit clears it
                g_pe_sr &= ~val;
            }
        }
    );
}

} // namespace nwii::runtime::hw
