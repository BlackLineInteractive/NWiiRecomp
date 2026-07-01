#include "runtime/gx/fifo_parser.h"
#include "runtime/gx/renderer.h"
#include "runtime/gx_state.h"
#include "runtime/cpu_context.h"
#include <iostream>
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


void ProcessGXFifo() {
    std::lock_guard<std::mutex> lock(g_fifo_mutex);
    if (g_hw_fifo.empty()) return;

    std::vector<nwii::runtime::gx::GXCommand> commands;
    nwii::runtime::gx::FifoParser::Parse(g_hw_fifo, commands);
    nwii::runtime::gx::Renderer::Render(commands);
}

namespace nwii::runtime {

void GX_WGPIPE_Write8(uint8_t val) {
    std::lock_guard<std::mutex> lock(g_fifo_mutex);
    g_hw_fifo.push_back(val);
}

void GX_WGPIPE_Write16(uint16_t val) {
    std::lock_guard<std::mutex> lock(g_fifo_mutex);
    g_hw_fifo.push_back(val >> 8);
    g_hw_fifo.push_back(val & 0xFF);
}

void GX_WGPIPE_Write32(uint32_t val) {
    std::lock_guard<std::mutex> lock(g_fifo_mutex);
    g_hw_fifo.push_back(val >> 24);
    g_hw_fifo.push_back((val >> 16) & 0xFF);
    g_hw_fifo.push_back((val >> 8) & 0xFF);
    g_hw_fifo.push_back(val & 0xFF);
}

void GX_WGPIPE_WriteF32(float val) {
    union { float f; uint32_t i; } u; u.f = val;
    GX_WGPIPE_Write32(u.i);
}

void GX_WGPIPE_WriteF64(double val) {
    union { double d; uint64_t i; } u; u.d = val;
    std::lock_guard<std::mutex> lock(g_fifo_mutex);
    g_hw_fifo.push_back(u.i >> 56);
    g_hw_fifo.push_back((u.i >> 48) & 0xFF);
    g_hw_fifo.push_back((u.i >> 40) & 0xFF);
    g_hw_fifo.push_back((u.i >> 32) & 0xFF);
    g_hw_fifo.push_back((u.i >> 24) & 0xFF);
    g_hw_fifo.push_back((u.i >> 16) & 0xFF);
    g_hw_fifo.push_back((u.i >> 8) & 0xFF);
    g_hw_fifo.push_back(u.i & 0xFF);
}

} // namespace

// Dolphin-accurate CP FIFO Drainer
