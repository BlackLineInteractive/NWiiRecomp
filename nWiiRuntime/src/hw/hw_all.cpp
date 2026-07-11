#include "runtime/hw/hw.h"

namespace nwii::runtime::hw {
extern void register_cp(MMIODispatcher &dispatcher);
void register_all_hw(MMIODispatcher &dispatcher) {
  register_cp(dispatcher);
  register_pi(dispatcher);
  register_pe(dispatcher);
  register_vi(dispatcher);
  register_dsp(dispatcher);
  register_exi(dispatcher);
  register_si(dispatcher);
  register_ai(dispatcher);
  register_di(dispatcher);
  register_ipc(dispatcher);
  register_mi(dispatcher);
}
} // namespace nwii::runtime::hw