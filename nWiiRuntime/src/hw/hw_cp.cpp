#include "runtime/hw/hw.h"
#include "runtime/cpu_context.h"
#include <iostream>

namespace nwii::runtime {
extern CPUContext *g_ctx_ptr;
void GX_WGPIPE_Write8(uint8_t val);
}

namespace nwii::runtime::hw {

static uint16_t g_cp_cr = 0;
static uint16_t g_cp_sr = 0;
static uint16_t g_cp_clear = 0;
static uint32_t g_cp_fifo_base = 0;
static uint32_t g_cp_fifo_end = 0;
static uint32_t g_cp_fifo_hi = 0;
static uint32_t g_cp_fifo_lo = 0;
static uint32_t g_cp_fifo_rw_dist = 0;
static uint32_t g_cp_fifo_wp = 0;
static uint32_t g_cp_fifo_rp = 0;

// CP fifo pointer registers are 16-bit pairs with the LOW half at the
// lower address (Dolphin CommandProcessor: FIFO_BASE_LO=0x20, _HI=0x22).
// The old heuristic treated the +0 half as HIGH and assembled garbage
// (base 0x00474EA0 became 0x4EA00047).
static void write_reg32(uint32_t addr, uint32_t val, uint32_t& reg32) {
    if ((addr & 3) == 0) { // Low half (or full 32-bit store)
        if (val > 0xFFFF) {
            reg32 = val;
        } else {
            reg32 = (reg32 & 0xFFFF0000) | (val & 0xFFFF);
        }
    } else if ((addr & 3) == 2) { // High half
        reg32 = (reg32 & 0xFFFF) | ((val & 0xFFFF) << 16);
    }
}

static void process_cp_fifo() {
    if (!g_ctx_ptr) return;
    if (!(g_cp_cr & 1)) return; // CP not enabled

    // Process from RP to WP
    int count = 0;
    
    // In GC, addresses are physical (usually 0x00XXXXXX or 0x01XXXXXX)
    // We mask with 0x03FFFFFF to be safe
    uint32_t rp = g_cp_fifo_rp & 0x03FFFFFF;
    uint32_t wp = g_cp_fifo_wp & 0x03FFFFFF;
    uint32_t base = g_cp_fifo_base & 0x03FFFFFF;
    uint32_t end = g_cp_fifo_end & 0x03FFFFFF;

    if (base == 0 || end == 0) return;

    uint32_t rp0 = rp;
    while (rp != wp && count++ < 100000) {
        uint8_t b = g_ctx_ptr->mmu.read8(rp);
        nwii::runtime::GX_WGPIPE_Write8(b);
        rp++;
        if (rp >= end) {
            rp = base;
        }
    }
    g_cp_fifo_rp = rp;
    if (count > 0 && std::getenv("NWII_CPTRACE"))
        std::cout << "[CPFIFO] drained " << std::dec << count << " bytes rp=0x"
                  << std::hex << rp0 << "->0x" << rp << " wp=0x" << wp
                  << " base=0x" << base << " end=0x" << end << std::dec << "\n";
}

uint32_t cp_fifo_base_reg() { return g_cp_fifo_base; }

static int cp_debug_count = 0;
void register_cp(MMIODispatcher &dispatcher) {
    dispatcher.register_region(0xCC000000, 0xCC0000FF,
        [](uint32_t addr) -> uint32_t {
            switch (addr) {
                case 0xCC000000: return g_cp_sr;
                case 0xCC000002: return g_cp_cr;
                // 16-bit pairs, LOW half at the lower address.
                case 0xCC000020: return g_cp_fifo_base & 0xFFFF;
                case 0xCC000022: return g_cp_fifo_base >> 16;
                case 0xCC000024: return g_cp_fifo_end & 0xFFFF;
                case 0xCC000026: return g_cp_fifo_end >> 16;
                case 0xCC000028: return g_cp_fifo_hi & 0xFFFF;
                case 0xCC00002A: return g_cp_fifo_hi >> 16;
                case 0xCC00002C: return g_cp_fifo_lo & 0xFFFF;
                case 0xCC00002E: return g_cp_fifo_lo >> 16;
                case 0xCC000030: return g_cp_fifo_rw_dist & 0xFFFF;
                case 0xCC000032: return g_cp_fifo_rw_dist >> 16;
                case 0xCC000034: return g_cp_fifo_wp & 0xFFFF;
                case 0xCC000036: return g_cp_fifo_wp >> 16;
                case 0xCC000038: return g_cp_fifo_rp & 0xFFFF;
                case 0xCC00003A: return g_cp_fifo_rp >> 16;
            }
            return 0;
        },
        [](uint32_t addr, uint32_t val) {
            if (cp_debug_count++ < 100) std::cout << "[CP] write addr=" << std::hex << addr << " val=" << val << "\n";
            if (addr == 0xCC000000) { g_cp_sr = val; }
            else if (addr == 0xCC000002) { 
                g_cp_cr = val; 
                process_cp_fifo(); 
            }
            else if (addr == 0xCC000004) { g_cp_clear = val; }
            else if (addr >= 0xCC000020 && addr <= 0xCC000022) { write_reg32(addr, val, g_cp_fifo_base); }
            else if (addr >= 0xCC000024 && addr <= 0xCC000026) { write_reg32(addr, val, g_cp_fifo_end); }
            else if (addr >= 0xCC000028 && addr <= 0xCC00002A) { write_reg32(addr, val, g_cp_fifo_hi); }
            else if (addr >= 0xCC00002C && addr <= 0xCC00002E) { write_reg32(addr, val, g_cp_fifo_lo); }
            else if (addr >= 0xCC000030 && addr <= 0xCC000032) { write_reg32(addr, val, g_cp_fifo_rw_dist); }
            else if (addr >= 0xCC000034 && addr <= 0xCC000036) {
                write_reg32(addr, val, g_cp_fifo_wp);
                // Drain only once WP is fully programmed: the SDK writes the
                // halves separately and a half-updated WP would drain junk.
                if (addr == 0xCC000036 || val > 0xFFFF)
                    process_cp_fifo();
            }
            else if (addr >= 0xCC000038 && addr <= 0xCC00003A) { write_reg32(addr, val, g_cp_fifo_rp); }
        }
    );
}

} // namespace nwii::runtime::hw
