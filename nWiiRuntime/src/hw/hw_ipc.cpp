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
    uint32_t g_last_ipc_r13 = 0;
    uint32_t g_last_ipc_priority = 16;
}

namespace nwii::runtime::hw {

static uint32_t ipc_arm_msg = 0;
static uint32_t ipc_arm_ctrl = 0;
static uint32_t ipc_ppc_ctrl = 0;
uint32_t ipc_ppc_msg = 0;


extern "C" void handle_ios_ipc(nwii::runtime::CPUContext& ctx, uint32_t request_addr);

void dispatch_ipc(CPUContext& ctx, uint32_t virt_addr) {
  handle_ios_ipc(ctx, virt_addr);

  ipc_arm_msg  = virt_addr & 0x1FFFFFFF;
  ipc_arm_ctrl = 0x00000003; // Y1 | Y2
  g_ipc_interrupt_delay = 5000;
}

void ipc_dispatch_request(CPUContext &ctx, uint32_t req_addr) {
  uint32_t virt_addr = req_addr;
  if ((virt_addr & 0xF0000000) == 0xC0000000) virt_addr = (virt_addr & 0x0FFFFFFF) | 0x80000000;
  if ((virt_addr & 0xF0000000) == 0xD0000000) virt_addr = (virt_addr & 0x0FFFFFFF) | 0x90000000;
  
  nwii::runtime::g_last_ipc_r13 = ctx.gpr[13];
  uint32_t cur_thread = ctx.mmu.read32(0x800000E4);
  if (cur_thread != 0) {
      nwii::runtime::g_last_ipc_priority = ctx.mmu.read32(cur_thread + 720);
  }
  std::cout << "[HW IPC] Saving r13=0x" << std::hex << nwii::runtime::g_last_ipc_r13 << std::dec << " prio=" << nwii::runtime::g_last_ipc_priority << std::endl;
  
  dispatch_ipc(ctx, virt_addr);
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
                
                // Update Interrupt Enables
                ipc_ppc_ctrl = (ipc_ppc_ctrl & ~0x30) | (val & 0x30);
                
                if (val & 0x01) {
                    ipc_ppc_ctrl |= 0x01; // Set X1
                    if (g_ctx_ptr) {
                        ipc_dispatch_request(*g_ctx_ptr, ipc_ppc_msg);
                    }
                    ipc_ppc_ctrl &= ~0x01; // Starlet clears X1
                    // Starlet ACKs with 0x22 (Finished current request and wake up thread)
                    // and 0x04 (Ready for next request)
                    ipc_ppc_ctrl |= 0x26; 
                }
                
                if (val & 0x04) ipc_ppc_ctrl &= ~0x04; // Broadway clears X2
                if (val & 0x02) ipc_ppc_ctrl &= ~0x02; // Broadway clears Y1
                if (val & 0x08) ipc_ppc_ctrl &= ~0x08; // Broadway clears Y2
                
                std::cout << "[HW IPC] PPC_CTRL write: val=0x" << std::hex << val << " old=0x" << old_ctrl << " new=0x" << ipc_ppc_ctrl << " PC=0x" << (g_ctx_ptr ? g_ctx_ptr->pc : 0) << " LR=0x" << (g_ctx_ptr ? g_ctx_ptr->lr : 0) << std::dec << std::endl;
                break;
            }
            case 0x00000C: {
                uint32_t old_ctrl = ipc_arm_ctrl;
                ipc_arm_ctrl &= ~(val & 0x03);
                if (!(ipc_arm_ctrl & 3))
                    clear_pi_interrupt(0x00004000);
                std::cout << "[HW IPC] ARM_CTRL write: val=0x" << std::hex << val << " old=0x" << old_ctrl << " new=0x" << ipc_arm_ctrl << std::dec << std::endl;
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
