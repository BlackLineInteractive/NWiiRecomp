#include "runtime/hw/hw.h"
#include "runtime/cpu_context.h"
#include <iostream>

namespace nwii::runtime {
extern CPUContext *g_ctx_ptr;
}

namespace nwii::runtime::hw {

// PE status register: tracks TOKEN (bit 2) and FINISH (bit 3) flags.
// Set by the GX FIFO capture when the game's own command stream carries a
// draw-done (BP 0x45, low nibble 2) or token (BP 0x47/0x48); cleared W1C by
// the OS PE ISR writing 0xCC00100A.
uint32_t g_pe_sr = 0;
// Last GXSetDrawSync token seen in the command stream (BP 0x47/0x48 payload);
// the PE ISR reads it back from 0xCC00100E to hand to the game's callback.
uint32_t g_pe_token = 0;

void register_pe(MMIODispatcher &dispatcher) {
    dispatcher.register_region(0xCC001000, 0xCC0010FF,
        [](uint32_t addr) -> uint32_t {
            // PE_SR at 0xCC00100A: bit 2 = PE_TOKEN, bit 3 = PE_FINISH.
            if (addr == 0xCC00100A || addr == 0xCC001008)
                return g_pe_sr;
            // PE_TOKEN value register.
            if (addr == 0xCC00100E || addr == 0xCC00100C)
                return g_pe_token & 0xFFFF;
            return 0;
        },
        [](uint32_t addr, uint32_t val) {
            if (addr == 0xCC00100A || addr == 0xCC001008) {
                // W1C on the status bits; also drop the matching PI causes so
                // the acked interrupt doesn't redispatch.
                if (val & 0x04) { g_pe_sr &= ~0x04; clear_pi_interrupt(0x200); }
                if (val & 0x08) { g_pe_sr &= ~0x08; clear_pi_interrupt(0x400); }
            }
        }
    );
}

// Called by the GX write-gather capture when the guest command stream
// signals the pixel engine. Models an instant GPU: the token/finish becomes
// visible the moment the game submits it.
void pe_signal_token(uint32_t token, bool raise_interrupt) {
    static int n = 0;
    if (n++ < 6)
        std::cout << "[PE] token 0x" << std::hex << token << (raise_interrupt ? " +int" : "") << std::dec << "\n";
    g_pe_token = token;
    if (raise_interrupt) {
        g_pe_sr |= 0x04;
        trigger_pi_interrupt(0x200); // INT_CAUSE_PE_TOKEN
    }
}

void pe_signal_finish() {
    static int n = 0;
    if (n++ < 6)
        std::cout << "[PE] finish\n";
    g_pe_sr |= 0x08;
    trigger_pi_interrupt(0x400); // INT_CAUSE_PE_FINISH
}

} // namespace nwii::runtime::hw
