#include "runtime/hw/hw.h"
#include "runtime/cpu_context.h"
#include <iostream>

namespace nwii::runtime {
extern CPUContext *g_ctx_ptr;
}

namespace nwii::runtime::hw {


void register_pe(MMIODispatcher &dispatcher) {
    dispatcher.register_region(0xCC001000, 0xCC0010FF,
        [](uint32_t addr) -> uint32_t {
            if (addr == 0xCC00100A) {
                if (g_ctx_ptr) {
                    uint32_t r13 = g_ctx_ptr->gpr[13];
                    if (r13 != 0) {
                        uint32_t gx_data_ptr = g_ctx_ptr->mmu.read32(r13 - 9112);
                        if (gx_data_ptr != 0) {
                            return g_ctx_ptr->mmu.read16(gx_data_ptr + 14);
                        }
                    }
                }
                return 0;
            }
            return 0;
        },
        [](uint32_t addr, uint32_t val) {
            // Ignore writes to PE for now
        }
    );
}

} // namespace nwii::runtime::hw
