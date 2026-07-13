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

// PE draw-done/token signals are detected inside FifoParser::Parse (it sees
// display-list contents too), not by sniffing the raw byte stream here.

void ProcessGXFifo() {

    std::lock_guard<std::mutex> lock(g_fifo_mutex);
    if (g_hw_fifo.empty()) return;

    std::vector<nwii::runtime::gx::GXCommand> commands;
    nwii::runtime::gx::FifoParser::Parse(g_hw_fifo, commands);

    // Stall detector: a remainder that keeps its size across many parse ticks
    // means the parser cannot advance (desync or bogus length field).
    if (std::getenv("NWII_SAMPLE") && !g_hw_fifo.empty()) {
        static size_t last_size = 0;
        static int stuck_ticks = 0, dumps = 0;
        if (g_hw_fifo.size() == last_size) {
            if (++stuck_ticks == 60 && dumps < 8) {
                dumps++;
                stuck_ticks = 0;
                std::cout << "[GX] stalled remainder " << std::dec
                          << g_hw_fifo.size() << " bytes, head:" << std::hex;
                for (size_t i = 0; i < g_hw_fifo.size() && i < 24; i++)
                    std::cout << " " << (unsigned)g_hw_fifo[i];
                std::cout << std::dec << "\n";
            }
        } else {
            last_size = g_hw_fifo.size();
            stuck_ticks = 0;
        }
    }

    // Parse erases the fully-consumed prefix; a trailing incomplete command is
    // kept for the next chunk (do NOT clear the whole buffer — that splits
    // commands and permanently desyncs the stream). Safety valve: if the
    // remainder grows past a sane bound the stream is corrupt (a bad length or
    // a byte the parser can't advance past), so drop it to avoid unbounded
    // growth rather than accumulating forever.
    if (g_hw_fifo.size() > (4u << 20))
        g_hw_fifo.clear();

    // Headless runs still parse (PE signals, GX state) but have no GL context.
    if (IsWindowReady()) {
        nwii::runtime::gx::Renderer::Render(commands);
    }
}

namespace nwii::runtime {

// Single entry point for every captured command byte. Callers hold g_fifo_mutex.
static inline void wgp_push(uint8_t b) {
    // NWII_GXDUMP=path: tee the first 64KB of the raw WGP stream to a file
    // for offline analysis of parser desyncs.
    if (const char* path = std::getenv("NWII_GXDUMP")) {
        static FILE* f = fopen(path, "wb");
        static size_t n = 0;
        if (f && n < 65536) {
            fputc(b, f);
            fflush(f);
            ++n;
        }
    }
    g_hw_fifo.push_back(b);
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
