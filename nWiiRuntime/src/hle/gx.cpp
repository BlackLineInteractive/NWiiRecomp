
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
#include <iterator>
#include <chrono>

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

static std::vector<nwii::runtime::gx::GXCommand> s_carry;

uint64_t g_stat_frames = 0;
uint64_t g_stat_draws = 0;


uint64_t g_stat_parse_us = 0;
uint64_t g_stat_render_us = 0;

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

    
    
    static std::vector<uint8_t> s_parse_buf;
    {
        std::lock_guard<std::mutex> lock(g_fifo_mutex);
        if (!g_hw_fifo.empty()) {
            s_parse_buf.insert(s_parse_buf.end(), g_hw_fifo.begin(),
                               g_hw_fifo.end());
            g_hw_fifo.clear();
        }
    }
    if (s_parse_buf.empty()) return;

    auto t0 = std::chrono::steady_clock::now();
    std::vector<nwii::runtime::gx::GXCommand> commands;
    nwii::runtime::gx::FifoParser::Parse(s_parse_buf, commands);
    auto t1 = std::chrono::steady_clock::now();
    g_stat_parse_us +=
        std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();

    static const bool s_sample = std::getenv("NWII_SAMPLE") != nullptr;
    if (s_sample && !s_parse_buf.empty()) {
        static size_t last_size = 0;
        static int stuck_ticks = 0, dumps = 0;
        if (s_parse_buf.size() == last_size) {
            if (++stuck_ticks == 60 && dumps < 8) {
                dumps++;
                stuck_ticks = 0;
                std::cout << "[GX] stalled remainder " << std::dec
                          << s_parse_buf.size() << " bytes, head:" << std::hex;
                for (size_t i = 0; i < s_parse_buf.size() && i < 24; i++)
                    std::cout << " " << (unsigned)s_parse_buf[i];
                std::cout << std::dec << "\n";
            }
        } else {
            last_size = s_parse_buf.size();
            stuck_ticks = 0;
        }
    }

    if (s_parse_buf.size() > (4u << 20))
        s_parse_buf.clear();

    

    // "sometimes only Press any button" frames. Dolphin has no such boundary

    
    
    s_carry.insert(s_carry.end(), std::make_move_iterator(commands.begin()),
                   std::make_move_iterator(commands.end()));

    int last_frame_end = -1;
    int prev_frame_end = -1; 
    for (size_t i = 0; i < s_carry.size(); ++i) {
        const auto &c = s_carry[i];
        if (c.type == nwii::runtime::gx::GXCommandType::BPRegister &&
            c.reg == 0x52 && (c.val & 0x4000)) {
            prev_frame_end = last_frame_end;
            last_frame_end = (int)i;
        }
    }
    if (last_frame_end < 0)
        return; 

    

    

    

    std::vector<nwii::runtime::gx::GXCommand> frame;
    frame.reserve(s_carry.size());
    for (int i = 0; i <= last_frame_end; ++i) {
        auto &c = s_carry[i];
        if (i <= prev_frame_end &&
            c.type == nwii::runtime::gx::GXCommandType::DrawPrimitive)
            continue; 
        frame.push_back(std::move(c));
    }
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
        if (g_renderer) {
            auto r0 = std::chrono::steady_clock::now();
            g_renderer->Render(frame);
            auto r1 = std::chrono::steady_clock::now();
            g_stat_render_us +=
                std::chrono::duration_cast<std::chrono::microseconds>(r1 - r0)
                    .count();
        }
    }
}

namespace nwii::runtime {

static inline void wgp_push(uint8_t b) {

    static const char* s_dump_path = std::getenv("NWII_GXDUMP");
    static const char* s_seg_path = std::getenv("NWII_WGSEG");
    if (s_dump_path) {
        static FILE* f = fopen(s_dump_path, "wb");
        static size_t n = 0;
        if (f && n < 4000000) {
            fputc(b, f);
            fflush(f);
            ++n;
        }
    }

    
    if (const char* segp = s_seg_path) {
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

    
    static const bool wgtrace = std::getenv("NWII_WGTRACE") != nullptr;
    if (wgtrace) {
        static int zeros = 0;
        static int logged = 0;
        static uint8_t prev = 0xFF;
        static int vcd_collect = 0;      
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





static bool wgp_redirect(uint8_t b) {
    uint32_t base, end, wptr;
    nwii::runtime::hw::pi_fifo_get(base, end, wptr);
    if (base == 0) return false; 
    uint32_t cp_base = nwii::runtime::hw::cp_fifo_base_reg();
    if ((base & 0x03FFFFFF) == (cp_base & 0x03FFFFFF))
        return false; 
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

} 
