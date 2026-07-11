#include "runtime/cpu_context.h"
namespace nwii::runtime { extern CPUContext *g_ctx_ptr; }

#include "runtime/gx/fifo_parser.h"
#include "runtime/gx/renderer.h"
#include "runtime/hw/hw.h"
#include "runtime/gx_state.h"
#include "runtime/cpu_context.h"
#include <iostream>
#include <cstdlib>
#include <vector>
#include <mutex>
#include <raylib.h>
#include <rlgl.h>

using namespace nwii::runtime;
using namespace nwii::runtime::gx;

GXState nwii::runtime::gx::g_state = {};

namespace nwii::runtime {
    extern MMU* g_mmu; // Access to physical memory for Index Array
}

std::mutex g_fifo_mutex;
std::vector<uint8_t> g_hw_fifo;

// Rolling 5-byte window over the raw command stream to catch PE signals the
// moment the game submits them: BP load (0x61) of reg 0x45 with low nibble 2
// = draw-done (GXDrawDone), reg 0x47/0x48 = draw-sync token. This models an
// instant GPU so GXWaitDrawDone/token waits complete. A data byte can fake
// the 0x61 prefix in rare cases; a false PE signal is harmless (the ISR just
// runs its callback once more).
static void pe_sniff_byte(uint8_t b) {
    static uint8_t w[5];
    w[0] = w[1]; w[1] = w[2]; w[2] = w[3]; w[3] = w[4]; w[4] = b;
    if (w[0] != 0x61)
        return;
    uint8_t reg = w[1];
    uint32_t val = ((uint32_t)w[2] << 16) | ((uint32_t)w[3] << 8) | w[4];
    if (reg == 0x45 && (val & 0xF) == 2)
        nwii::runtime::hw::pe_signal_finish();
    else if (reg == 0x47)
        nwii::runtime::hw::pe_signal_token(val & 0xFFFF, false);
    else if (reg == 0x48)
        nwii::runtime::hw::pe_signal_token(val & 0xFFFF, true);
}


void ProcessGXFifo() {

    std::lock_guard<std::mutex> lock(g_fifo_mutex);
    if (g_hw_fifo.empty()) return;

    std::vector<nwii::runtime::gx::GXCommand> commands;
    nwii::runtime::gx::FifoParser::Parse(g_hw_fifo, commands);
    nwii::runtime::gx::Renderer::Render(commands);
}

namespace nwii::runtime {

// Single entry point for every captured command byte: feeds the FIFO and
// the PE-signal sniffer. Callers hold g_fifo_mutex.
static inline void wgp_push(uint8_t b) {
    g_hw_fifo.push_back(b);
    pe_sniff_byte(b);
}

void GX_WGPIPE_Write8(uint8_t val) {
    std::lock_guard<std::mutex> lock(g_fifo_mutex);
    wgp_push(val);
}

void GX_WGPIPE_Write16(uint16_t val) {
    std::lock_guard<std::mutex> lock(g_fifo_mutex);
    wgp_push(val >> 8);
    wgp_push(val & 0xFF);
}

void GX_WGPIPE_Write32(uint32_t val) {
    if (std::getenv("NWII_SAMPLE")) {
        static uint64_t n = 0;
        ++n;
        if (n == 1 || n == 10 || n == 100 || n == 1000 || n == 10000 ||
            (n % 100000) == 0)
            std::cout << "[GX] WGP write32 #" << std::dec << n
                      << " fifo_bytes=" << g_hw_fifo.size() << "\n";
    }
    std::lock_guard<std::mutex> lock(g_fifo_mutex);
    wgp_push(val >> 24);
    wgp_push((val >> 16) & 0xFF);
    wgp_push((val >> 8) & 0xFF);
    wgp_push(val & 0xFF);
}

void GX_WGPIPE_WriteF32(float val) {
    union { float f; uint32_t i; } u; u.f = val;
    GX_WGPIPE_Write32(u.i);
}

void GX_WGPIPE_WriteF64(double val) {
    union { double d; uint64_t i; } u; u.d = val;
    std::lock_guard<std::mutex> lock(g_fifo_mutex);
    for (int s = 56; s >= 0; s -= 8)
        wgp_push((u.i >> s) & 0xFF);
}

} // namespace

// Dolphin-accurate CP FIFO Drainer
