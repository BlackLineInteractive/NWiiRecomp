#include "runtime/hw/hw.h"
#include "runtime/cpu_context.h"
#include <iostream>
#include <atomic>

namespace nwii::runtime {
extern CPUContext *g_ctx_ptr;
}

namespace nwii::runtime::hw {

// PE control register (0xCC00100A), Dolphin UPECtrlReg: bit 0 = token
// interrupt enable, bit 1 = finish interrupt enable, bits 2/3 = token/finish
// acknowledge (WRITE-ONLY — they must read back as 0, or a guest
// read-modify-write ack of one interrupt silently acks the other pending one
// as well and it is lost before dispatch).
static std::atomic<uint32_t> g_pe_ctrl = 0; // enable bits only (0x01|0x02)
static std::atomic<bool> g_pe_token_pending = false;
static std::atomic<bool> g_pe_finish_pending = false;
// Legacy mirror (bit 2 = token pending, bit 3 = finish pending) for externs.
std::atomic<uint32_t> g_pe_sr = 0;
// Last GXSetDrawSync token seen in the command stream (BP 0x47/0x48 payload);
// the PE ISR reads it back from 0xCC00100E to hand to the game's callback.
// Atomic: the parser writes it on the frame thread while the guest token ISR
// reads it on the CPU thread (TSan named this exact pair).
std::atomic<uint32_t> g_pe_token = 0;

// PI lines follow (pending && enabled), like Dolphin's UpdateInterrupts.
static void pe_update_interrupts() {
    g_pe_sr = (g_pe_token_pending ? 0x04u : 0u) | (g_pe_finish_pending ? 0x08u : 0u);
    if (g_pe_token_pending && (g_pe_ctrl & 0x01))
        trigger_pi_interrupt(0x200); // INT_CAUSE_PE_TOKEN
    else
        clear_pi_interrupt(0x200);
    if (g_pe_finish_pending && (g_pe_ctrl & 0x02))
        trigger_pi_interrupt(0x400); // INT_CAUSE_PE_FINISH
    else
        clear_pi_interrupt(0x400);
}

void register_pe(MMIODispatcher &dispatcher) {
    dispatcher.register_region(0xCC001000, 0xCC0010FF,
        [](uint32_t addr) -> uint32_t {
            // PE CTRL (0x0A): only the enable bits are readable. 0x1008 is
            // PE_ALPHAREAD — a different register; aliasing it here let an
            // AlphaRead write clobber the interrupt enables.
            if (addr == 0xCC00100A)
                return g_pe_ctrl.load();
            // PE_TOKEN value register.
            if (addr == 0xCC00100E)
                return g_pe_token & 0xFFFF;
            return 0;
        },
        [](uint32_t addr, uint32_t val) {
            if (addr == 0xCC00100A) {
                if (std::getenv("NWII_SAMPLE")) {
                    static int n = 0;
                    if (n++ < 12)
                        std::cout << "[PE] ctrl write @" << std::hex << addr
                                  << " val=0x" << val << std::dec << "\n";
                }
                if (val & 0x04) g_pe_token_pending = false;
                if (val & 0x08) g_pe_finish_pending = false;
                g_pe_ctrl = val & 0x03;
                pe_update_interrupts();
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
        g_pe_token_pending = true;
        pe_update_interrupts();
    }
}

void pe_signal_finish() {
    static int n = 0;
    if (n++ < 6)
        std::cout << "[PE] finish\n";
    g_pe_finish_pending = true;
    pe_update_interrupts();
}

} // namespace nwii::runtime::hw
