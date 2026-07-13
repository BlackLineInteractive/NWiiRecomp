#pragma once
#include "runtime/hw/mmio_dispatcher.h"
#include <cstdint>
#include <atomic>

namespace nwii::runtime {
uint64_t get_os_time();
}

namespace nwii::runtime::hw {

extern std::atomic<uint32_t> pi_intsr;
extern std::atomic<uint32_t> pi_intmr;
extern std::atomic<uint32_t> g_pe_sr;       // PE status register (TOKEN/FINISH bits), W1C
extern int g_di_interrupt_delay; // countdown to DI completion interrupt

inline void trigger_pi_interrupt(uint32_t mask) {
  pi_intsr.fetch_or(mask, std::memory_order_relaxed);
}
inline void clear_pi_interrupt(uint32_t mask) {
  pi_intsr.fetch_and(~mask, std::memory_order_relaxed);
}

void vi_trigger_interrupt();
void ipc_post_reply(uint32_t req_addr); // complete a deferred (IPC_NO_REPLY) request
void ai_update();
void dsp_trigger_interrupt();
// Maps the pending DSPCSR sub-cause to its __OSInterruptTable index:
// 5 = AID (audio DMA), 6 = ARAM DMA, 7 = DSP mailbox.
int dsp_pending_os_interrupt();
void di_tick();  // counts down g_di_interrupt_delay, raises the DI IRQ at zero
void dsp_tick(); // counts down the deferred DSP-mail ack, raises PI 0x40
void pe_signal_token(uint32_t token, bool raise_interrupt); // GX stream saw BP 0x47/0x48
void pe_signal_finish();                                    // GX stream saw draw-done (BP 0x45)

void register_pi(MMIODispatcher &dispatcher);
void register_pe(MMIODispatcher &dispatcher);
void register_vi(MMIODispatcher &dispatcher);
void register_dsp(MMIODispatcher &dispatcher);
void register_exi(MMIODispatcher &dispatcher);
void register_si(MMIODispatcher &dispatcher);
void register_ai(MMIODispatcher &dispatcher);
void register_di(MMIODispatcher &dispatcher);
void register_ipc(MMIODispatcher &dispatcher);
void register_mi(MMIODispatcher &dispatcher);

void register_all_hw(MMIODispatcher &dispatcher);

} // namespace nwii::runtime::hw