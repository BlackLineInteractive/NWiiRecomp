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

struct VAT {
    uint32_t posCnt, posFmt;
    uint32_t colCnt[2], colFmt[2];
    uint32_t texCnt[8], texFmt[8];
};

struct CPState {
    VAT vat[8];
    uint32_t matrix_idx;
};

static GXFifoObj g_cpu_fifo = {0};
static GXFifoObj g_gp_fifo = {0};
static CPState g_cp_state;
static std::vector<uint8_t> g_cmd_buffer;



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

} // extern "C"

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
static int g_fifo_state = 0; // 0: Idle, 1: BP, 2: CP, 3: XF, 4: Vertex
static int g_fifo_bytes_expected = 0;
static int g_vtx_expected_bytes = 0;
static int g_vtx_format_idx = 0;

static void process_command(CPUContext& ctx) {
    if (g_cmd_buffer.empty()) return;
    uint8_t cmd = g_cmd_buffer[0];
    
    if (cmd == 0x08) {
        // CP Command (6 bytes expected)
        if (g_cmd_buffer.size() >= 6) {
            uint8_t subcmd = g_cmd_buffer[1];
            uint32_t val = (g_cmd_buffer[2] << 24) | (g_cmd_buffer[3] << 16) | (g_cmd_buffer[4] << 8) | g_cmd_buffer[5];
            
            if (subcmd >= 0x50 && subcmd <= 0x57) {
                // VAT A
                int idx = subcmd - 0x50;
                g_cp_state.vat[idx].posCnt = val & 1;
                g_cp_state.vat[idx].posFmt = (val >> 1) & 7;
                g_cp_state.vat[idx].colCnt[0] = (val >> 9) & 1;
                g_cp_state.vat[idx].colFmt[0] = (val >> 10) & 7;
            } else if (subcmd >= 0x60 && subcmd <= 0x67) {
                // VAT B
                // Add extended format processing if needed
            } else if (subcmd >= 0x70 && subcmd <= 0x77) {
                // VAT C
            }
        }
    } else if (cmd == 0x10) {
        // XF Command
        // Size dynamically calculated in write function
    } else if (cmd >= 0x80 && cmd <= 0x9F) {
        // Vertex data
        // For a full implementation, we'd read indices, fetch from RAM based on VAT, etc.
    }
    
    g_cmd_buffer.clear();
    g_fifo_state = 0;
    g_fifo_bytes_expected = 0;
}

namespace nwii {
namespace runtime {

void GX_WGPIPE_Write8(uint8_t val) {
    push_fifo(&val, 1);
    
    if (g_fifo_state == 0) {
        g_cmd_buffer.clear();
        g_cmd_buffer.push_back(val);
        
        if (val == 0x61) { g_fifo_state = 1; g_fifo_bytes_expected = 4; }
        else if (val == 0x08) { g_fifo_state = 2; g_fifo_bytes_expected = 5; } // Command byte + 5 bytes = 6 bytes total
        else if (val == 0x10) { g_fifo_state = 3; g_fifo_bytes_expected = 2; } // First need 2 bytes for length
        else if (val >= 0x80 && val <= 0x9F) {
            g_fifo_state = 4;
            g_vtx_format_idx = val & 7;
            // Native draw command
            int rl_mode = RL_TRIANGLES;
            if (val == 0x80) rl_mode = RL_QUADS;
            rlBegin(rl_mode);
            g_in_begin = true;
            g_vtx_idx = 0;
            // The next bytes are 2-byte count, then vertices.
            g_fifo_bytes_expected = 2; 
        }
    } else {
        g_cmd_buffer.push_back(val);
        g_fifo_bytes_expected--;
        
        // Dynamically adjust XF size once length bytes are available
        if (g_fifo_state == 3 && g_cmd_buffer.size() == 3) {
            uint16_t length = (g_cmd_buffer[1] << 8) | g_cmd_buffer[2];
            // length in XF command is (N-1). Data is (length+1)*4 bytes, plus 2 for register.
            g_fifo_bytes_expected = 2 + (length + 1) * 4;
        } else if (g_fifo_state == 4 && g_cmd_buffer.size() == 3) {
            uint16_t vtx_count = (g_cmd_buffer[1] << 8) | g_cmd_buffer[2];
            // Compute expected bytes per vertex based on g_cp_state.vat[g_vtx_format_idx]
            // For now, assume a fixed size or fallback to intercepting write F32s.
            // A real emulator would dynamically size this.
        }
        
        if (g_fifo_bytes_expected <= 0 && g_fifo_state != 4) {
            // Can't call process_command directly without ctx, 
            // but we can parse simple commands locally or buffer them for the GPU thread.
            g_cmd_buffer.clear();
            g_fifo_state = 0;
        }
    }
}

void GX_WGPIPE_Write16(uint16_t val) {
    uint8_t bytes[2] = { (uint8_t)(val >> 8), (uint8_t)val };
    GX_WGPIPE_Write8(bytes[0]);
    GX_WGPIPE_Write8(bytes[1]);
}

void GX_WGPIPE_Write32(uint32_t val) {
    uint8_t bytes[4] = { (uint8_t)(val >> 24), (uint8_t)(val >> 16), (uint8_t)(val >> 8), (uint8_t)val };
    for (int i=0; i<4; i++) GX_WGPIPE_Write8(bytes[i]);
    
    if (g_in_begin) {
        // Usually a 32-bit integer write during vertex phase is an RGBA8 color
        rlColor4ub(bytes[0], bytes[1], bytes[2], bytes[3]);
    }
}

void GX_WGPIPE_WriteF32(float val) {
    union { float f; uint32_t i; } u;
    u.f = val;
    GX_WGPIPE_Write32(u.i);
    
    if (g_in_begin) {
        g_vtx[g_vtx_idx++] = val;
        // Vertex format handler logic
        if (g_vtx_idx == 3) {
            rlVertex3f(g_vtx[0], g_vtx[1], g_vtx[2]);
            g_vtx_idx = 0;
        }
    }
}

}
}
