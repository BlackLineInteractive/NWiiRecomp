#include "runtime/hw/hw.h"
#include "runtime/config.h"
#include "runtime/cpu_context.h"
#include <iostream>
#include <cstring>
#include <algorithm>

namespace nwii::runtime {
    extern MMU* g_mmu;
}

namespace nwii::runtime::hw {

static uint16_t dsp_mbox_cpu_hi = 0;
static uint16_t dsp_mbox_cpu_lo = 0;
static uint16_t dsp_mbox_dsp_hi = 0;
static uint16_t dsp_mbox_dsp_lo = 0;
static uint32_t ar_mmaddr = 0;
static uint32_t ar_araddr = 0;
static uint32_t ar_cnt = 0;
static uint16_t dsp_control = 0x0800; // HW boots with DSP HALTED
static uint16_t ar_size = 0;    // 0xCC005012 AR_INFO/AR_SIZE
static uint16_t ar_mode = 0;    // 0xCC005016 AR_MODE
static uint16_t ar_refresh = 0; // 0xCC00501A AR_REFRESH
// Countdown to the deferred DSP-mail acknowledge (see the mail write
// handler); models the milliseconds a real DSP spends on an audio frame.
int g_dsp_mail_delay = 0;

// Called from the backedge callback pump: completes a pending mail ack.
void dsp_tick() {
    if (g_dsp_mail_delay > 0 && --g_dsp_mail_delay == 0) {
        // 0xDCD10000 = task resumed/init done: __DSPHandler's branch for it
        // marks the task started and invokes the task's resume callback,
        // which is what AX init blocks on. (0xDCD10002 is the yield path.)
        dsp_mbox_dsp_hi = 0xDCD1;
        dsp_mbox_dsp_lo = 0x0000;
        dsp_control |= 0x0008;
        if (dsp_control & 0x0010)
            trigger_pi_interrupt(0x40);
    }
}


void register_dsp(MMIODispatcher& dispatcher) {{
    dispatcher.register_region(0xCC005000, 0xCC0050FF,
        [](uint32_t addr) -> uint32_t {
            if (addr == 0xCC005000 || addr == 0xCC005002) return dsp_mbox_cpu_hi;
            if (addr == 0xCC005004) return dsp_mbox_dsp_hi;
            if (addr == 0xCC005006) {
                uint16_t val = dsp_mbox_dsp_lo;
                dsp_mbox_dsp_hi &= ~0x8000;
                // Mail consumed: drop the mailbox interrupt status with it.
                // Leaving 0x08 set let any later CSR write (mask bits) re-raise
                // PI 0x40 with an empty mailbox, parking __DSPHandler forever
                // in its entry mail-poll with EE off.
                dsp_control &= ~0x0008;
                clear_pi_interrupt(0x40);
                if (std::getenv("NWII_DSPTRACE"))
                    std::cout << "[DSPm] mail read lo=0x" << std::hex << val
                              << " (hi was 0x" << (dsp_mbox_dsp_hi | 0x8000)
                              << ") csr=0x" << dsp_control << std::dec << "\n";
                return val;
            }
            if (addr == 0xCC005008 || addr == 0xCC00500A) return dsp_control;
            if (addr == 0xCC005012) return ar_size;
            // AR_MODE bit 0 is the ARAM controller "ready" handshake the SDK
            // ARAM-init/size-detect routine polls for. Our ARAM has no init
            // latency, so it is always ready.
            if (addr == 0xCC005016) return ar_mode | 0x0001;
            if (addr == 0xCC00501A) return ar_refresh;
            if (addr == 0xCC005020) return ar_mmaddr;
            if (addr == 0xCC005024) return ar_araddr;
            if (addr == 0xCC005028) return ar_cnt;
            return 0;
        },
        [](uint32_t addr, uint32_t val) {
            if (addr == 0xCC005000 || addr == 0xCC005002) {
                dsp_mbox_cpu_hi = (val & 0x7FFF) | 0x8000;
                dsp_mbox_cpu_hi &= ~0x8000;
                dsp_mbox_dsp_hi = 0x8000;
                // Once the game unmasks the DSP-mailbox interrupt it has a
                // task registered and __DSPHandler's callback slots are live
                // (the AX init loop spins on a flag that only the handler's
                // 0xDCD10002 "resumed" branch sets). Acknowledge each CPU
                // mail with that resume mail, but AFTER a delay: an instant
                // ack made the audio task submit the next frame immediately,
                // an unbounded ping-pong that starved the rest of the game.
                // Before the mask is set (boot probes) stay silent: raising
                // the interrupt with no task installed jumped to garbage.
                if (dsp_control & 0x0010)
                    g_dsp_mail_delay = 15000;
                if (std::getenv("NWII_DSPTRACE"))
                    std::cout << "[DSPm] cpu mail write @" << std::hex
                              << (addr & 0xFFFF) << " val=0x" << val
                              << " csr=0x" << dsp_control << std::dec << "\n";
            } else if (addr == 0xCC005004 || addr == 0xCC005006) {
                if (val & 0x8000) dsp_mbox_dsp_hi &= ~0x8000;
            } else if (addr == 0xCC005008 || addr == 0xCC00500A) {
                uint16_t val16 = val & 0xFFFF;
                if (std::getenv("NWII_DSPTRACE"))
                    std::cout << "[DSPm] csr write val=0x" << std::hex
                              << (val & 0xFFFF) << " csr=0x" << dsp_control
                              << std::dec << "\n";
                if (!(val16 & 0x0008)) dsp_control &= ~0x0008;
                if (!(val16 & 0x0020)) {
                    dsp_control &= ~0x0020;
                    clear_pi_interrupt(0x80); // Clear PI_INT_ARAM
                }
                if (!(val16 & 0x0080)) dsp_control &= ~0x0080;
                // DSP reset/boot: post the 0xDCD10000 "init done" mail so the
                // game can POLL for it. We deliberately do NOT raise the DSP
                // mail interrupt here: __DSPHandler would dispatch the current
                // DSP task's callback, which is uninitialised until the game
                // uploads a real task (observed: NFS HP2 jumped to a garbage
                // 0x81800000 callback). Mail-driven audio comes online once a
                // task is registered; boot only needs the polled ack.
                if (val16 & 0x0001) {
                    dsp_mbox_dsp_hi = 0xDCD1;
                    dsp_mbox_dsp_lo = 0x0000;
                }
                bool was_halted = (dsp_control & 0x0800) != 0;
                bool is_halted = (val16 & 0x0800) != 0;
                dsp_control = (dsp_control & 0x00A8) | (val16 & ~0x00A9);
                if (was_halted && !is_halted) {
                    dsp_mbox_dsp_hi = 0xDCD1;
                    dsp_mbox_dsp_lo = 0x0000;
                }
                // Only the DSP-mailbox interrupt (status 0x08 & mask 0x10)
                // drives __DSPHandler. Do NOT raise it for ARAM-DMA completion
                // (status 0x20 & mask 0x40): our ARAM DMA finishes synchronously
                // and the SDK polls AR_DMA_CNT for it. Raising it invokes
                // __DSPHandler spuriously, which consumes the fake 0xDCD10000
                // boot mail as a DSP-task "resume" and calls an unregistered
                // task callback (garbage 0x81800000) -> WILD JUMP.
                bool pi_int = (dsp_control & 0x0008) && (dsp_control & 0x0010);
                if (pi_int) trigger_pi_interrupt(0x40);
                else clear_pi_interrupt(0x40);
            } else if (addr == 0xCC005012) {
                ar_size = val & 0xFFFF;
            } else if (addr == 0xCC005016) {
                ar_mode = val & 0xFFFF;
            } else if (addr == 0xCC00501A) {
                ar_refresh = val & 0xFFFF;
            } else if (addr == 0xCC005020) {
                ar_mmaddr = val;
            } else if (addr == 0xCC005024) {
                ar_araddr = val;
            } else if (addr == 0xCC005028) {
                ar_cnt = val;
                bool dir = (val & 0x80000000) != 0;
                uint32_t count = val & 0x7FFFFFFF;
                uint32_t mm = ar_mmaddr & 0x01FFFFFF;
                uint32_t ar_a = ar_araddr & 0x00FFFFFF;
                if (g_mmu && count > 0) {
                    uint8_t *mem1 = nwii::runtime::g_mmu->mem1.data();
                    uint8_t *mem2 = nwii::runtime::g_mmu->mem2.data();
                    uint32_t mem1_size = nwii::runtime::g_mmu->mem1.size();
                    uint32_t mem2_size = nwii::runtime::g_mmu->mem2.size();
                    
                    if (mm <= 0x30AC && mm + count > 0x30AC) {
                        std::cout << "[DSP DMA] Corrupting IPC Handler! dir=" << dir << " mm=0x" << std::hex << mm << " ar_a=0x" << ar_a << " count=0x" << count << std::dec << "\n";
                    }

                    if (mm < mem1_size && ar_a < mem2_size) {
                        uint32_t copy_count = std::min({count, mem1_size - mm, mem2_size - ar_a});
                        if (!dir) std::memcpy(mem2 + ar_a, mem1 + mm, copy_count);
                        else std::memcpy(mem1 + mm, mem2 + ar_a, copy_count);
                    }
                }
                ar_cnt = 0;
                dsp_control |= 0x0020;
                if (dsp_control & 0x0040) {
                    trigger_pi_interrupt(0x80); // PI_INT_ARAM is 0x80
                }
            }
        }
    );
}}

} // namespace nwii::runtime::hw
namespace nwii::runtime::hw {
void dsp_trigger_interrupt() {
    if (dsp_control & 0x0010) {
        dsp_control |= 0x0008;
        trigger_pi_interrupt(0x40);
    }
}
}
