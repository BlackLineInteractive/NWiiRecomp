#include "runtime/hw/hw.h"
#include "runtime/config.h"
#include "runtime/cpu_context.h"
#include <iostream>
#include <cstring>
#include <algorithm>
#include <mutex>
#include <vector>

namespace nwii::runtime {
    extern MMU* g_mmu;
    extern CPUContext *g_ctx_ptr;
    extern uint64_t get_os_time(); // wall-clock microseconds
}

#include "runtime/event_scheduler.h"

namespace nwii::runtime::hw {

static uint16_t dsp_mbox_cpu_hi = 0;
static uint16_t dsp_mbox_cpu_lo = 0;
static uint16_t dsp_mbox_dsp_hi = 0;
static uint16_t dsp_mbox_dsp_lo = 0;
static uint32_t ar_mmaddr = 0;
static uint32_t ar_araddr = 0;
static uint32_t ar_cnt = 0;
// DSPCSR bit layout (Dolphin UDSPControl): RES=0x0001, PIINT=0x0002,
// HALT=0x0004(HW)/0x0800(SDK view kept as before), and three interrupt
// pairs where the mask sits directly left of the status bit:
//   AID  (audio DMA done) status 0x0008 / mask 0x0010
//   ARAM (ARAM DMA done)  status 0x0020 / mask 0x0040
//   DSP  (mailbox)        status 0x0080 / mask 0x0100
static constexpr uint16_t CSR_INT_BITS = 0x00A8; // AID|ARAM|DSP statuses
static uint16_t dsp_control = 0x0800; // HW boots with DSP HALTED
static uint16_t ar_size = 0;    // 0xCC005012 AR_INFO/AR_SIZE
static uint16_t ar_mode = 0;    // 0xCC005016 AR_MODE
static uint16_t ar_refresh = 0; // 0xCC00501A AR_REFRESH
// Wall-clock deadline for the deferred DSP-mail acknowledge (see the mail
// write handler). A real DSP mixes one 5ms audio frame per command list;
// acking faster makes the game immediately prepare the next frame, and the
// audio pump then eats nearly all guest CPU time (observed: ~500 mails/s
// and 0.13 game-frames/s on MP7 with a backedge-tick countdown).
static void dsp_update_pi();

// ---- Audio DMA (0xCC005030-0xCC00503A) ----
// The final mixed output of the console: the game points this DMA at a
// PCM buffer in main RAM (s16 big-endian, interleaved stereo, 32kHz) and
// the hardware streams it to the DAC, raising the AID interrupt each time
// a buffer finishes so the mixer refills it. We pace it with wall-clock
// time and hand the samples to the host audio output through a ring.
static uint32_t adma_addr = 0;      // source address in main RAM
static uint16_t adma_control = 0;   // bit15 = enable, bits 0-14 = 32-byte blocks
static uint64_t s_adma_event_id = 0;
static std::mutex adma_mutex;
static std::vector<int16_t> adma_ring; // interleaved L,R host-endian

// Pulls up to `frames` stereo frames for the host audio backend; pads
// with silence when the game hasn't produced enough.
size_t dsp_audio_pull(int16_t* out, size_t frames) {
    std::lock_guard<std::mutex> lock(adma_mutex);
    size_t have = adma_ring.size() / 2;
    size_t n = have < frames ? have : frames;
    std::memcpy(out, adma_ring.data(), n * 2 * sizeof(int16_t));
    adma_ring.erase(adma_ring.begin(), adma_ring.begin() + n * 2);
    if (n < frames)
        std::memset(out + n * 2, 0, (frames - n) * 2 * sizeof(int16_t));
    return n;
}

static void adma_event_cb(CPUContext& ctx, uint64_t late) {
    if (!(adma_control & 0x8000))
        return;
    uint32_t blocks = adma_control & 0x7FFF;
    uint32_t bytes = blocks * 32;
    if (g_mmu && bytes > 0) {
        std::lock_guard<std::mutex> lock(adma_mutex);
        // Cap the ring at ~1 second so a paused host doesn't grow it.
        if (adma_ring.size() < 32000 * 2) {
            uint32_t va = adma_addr | 0x80000000;
            for (uint32_t i = 0; i + 3 < bytes; i += 4) {
                adma_ring.push_back((int16_t)g_mmu->read16(va + i));
                adma_ring.push_back((int16_t)g_mmu->read16(va + i + 2));
            }
        }
    }
    // Buffer consumed: AID interrupt tells the mixer to refill/flip.
    dsp_control |= 0x0008;
    dsp_update_pi();
}

// PI cause bit 6 (0x40) follows the OR of (status & its mask) across the
// three DSP subsystem sources — Dolphin's (csr >> 1) & csr trick.
static void dsp_update_pi() {
    if ((dsp_control >> 1) & dsp_control & CSR_INT_BITS)
        trigger_pi_interrupt(0x40);
    else
        clear_pi_interrupt(0x40);
}

// Which __OSInterruptTable index the pending DSPCSR sub-cause maps to.
// The guest __OSDispatchInterrupt fans PI cause bit 6 out into OS
// interrupts 5 (DSP_AI), 6 (DSP_ARAM), 7 (DSP_DSP mailbox) by reading
// DSPCSR; our leaf dispatch must do the same or an ARAM completion would
// invoke __DSPHandler, which treats any stale mail as a task resume and
// calls an unregistered callback (observed WILD JUMP to 0x81800000).
int dsp_pending_os_interrupt() {
    uint16_t pending = (dsp_control >> 1) & dsp_control & CSR_INT_BITS;
    if (pending & 0x0080) return 7; // DSP mailbox
    if (pending & 0x0020) return 6; // ARAM DMA
    if (pending & 0x0008) return 5; // AID (audio DMA)
    return 7;
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
                // Leaving it set let any later CSR write (mask bits) re-raise
                // PI 0x40 with an empty mailbox, parking __DSPHandler forever
                // in its entry mail-poll with EE off.
                dsp_control &= ~0x0080;
                dsp_update_pi();
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
            if (addr == 0xCC005030) return adma_addr >> 16;
            if (addr == 0xCC005032) return adma_addr & 0xFFFF;
            if (addr == 0xCC005036) return adma_control;
            if (addr == 0xCC00503A) {
                // AUDIO_DMA_BLOCKS_LEFT: approximate from the time left in
                // the current buffer (some SDKs poll it for sync).
                if (!(adma_control & 0x8000)) return 0;
                return 0; // Scheduler paces it perfectly, can return 0
            }
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
                // "resumed" branch sets). Acknowledge each CPU mail with the
                // resume mail, but AFTER a delay: an instant ack made the
                // audio task submit the next frame immediately, an unbounded
                // ping-pong that starved the rest of the game. Before the
                // mask is set (boot probes) stay silent: raising the
                // interrupt with no task installed jumped to garbage.
                // Pace: one ack per real DSP audio frame (5ms wall-clock),
                // measured from the previous ack so bursts don't accumulate.
                if (dsp_control & 0x0100) {
                    EventScheduler::get().schedule_after(g_ctx_ptr->tb_freq / 200, [](CPUContext& c, uint64_t){
                        dsp_mbox_dsp_hi = 0xDCD1;
                        dsp_mbox_dsp_lo = 0x0000;
                        dsp_control |= 0x0080;
                        dsp_update_pi();
                    });
                }
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
                // Interrupt statuses are write-1-to-clear; everything else
                // (masks, halt, init flags) comes from the written value.
                // RES (0x0001) self-clears and is never stored.
                uint16_t kept_ints = dsp_control & CSR_INT_BITS & ~val16;
                bool was_halted = (dsp_control & 0x0800) != 0;
                bool is_halted = (val16 & 0x0800) != 0;
                dsp_control = kept_ints | (val16 & ~(CSR_INT_BITS | 0x0001));
                // DSP reset/boot: post the 0xDCD10000 "init done" mail so the
                // game can POLL for it. We deliberately do NOT raise the DSP
                // mail interrupt here: __DSPHandler would dispatch the current
                // DSP task's callback, which is uninitialised until the game
                // uploads a real task (observed: NFS HP2 jumped to a garbage
                // 0x81800000 callback). Mail-driven audio comes online once a
                // task is registered; boot only needs the polled ack.
                if ((val16 & 0x0001) || (was_halted && !is_halted)) {
                    dsp_mbox_dsp_hi = 0xDCD1;
                    dsp_mbox_dsp_lo = 0x0000;
                }
                dsp_update_pi();
            } else if (addr == 0xCC005012) {
                ar_size = val & 0xFFFF;
            } else if (addr == 0xCC005016) {
                ar_mode = val & 0xFFFF;
            } else if (addr == 0xCC00501A) {
                ar_refresh = val & 0xFFFF;
            } else if (addr == 0xCC005030) {
                adma_addr = (adma_addr & 0x0000FFFF) | ((val & 0xFFFF) << 16);
            } else if (addr == 0xCC005032) {
                adma_addr = (adma_addr & 0xFFFF0000) | (val & 0xFFFF);
            } else if (addr == 0xCC005036) {
                bool was_on = (adma_control & 0x8000) != 0;
                adma_control = val & 0xFFFF;
                if (!was_on && (adma_control & 0x8000)) {
                    if (s_adma_event_id != 0) {
                        EventScheduler::get().cancel(s_adma_event_id);
                    }
                    s_adma_event_id = EventScheduler::get().schedule_recurring(g_ctx_ptr->tb_freq / 200, adma_event_cb);
                } else if (was_on && !(adma_control & 0x8000)) {
                    if (s_adma_event_id != 0) {
                        EventScheduler::get().cancel(s_adma_event_id);
                        s_adma_event_id = 0;
                    }
                }
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
                    if (mm < mem1_size && ar_a < mem2_size) {
                        uint32_t copy_count = std::min({count, mem1_size - mm, mem2_size - ar_a});
                        if (!dir) std::memcpy(mem2 + ar_a, mem1 + mm, copy_count);
                        else std::memcpy(mem1 + mm, mem2 + ar_a, copy_count);
                    }
                }
                // DMA completes synchronously; raise the ARAM-done status.
                // With the mask enabled this asserts PI 0x40, which the leaf
                // dispatch routes to OS interrupt 6 (__ARHandler / ARQ), not
                // to __DSPHandler.
                ar_cnt = 0;
                dsp_control |= 0x0020;
                dsp_update_pi();
            }
        }
    );
}}

void dsp_trigger_interrupt() {
    if (dsp_control & 0x0100) {
        dsp_control |= 0x0080;
        trigger_pi_interrupt(0x40);
    }
}

} // namespace nwii::runtime::hw
