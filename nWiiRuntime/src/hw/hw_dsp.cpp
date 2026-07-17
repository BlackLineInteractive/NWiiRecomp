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
    extern uint64_t get_os_time(); 
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



static constexpr uint16_t CSR_INT_BITS = 0x00A8; 
static uint16_t dsp_control = 0x0800; 
static uint16_t ar_size = 0;    
static uint16_t ar_mode = 0;    
static uint16_t ar_refresh = 0; 



static void dsp_update_pi();



static void ar_dma_execute() {
    bool dir = (ar_cnt & 0x80000000) != 0;
    uint32_t count = ar_cnt & 0x7FFFFFFF;
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

    

    
    
    EventScheduler::get().schedule_after(
        (uint64_t)count / 2 + 64, [](CPUContext&, uint64_t) {
            ar_cnt = 0;
            dsp_control |= 0x0020;
            dsp_update_pi();
        });
}





static uint32_t adma_addr = 0;      
static uint16_t adma_control = 0;   
static uint64_t s_adma_event_id = 0;
static std::mutex adma_mutex;
static std::vector<int16_t> adma_ring; 


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
        
        if (adma_ring.size() < 32000 * 2) {
            uint32_t va = adma_addr | 0x80000000;
            for (uint32_t i = 0; i + 3 < bytes; i += 4) {
                adma_ring.push_back((int16_t)g_mmu->read16(va + i));
                adma_ring.push_back((int16_t)g_mmu->read16(va + i + 2));
            }
        }
    }
    
    dsp_control |= 0x0008;
    dsp_update_pi();
}

// three DSP subsystem sources — Dolphin's (csr >> 1) & csr trick.
static void dsp_update_pi() {
    if ((dsp_control >> 1) & dsp_control & CSR_INT_BITS)
        trigger_pi_interrupt(0x40);
    else
        clear_pi_interrupt(0x40);
}





int dsp_pending_os_interrupt() {
    uint16_t pending = (dsp_control >> 1) & dsp_control & CSR_INT_BITS;
    if (pending & 0x0080) return 7; 
    if (pending & 0x0020) return 6; 
    if (pending & 0x0008) return 5; 
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

            
            if (addr == 0xCC005016) return ar_mode | 0x0001;
            if (addr == 0xCC00501A) return ar_refresh;
            if (addr == 0xCC005020) return ar_mmaddr;
            if (addr == 0xCC005024) return ar_araddr;
            if (addr == 0xCC005028) return ar_cnt;
            if (addr == 0xCC005030) return adma_addr >> 16;
            if (addr == 0xCC005032) return adma_addr & 0xFFFF;
            if (addr == 0xCC005036) return adma_control;
            if (addr == 0xCC00503A) {

                if (!(adma_control & 0x8000)) return 0;
                return 0; 
            }
            return 0;
        },
        [](uint32_t addr, uint32_t val) {
            static const bool artrace = std::getenv("NWII_ARTRACE") != nullptr;
            if (artrace && (addr & 0xFFF0) >= 0x5010 && (addr & 0xFFF0) < 0x5030)
                std::cout << "[ARw] 0x" << std::hex << (addr & 0xFFFF)
                          << " = 0x" << val << std::dec << "\n";
            if (addr == 0xCC005000 || addr == 0xCC005002) {
                
                // and speak the AX ucode protocol (Dolphin AXUCode::HandleMail):

                

                

                
                
                static uint16_t s_cpu_mail_hi = 0;
                static int s_ax_state = 0; 
                bool completed = (addr == 0xCC005002);
                if (addr == 0xCC005000)
                    s_cpu_mail_hi = val & 0xFFFF;
                dsp_mbox_cpu_hi = (val & 0x7FFF) | 0x8000;
                dsp_mbox_cpu_hi &= ~0x8000;
                if (completed && (dsp_control & 0x0100)) {
                    uint32_t mail = ((uint32_t)s_cpu_mail_hi << 16) | (val & 0xFFFF);
                    auto reply = [](uint16_t lo) {
                        EventScheduler::get().schedule_after(
                            g_ctx_ptr->tb_freq / 1000, [lo](CPUContext&, uint64_t) {
                                dsp_mbox_dsp_hi = 0xDCD1;
                                dsp_mbox_dsp_lo = lo;
                                dsp_control |= 0x0080;
                                dsp_update_pi();
                            });
                    };
                    if (s_ax_state == 1) {
                        
                        reply(0x0002); 
                        s_ax_state = 2;
                    } else if ((mail & 0xFFFF0000u) == 0xBABE0000u) {
                        s_ax_state = 1;
                    } else if (mail == 0xCDD10001u) {
                        reply(0x0001); 
                        s_ax_state = 0;
                    } else if ((mail & 0xFFFF0000u) == 0xCDD10000u) {
                        s_ax_state = 0; 
                        reply(0x0000);
                    } else {

                        reply(0x0000);
                    }
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

                
                uint16_t kept_ints = dsp_control & CSR_INT_BITS & ~val16;
                bool was_halted = (dsp_control & 0x0800) != 0;
                bool is_halted = (val16 & 0x0800) != 0;
                dsp_control = kept_ints | (val16 & ~(CSR_INT_BITS | 0x0001));

                

                
                
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
                ar_dma_execute();
            }
        },
        
        // (Dolphin DSP.cpp: MMADDR_H/L, ARADDR_H/L, CNT_H/L) and the SDK's

        

        
        [](uint32_t addr) -> uint16_t {
            if (addr == 0xCC005020) return (uint16_t)(ar_mmaddr >> 16);
            if (addr == 0xCC005022) return (uint16_t)ar_mmaddr;
            if (addr == 0xCC005024) return (uint16_t)(ar_araddr >> 16);
            if (addr == 0xCC005026) return (uint16_t)ar_araddr;
            if (addr == 0xCC005028) return (uint16_t)(ar_cnt >> 16);
            if (addr == 0xCC00502A) return (uint16_t)ar_cnt;
            return (uint16_t)MMIODispatcher::get().read32(addr);
        },
        [](uint32_t addr, uint16_t val) {
            static const bool artrace = std::getenv("NWII_ARTRACE") != nullptr;
            if (artrace && (addr & 0xFFFF) >= 0x5020 && (addr & 0xFFFF) < 0x5030)
                std::cout << "[ARw16] 0x" << std::hex << (addr & 0xFFFF)
                          << " = 0x" << val << std::dec << "\n";
            if      (addr == 0xCC005020) ar_mmaddr = (ar_mmaddr & 0x0000FFFF) | ((uint32_t)val << 16);
            else if (addr == 0xCC005022) ar_mmaddr = (ar_mmaddr & 0xFFFF0000) | val;
            else if (addr == 0xCC005024) ar_araddr = (ar_araddr & 0x0000FFFF) | ((uint32_t)val << 16);
            else if (addr == 0xCC005026) ar_araddr = (ar_araddr & 0xFFFF0000) | val;
            else if (addr == 0xCC005028) ar_cnt = (ar_cnt & 0x0000FFFF) | ((uint32_t)val << 16);
            else if (addr == 0xCC00502A) {
                ar_cnt = (ar_cnt & 0xFFFF0000) | val;
                ar_dma_execute();
            }
            else MMIODispatcher::get().write32(addr, val);
        }
    );
}}

void dsp_trigger_interrupt() {
    if (dsp_control & 0x0100) {
        dsp_control |= 0x0080;
        trigger_pi_interrupt(0x40);
    }
}

} 
