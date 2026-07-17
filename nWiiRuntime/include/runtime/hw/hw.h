#pragma once
#include "runtime/hw/mmio_dispatcher.h"
#include <cstdint>
#include <cstddef>
#include <atomic>

namespace nwii::runtime {
uint64_t get_os_time();
}

namespace nwii::runtime::hw {

extern std::atomic<uint32_t> pi_intsr;
extern std::atomic<uint32_t> pi_intmr;
extern std::atomic<uint32_t> g_pe_sr;       
extern int g_di_interrupt_delay; 

inline void trigger_pi_interrupt(uint32_t mask) {
  pi_intsr.fetch_or(mask, std::memory_order_relaxed);
}
inline void clear_pi_interrupt(uint32_t mask) {
  pi_intsr.fetch_and(~mask, std::memory_order_relaxed);
}

void vi_trigger_interrupt();
void ipc_post_reply(uint32_t req_addr); 
void ai_update();
void dsp_trigger_interrupt();

int dsp_pending_os_interrupt();
void di_tick();  

size_t dsp_audio_pull(int16_t* out, size_t frames);
void pe_signal_token(uint32_t token, bool raise_interrupt); 
void pe_signal_finish();                                    

void register_pi(MMIODispatcher &dispatcher);

void pi_fifo_get(uint32_t &base, uint32_t &end, uint32_t &wptr);
void pi_fifo_set_wptr(uint32_t wptr);

uint32_t cp_fifo_base_reg();
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

} 