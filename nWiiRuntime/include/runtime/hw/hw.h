#pragma once
#include <cstdint>
#include "runtime/mmio_dispatcher.h"

namespace nwii::runtime {
    uint64_t get_os_time();
}

namespace nwii::runtime::hw {

extern uint32_t pi_intsr;
extern uint32_t pi_intmr;
inline void trigger_pi_interrupt(uint32_t mask) { pi_intsr |= mask; /* TODO: trigger CPU interrupt if (pi_intsr & pi_intmr) != 0 */ }
inline void clear_pi_interrupt(uint32_t mask) { pi_intsr &= ~mask; }

void register_pi(MMIODispatcher& dispatcher);
void register_vi(MMIODispatcher& dispatcher);
void register_dsp(MMIODispatcher& dispatcher);
void register_exi(MMIODispatcher& dispatcher);
void register_si(MMIODispatcher& dispatcher);
void register_ai(MMIODispatcher& dispatcher);
void register_di(MMIODispatcher& dispatcher);
void register_ipc(MMIODispatcher& dispatcher);

void register_all_hw(MMIODispatcher& dispatcher);

} // namespace nwii::runtime::hw
