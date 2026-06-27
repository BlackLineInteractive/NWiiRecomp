#include "runtime/hw/hw.h"
#include "runtime/config.h"
#include "runtime/cpu_context.h"
#include <iostream>
#include <cstring>
#include <algorithm>

namespace nwii::runtime::hw {

static uint32_t ipc_arm_msg = 0;
static uint32_t ipc_arm_ctrl = 0;
static uint32_t ipc_ppc_ctrl = 0;
uint32_t ipc_ppc_msg = 0;
extern CPUContext* g_ctx_ptr;
extern MMU* g_mmu;
extern "C" void handle_ios_ipc(nwii::runtime::CPUContext& ctx, uint32_t request_addr);

void ipc_dispatch_request(CPUContext &ctx, uint32_t req_addr) {
  uint32_t virt_addr = req_addr;
  if (virt_addr < 0x01800000)
    virt_addr |= 0x80000000;
  else if (virt_addr >= 0x10000000 && virt_addr < 0x14000000)
    virt_addr = (virt_addr & 0x03FFFFFF) | 0x90000000;

  handle_ios_ipc(ctx, virt_addr);

  ipc_arm_msg  = req_addr & 0x1FFFFFFF;
  ipc_arm_ctrl = 0x00000003; // Y1 | Y2
  trigger_pi_interrupt(0x00004000); // INT_CAUSE_WII_IPC
}

void hle_set_ipc_arm_msg(uint32_t req_addr) {
  ipc_arm_msg = req_addr & 0x1FFFFFFF;
  ipc_arm_ctrl = 0x00000002;
  ipc_ppc_ctrl |= 0x00000004;
  trigger_pi_interrupt(0x00001000);
}


void register_ipc(MMIODispatcher& dispatcher) {{
    dispatcher.register_region(0xCD000000, 0xCD00FFFF, 
        [](uint32_t addr) -> uint32_t {
            switch (addr & 0x00FFFFFF) {
            case 0x000000: return 0;
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
                ipc_ppc_ctrl = (ipc_ppc_ctrl & ~0x30) | (val & 0x30);
                if (val & 0x01) {
                    ipc_ppc_ctrl |= 0x01;
                    if (g_ctx_ptr) {
                        ipc_dispatch_request(*g_ctx_ptr, ipc_ppc_msg);
                    }
                    if (g_mmu && g_ctx_ptr) {
                        uint32_t req_vaddr = ipc_ppc_msg | 0x80000000;
                        g_mmu->write32(req_vaddr + 4, 0);
                        ipc_arm_msg = ipc_ppc_msg;
                        uint32_t ipc_handler = g_mmu->read32(0x80003040 + 27*4);
                        if (ipc_handler != 0 && ipc_handler != 0xFFFFFFFF) {
                            ipc_ppc_ctrl &= ~0x01;
                            ipc_ppc_ctrl |= 0x06;
                            ipc_arm_ctrl = 0x00000003;
                            trigger_pi_interrupt(0x00000010);
                            g_ctx_ptr->queue_callback(ipc_handler, 27, 0);
                        } else {
                            ipc_arm_ctrl = 0;
                            ipc_ppc_ctrl &= ~0x01;
                            clear_pi_interrupt(0x00000010);
                        }
                    }
                }
                if (val & 0x02) ipc_ppc_ctrl &= ~0x02;
                if (val & 0x08) ipc_ppc_ctrl &= ~0x04;
                break;
            }
            case 0x00000C:
                ipc_arm_ctrl &= ~(val & 0x03);
                if (!(ipc_arm_ctrl & 3))
                    clear_pi_interrupt(0x00004000);
                break;
            }
        }
    );
    dispatcher.register_region(0x0D000000, 0x0D00FFFF, 
        [&dispatcher](uint32_t a) { return dispatcher.read32(a | 0xC0000000); },
        [&dispatcher](uint32_t a, uint32_t v) { dispatcher.write32(a | 0xC0000000, v); }
    );
}}

} // namespace nwii::runtime::hw
