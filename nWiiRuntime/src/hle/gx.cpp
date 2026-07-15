
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

using namespace nwii::runtime;
using namespace nwii::runtime::gx;

GXState nwii::runtime::gx::g_state = {};

static std::unique_ptr<nwii::runtime::gx::IRenderer> g_renderer = nullptr;
static void* g_window = nullptr;

void GX_SetWindow(void* window) { g_window = window; }

void GX_Init() {
    g_renderer = nwii::runtime::gx::IRenderer::Create();
    if (g_renderer) g_renderer->Initialize(nullptr);
}

namespace nwii::runtime {
    extern MMU* g_mmu;
}

std::mutex g_fifo_mutex;
std::vector<uint8_t> g_hw_fifo;
// Commands of a frame the game has not finished submitting yet.
static std::vector<nwii::runtime::gx::GXCommand> s_carry;

void GX_GetClearColor(unsigned char rgba[4]) {
    rgba[0] = (unsigned char)(g_state.clearAR & 0xFF);
    rgba[1] = (unsigned char)(g_state.clearGB >> 8);
    rgba[2] = (unsigned char)(g_state.clearGB & 0xFF);
    rgba[3] = (unsigned char)(g_state.clearAR >> 8);
}

void GX_GetXfb(uint32_t* addr, unsigned* w, unsigned* h, unsigned* stride) {
    *addr   = g_state.xfbAddr;
    *w      = g_state.xfbW;
    *h      = g_state.xfbH;
    *stride = g_state.xfbStride;
}

void ProcessGXFifo() {
    std::lock_guard<std::mutex> lock(g_fifo_mutex);
    if (g_hw_fifo.empty()) return;

    std::vector<nwii::runtime::gx::GXCommand> commands;
    nwii::runtime::gx::FifoParser::Parse(g_hw_fifo, commands);

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

    if (g_hw_fifo.size() > (4u << 20))
        g_hw_fifo.clear();

    // Render whole frames only. This runs once per host frame-loop iteration,
    // pinned to the host's vsync, which has nothing to do with how far the
    // guest CPU has got: the drain boundary regularly falls mid-frame, so we
    // rendered (and EFB-cleared for) a fragment — that is the flicker, and the
    // "sometimes only Press any button" frames. Dolphin has no such boundary
    // because it drives the GP from the CP FIFO pointers and CPU ticks
    // (FifoManager::RunGpuOnCpu), never a wall clock. The game marks a finished
    // frame with BP 0x52 bit14 (copy-to-XFB), so buffer until one arrives and
    // render everything up to it as one unit.
    s_carry.insert(s_carry.end(), commands.begin(), commands.end());

    int last_frame_end = -1;
    for (size_t i = 0; i < s_carry.size(); ++i) {
        const auto &c = s_carry[i];
        if (c.type == nwii::runtime::gx::GXCommandType::BPRegister &&
            c.reg == 0x52 && (c.val & 0x4000))
            last_frame_end = (int)i;
    }
    if (last_frame_end < 0)
        return; // frame still incomplete: keep buffering

    std::vector<nwii::runtime::gx::GXCommand> frame(
        s_carry.begin(), s_carry.begin() + last_frame_end + 1);
    s_carry.erase(s_carry.begin(), s_carry.begin() + last_frame_end + 1);

    if (g_window) {
        if (!g_renderer) {
            g_renderer = nwii::runtime::gx::IRenderer::Create();
            g_renderer->Initialize(nullptr);
        }
        if (g_renderer)
            g_renderer->Render(frame);
    }
}

namespace nwii::runtime {

static inline void wgp_push(uint8_t b) {
    if (const char* path = std::getenv("NWII_GXDUMP")) {
        static FILE* f = fopen(path, "wb");
        static size_t n = 0;
        if (f && n < 4000000) {
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

} // namespace nwii::runtime
