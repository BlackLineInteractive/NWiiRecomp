#include "runtime/hw/hw.h"
#include "runtime/config.h"
#include "runtime/cpu_context.h"
#include <iostream>
#include <cstring>
#include <algorithm>

namespace nwii::runtime {
    extern CPUContext* g_ctx_ptr;
    extern MMU* g_mmu;
    extern int g_ipc_interrupt_delay;
}

namespace nwii::runtime::hw {

static uint32_t ipc_arm_msg = 0;
static uint32_t ipc_arm_ctrl = 0;
static uint32_t ipc_ppc_ctrl = 0;
uint32_t ipc_ppc_msg = 0;


extern "C" int32_t handle_ios_ipc(nwii::runtime::CPUContext& ctx, uint32_t request_addr);

int32_t dispatch_ipc(CPUContext& ctx, uint32_t virt_addr) {
  int32_t result = handle_ios_ipc(ctx, virt_addr);

  ipc_arm_msg  = virt_addr & 0x1FFFFFFF;
  ipc_arm_ctrl = 0x00000003; // Y1 | Y2
  // g_ipc_interrupt_delay is handled in register_ipc now
  return result;
}

int32_t ipc_dispatch_request(CPUContext &ctx, uint32_t req_addr) {
  uint32_t virt_addr = req_addr;
  if ((virt_addr & 0xF0000000) == 0xC0000000) virt_addr = (virt_addr & 0x0FFFFFFF) | 0x80000000;
  if ((virt_addr & 0xF0000000) == 0xD0000000) virt_addr = (virt_addr & 0x0FFFFFFF) | 0x90000000;

  return dispatch_ipc(ctx, virt_addr);
}

void hle_set_ipc_arm_msg(uint32_t req_addr) {
  ipc_arm_msg = req_addr & 0x1FFFFFFF;
  ipc_arm_ctrl = 0x00000002;
  ipc_ppc_ctrl |= 0x00000004;
  g_ipc_interrupt_delay = 5000;
}


void register_ipc(MMIODispatcher& dispatcher) {{
    dispatcher.register_region(0xCD000000, 0xCD00FFFF, 
        [](uint32_t addr) -> uint32_t {
            switch (addr & 0x00FFFFFF) {
            case 0x000000: return ipc_ppc_msg;
            case 0x000004: return ipc_ppc_ctrl;
            case 0x000008: return ipc_arm_msg;
            case 0x00000C: return ipc_arm_ctrl;
            default: return 0;
            }
        },
        [](uint32_t addr, uint32_t val) {
            switch (addr & 0x00FFFFFF) {
            case 0x000000: {
                ipc_ppc_msg = val;
                break;
            }
            case 0x000004: {
                uint32_t old_ctrl = ipc_ppc_ctrl;

                // Update Interrupt Enable bits (IPC_INTE=0x20, IPC_OAT=0x10)
                ipc_ppc_ctrl = (ipc_ppc_ctrl & ~0x30) | (val & 0x30);

                if (val & 0x01) {
                    // Broadway sets X1 to send request to Starlet
                    ipc_ppc_ctrl |= 0x01;
                    int32_t result = 0;
                    if (g_ctx_ptr) {
                        // Process the IOS request synchronously (HLE Starlet)
                        result = ipc_dispatch_request(*g_ctx_ptr, ipc_ppc_msg);
                    }
                    if (result != -0x70000001) { // IPC_NO_REPLY
                        // Starlet clears X1 and sets Y1+X2 (reply ready + ARM ready for next cmd)
                        ipc_ppc_ctrl &= ~0x01;
                        ipc_ppc_ctrl |= 0x26; // Y1(0x02) + X2(0x04) + IPC_INTE(0x20)
                        // Trigger IPC PI interrupt after short delay
                        g_ipc_interrupt_delay = 50;
                    } else {
                        // Request is asynchronous, Starlet is not ready to reply yet.
                        // We still clear X1 (request accepted) and set X2 (ready for next request).
                        // Do not set Y1, and do not trigger PI interrupt!
                        ipc_ppc_ctrl &= ~0x01;
                        ipc_ppc_ctrl |= 0x04; // X2
                    }
                }

                if (val & 0x04) ipc_ppc_ctrl &= ~0x04; // Broadway clears X2
                if (val & 0x02) ipc_ppc_ctrl &= ~0x02; // Broadway clears Y1
                if (val & 0x08) ipc_ppc_ctrl &= ~0x08; // Broadway clears Y2
                (void)old_ctrl;
                break;
            }

            case 0x00000C: {
                ipc_arm_ctrl &= ~(val & 0x03);
                if (!(ipc_arm_ctrl & 3))
                    clear_pi_interrupt(0x00004000);
                break;
            }
            } // close switch
        }
    );
    dispatcher.register_region(0x0D000000, 0x0D00FFFF, 
        [&dispatcher](uint32_t a) { return dispatcher.read32(a | 0xC0000000); },
        [&dispatcher](uint32_t a, uint32_t v) { dispatcher.write32(a | 0xC0000000, v); }
    );
}}

} // namespace nwii::runtime::hw
