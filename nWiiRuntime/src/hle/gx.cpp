
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

// Run-to-run diagnostics: which stage a run got to (see the [STAT] line).
uint64_t g_stat_frames = 0;
uint64_t g_stat_draws = 0;

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

    ++g_stat_frames;
    for (const auto &c : frame)
        if (c.type == nwii::runtime::gx::GXCommandType::DrawPrimitive)
            ++g_stat_draws;

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
    // NWII_WGSEG=path: append "offset pc" lines each time the guest pc
    // behind consecutive pipe bytes changes — segments the raw stream by
    // its writer, which pins draw-header vs vertex-data boundaries exactly.
    if (const char* segp = std::getenv("NWII_WGSEG")) {
        static FILE* sf = fopen(segp, "w");
        static uint64_t off = 0;
        static uint32_t last_pc = 0;
        extern CPUContext* g_ctx_ptr;
        uint32_t pc = g_ctx_ptr ? g_ctx_ptr->pc : 0;
        if (sf && pc != last_pc) {
            fprintf(sf, "%llx %x\n", (unsigned long long)off, pc);
            last_pc = pc;
        }
        ++off;
    }
    // NWII_WGTRACE=1: name the guest code behind pipe traffic. Logs the
    // pc/lr writing (a) a draw opcode arriving right after a GXFlush pad
    // (>=24 zero bytes) and (b) the starts of CP register loads.
    static const bool wgtrace = std::getenv("NWII_WGTRACE") != nullptr;
    if (wgtrace) {
        static int zeros = 0;
        static int logged = 0;
        static uint8_t prev = 0xFF;
        static int vcd_collect = 0;      // bytes left of a VCD value
        static uint32_t vcd_val = 0;
        static uint8_t vcd_reg = 0;
        extern CPUContext* g_ctx_ptr;
        if (vcd_collect > 0) {
            vcd_val = (vcd_val << 8) | b;
            if (--vcd_collect == 0)
                std::cout << "[WGP] VCD 0x" << std::hex << (unsigned)vcd_reg
                          << " = 0x" << vcd_val << " lr=0x"
                          << (g_ctx_ptr ? g_ctx_ptr->lr : 0) << std::dec << "\n";
        } else if (prev == 0x08 && (b == 0x50 || b == 0x60) && g_ctx_ptr &&
                   (g_ctx_ptr->pc == 0x800bbbac)) {
            vcd_collect = 4; vcd_val = 0; vcd_reg = b;
        }
        prev = b;
        if (b == 0) {
            ++zeros;
        } else {
            if (zeros >= 24 && b >= 0x80 && logged < 16 && g_ctx_ptr) {
                std::cout << "[WGP] draw 0x" << std::hex << (unsigned)b
                          << " after " << std::dec << zeros << " zeros pc=0x"
                          << std::hex << g_ctx_ptr->pc << " lr=0x"
                          << g_ctx_ptr->lr << std::dec << "\n";
                ++logged;
            }
            zeros = 0;
        }
    }
    g_hw_fifo.push_back(b);
}

// The write-gather pipe lands wherever the PI CPU-fifo points. When that
// fifo is NOT the GP-linked one — GXBeginDisplayList redirected it to a
// display-list buffer — gathered bytes belong in that guest-RAM buffer,
// not in the live command stream. Leaking them into the parser desynced
// it (recorded vertices misread under the live VCD/VAT) and left the DL
// buffer empty for the later GXCallDisplayList. Returns true if consumed.
static bool wgp_redirect(uint8_t b) {
    uint32_t base, end, wptr;
    nwii::runtime::hw::pi_fifo_get(base, end, wptr);
    if (base == 0) return false; // fifo not programmed yet (early boot)
    uint32_t cp_base = nwii::runtime::hw::cp_fifo_base_reg();
    if ((base & 0x03FFFFFF) == (cp_base & 0x03FFFFFF))
        return false; // GP-linked: live rendering path
    if (!nwii::runtime::g_mmu) return false;
    nwii::runtime::g_mmu->write8(0x80000000u | (wptr & 0x01FFFFFFu), b);
    wptr += 1;
    if ((wptr & 0x03FFFFFF) > (end & 0x03FFFFFF))
        wptr = base;
    nwii::runtime::hw::pi_fifo_set_wptr(wptr);
    return true;
}

void GX_WGPIPE_Write8(uint8_t val) {
    if (wgp_redirect(val)) return;
    std::lock_guard<std::mutex> lock(g_fifo_mutex);
    wgp_push(val);
}

void GX_WGPIPE_Write16(uint16_t val) {
    if (wgp_redirect(val >> 8)) { wgp_redirect(val & 0xFF); return; }
    std::lock_guard<std::mutex> lock(g_fifo_mutex);
    wgp_push(val >> 8);
    wgp_push(val & 0xFF);
}

void GX_WGPIPE_Write32(uint32_t val) {
    if (wgp_redirect(val >> 24)) {
        wgp_redirect((val >> 16) & 0xFF);
        wgp_redirect((val >> 8) & 0xFF);
        wgp_redirect(val & 0xFF);
        return;
    }
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
    if (wgp_redirect((u.i >> 56) & 0xFF)) {
        for (int s = 48; s >= 0; s -= 8)
            wgp_redirect((u.i >> s) & 0xFF);
        return;
    }
    std::lock_guard<std::mutex> lock(g_fifo_mutex);
    for (int s = 56; s >= 0; s -= 8)
        wgp_push((u.i >> s) & 0xFF);
}

} // namespace nwii::runtime
