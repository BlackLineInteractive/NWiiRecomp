#include "runtime/cpu_context.h"
#include <iostream>
#include <raylib.h>
#include <rlgl.h>

using namespace nwii::runtime;

extern "C" {

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

void GX_WGPIPE_Write8(uint8_t val) {
    if (!g_in_begin) return;
    // Assume 8-bit writes are colors (usually 4 bytes for RGBA)
    // For simplicity, we just skip handling individual byte colors here unless fully tracked.
}

void GX_WGPIPE_Write16(uint16_t val) {
    if (!g_in_begin) return;
}

void GX_WGPIPE_Write32(uint32_t val) {
    if (!g_in_begin) return;
    // Usually a 32-bit integer write is an RGBA8 color
    rlColor4ub((val >> 24) & 0xFF, (val >> 16) & 0xFF, (val >> 8) & 0xFF, val & 0xFF);
}

void GX_WGPIPE_WriteF32(float val) {
    if (!g_in_begin) return;
    
    g_vtx[g_vtx_idx++] = val;
    if (g_vtx_idx == 3) {
        rlVertex3f(g_vtx[0], g_vtx[1], g_vtx[2]);
        g_vtx_idx = 0;
    }
}

} // extern "C"
