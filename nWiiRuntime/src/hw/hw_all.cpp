#include "runtime/hw/hw.h"

namespace nwii::runtime::hw {
void register_all_hw(MMIODispatcher &dispatcher) {
  register_pi(dispatcher);
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