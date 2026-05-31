#include "runtime/cpu_context.h"
#include <iostream>
#include <raylib.h>
#include <rlgl.h>

using namespace nwii::runtime;

// GX FIFO structure representing the Graphics FIFO
struct GXFifoObj {
    uint32_t base;
    uint32_t top;
    uint32_t size;
    uint32_t count;
    uint32_t rd_ptr;
    uint32_t wr_ptr;
};

static GXFifoObj g_cpu_fifo = {0};
static GXFifoObj g_gp_fifo = {0};

extern "C" {

void GXInitFifoBase(CPUContext& ctx) {
    uint32_t fifo_ptr = ctx.gpr[3];
    uint32_t base = ctx.gpr[4];
    uint32_t size = ctx.gpr[5];
    
    // Simulate writing to the GXFifoObj structure in guest memory
    ctx.mmu.write32(fifo_ptr + 0, base);
    ctx.mmu.write32(fifo_ptr + 4, base + size);
    ctx.mmu.write32(fifo_ptr + 8, size);
    ctx.mmu.write32(fifo_ptr + 12, 0);
    ctx.mmu.write32(fifo_ptr + 16, base);
    ctx.mmu.write32(fifo_ptr + 20, base);
    
    std::cout << "[HLE GX] GXInitFifoBase: fifo_ptr=" << std::hex << fifo_ptr 
              << ", base=" << base << ", size=" << std::dec << size << std::endl;
}

void GXInitFifoPtrs(CPUContext& ctx) {
    uint32_t fifo_ptr = ctx.gpr[3];
    uint32_t rd_ptr = ctx.gpr[4];
    uint32_t wr_ptr = ctx.gpr[5];
    
    ctx.mmu.write32(fifo_ptr + 16, rd_ptr);
    ctx.mmu.write32(fifo_ptr + 20, wr_ptr);
}

void GXSetCPUFifo(CPUContext& ctx) {
    uint32_t fifo_ptr = ctx.gpr[3];
    
    g_cpu_fifo.base = ctx.mmu.read32(fifo_ptr + 0);
    g_cpu_fifo.top = ctx.mmu.read32(fifo_ptr + 4);
    g_cpu_fifo.size = ctx.mmu.read32(fifo_ptr + 8);
    g_cpu_fifo.count = ctx.mmu.read32(fifo_ptr + 12);
    g_cpu_fifo.rd_ptr = ctx.mmu.read32(fifo_ptr + 16);
    g_cpu_fifo.wr_ptr = ctx.mmu.read32(fifo_ptr + 20);
    
    std::cout << "[HLE GX] GXSetCPUFifo: base=" << std::hex << g_cpu_fifo.base << std::endl;
}

void GXGetCPUFifo(CPUContext& ctx) {
    // Return a dummy pointer or the one previously set
    // For simplicity, we just return a pointer to an internal dummy if needed,
    // but typically it returns a ptr to the active GXFifoObj.
    // Let's assume we allocated a dummy object in high memory or just return 0.
    ctx.gpr[3] = 0; // Stub
}

void VIInit(CPUContext& ctx) {
    std::cout << "[HLE GX] VIInit: Initializing Video Interface" << std::endl;
    if (!IsWindowReady()) {
        InitWindow(640, 480, "NWiiRecomp - HLE Window");
        SetTargetFPS(60);
    }
}

void GXInit(CPUContext& ctx) {
    uint32_t gx_fifo_addr = ctx.gpr[3];
    uint32_t gx_fifo_size = ctx.gpr[4];
    std::cout << "[HLE GX] GXInit: fifo=" << std::hex << gx_fifo_addr 
              << ", size=" << std::dec << gx_fifo_size << std::endl;
}

void GXSetCopyClear(CPUContext& ctx) {
    // r3 = color (RGBA8)
    // r4 = clear z (usually 0x00FFFFFF)
    uint32_t color = ctx.gpr[3];
    uint32_t z = ctx.gpr[4];
    
    Color clearColor;
    clearColor.r = (color >> 24) & 0xFF;
    clearColor.g = (color >> 16) & 0xFF;
    clearColor.b = (color >> 8) & 0xFF;
    clearColor.a = color & 0xFF;
    
    ClearBackground(clearColor);
    std::cout << "[HLE GX] GXSetCopyClear: color=" << std::hex << color << std::endl;
}

static bool g_in_begin = false;
static float g_vtx[3];
static int g_vtx_idx = 0;

void GXBegin(CPUContext& ctx) {
    uint32_t primitive_type = ctx.gpr[3];
    uint32_t vertex_format = ctx.gpr[4];
    uint32_t vertex_count = ctx.gpr[5];
    
    // Map Wii primitive types to rlgl
    int rl_mode = RL_TRIANGLES;
    if (primitive_type == 0x80) rl_mode = RL_QUADS;
    else if (primitive_type == 0x90) rl_mode = RL_TRIANGLES;
    else if (primitive_type == 0x98) rl_mode = RL_TRIANGLES; // Actually Triangle Strip, but fallback
    
    rlBegin(rl_mode);
    g_in_begin = true;
    g_vtx_idx = 0;
}

void GXEnd(CPUContext& ctx) {
    if (g_in_begin) {
        rlEnd();
        g_in_begin = false;
    }
}

void GXSetVtxDesc(CPUContext& ctx) {
    // Stub
}

void GXSetVtxAttrFmt(CPUContext& ctx) {
    // Stub
}

static void push_fifo(uint8_t* data, size_t size) {
    if (g_cpu_fifo.base == 0) return;
    
    // In a real implementation we would write to MMU and wrap around
    // For now, we simulate the write pointer advancing
    g_cpu_fifo.wr_ptr += size;
    if (g_cpu_fifo.wr_ptr >= g_cpu_fifo.top) {
        g_cpu_fifo.wr_ptr = g_cpu_fifo.base;
    }
    // Also increase count, though HLE might not need strict accounting unless polled
    g_cpu_fifo.count += size;
}

void GX_WGPIPE_Write8(uint8_t val) {
    push_fifo(&val, 1);
    if (!g_in_begin) return;
    // Assume 8-bit writes are colors (usually 4 bytes for RGBA)
    // For simplicity, we just skip handling individual byte colors here unless fully tracked.
}

void GX_WGPIPE_Write16(uint16_t val) {
    push_fifo((uint8_t*)&val, 2);
    if (!g_in_begin) return;
}

void GX_WGPIPE_Write32(uint32_t val) {
    push_fifo((uint8_t*)&val, 4);
    if (!g_in_begin) return;
    // Usually a 32-bit integer write is an RGBA8 color
    rlColor4ub((val >> 24) & 0xFF, (val >> 16) & 0xFF, (val >> 8) & 0xFF, val & 0xFF);
}

void GX_WGPIPE_WriteF32(float val) {
    push_fifo((uint8_t*)&val, 4);
    if (!g_in_begin) return;
    
    g_vtx[g_vtx_idx++] = val;
    if (g_vtx_idx == 3) {
        rlVertex3f(g_vtx[0], g_vtx[1], g_vtx[2]);
        g_vtx_idx = 0;
    }
}

} // extern "C"
