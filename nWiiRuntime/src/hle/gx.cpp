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

namespace {

inline uint32_t Read24(size_t offset) {
    return (g_hw_fifo[offset] << 16) | (g_hw_fifo[offset+1] << 8) | g_hw_fifo[offset+2];
}

inline uint32_t Read32(size_t offset) {
    return (g_hw_fifo[offset] << 24) | (g_hw_fifo[offset+1] << 16) | (g_hw_fifo[offset+2] << 8) | g_hw_fifo[offset+3];
}

// Decode BP registers (TEV, Texture, Z-Buffer)
void ParseBP(uint8_t reg, uint32_t val) {
    // GenMode
    if (reg == 0x00) {
        g_state.numTexGens = (val & 0xF);
        g_state.numChans = ((val >> 4) & 0x7);
        g_state.numTevStages = ((val >> 10) & 0xF) + 1;
    }
    // TEV Color Env
    else if (reg >= 0xC0 && reg <= 0xCF) {
        int stage = reg - 0xC0;
        g_state.tevStages[stage].colorInA = (val >> 12) & 0x1F;
        g_state.tevStages[stage].colorInB = (val >> 8) & 0x1F;
        g_state.tevStages[stage].colorInC = (val >> 4) & 0x1F;
        g_state.tevStages[stage].colorInD = val & 0x1F;
        g_state.tevStages[stage].colorOp = (val >> 18) & 0xF;
        g_state.tevStages[stage].colorBias = (val >> 16) & 0x3;
        g_state.tevStages[stage].colorScale = (val >> 20) & 0x3;
        g_state.tevStages[stage].colorClamp = (val >> 22) & 0x1;
        g_state.tevStages[stage].colorRegId = (val >> 23) & 0x3;
    }
    // TODO: Add texture register parsing (0x80-0x9F) and Z-Mode (0x40)
}

// Decode CP registers (Array Pointers, VAT)
void ParseCP(uint8_t reg, uint32_t val) {
    // Array Base pointers
    if (reg >= 0xA0 && reg <= 0xAC) {
        g_state.arrayBase[reg - 0xA0] = val & 0x3FFFFFFF; // Physical memory only
    }
    // Array Stride
    else if (reg >= 0xB0 && reg <= 0xBC) {
        g_state.arrayStride[reg - 0xB0] = val & 0xFF;
    }
    // VAT A (Format for Pos, Norm, Color)
    else if (reg >= 0x50 && reg <= 0x57) {
        int vatIdx = reg - 0x50;
        g_state.vat[vatIdx].posMask = (VtxAttrMask)((val >> 9) & 3);
        g_state.vat[vatIdx].posType = (VtxAttrType)((val >> 0) & 7);
        g_state.vat[vatIdx].posShift = (val >> 5) & 0x1F;
        
        g_state.vat[vatIdx].clrMask[0] = (VtxAttrMask)((val >> 13) & 3);
        g_state.vat[vatIdx].clrType[0] = (VtxAttrType)((val >> 11) & 7);
    }
}

// Read a vertex attribute component (Direct or Indexed from memory).
// fifo_size is passed for bounds checking; on underflow sets fifo_offset past end
// so the caller's guard (curr_offset > fifo_size) will trigger a clean break.
float ReadAttribute(size_t& fifo_offset, VtxAttrMask mask, VtxAttrType type,
                    uint8_t shift, uint32_t array_idx, size_t fifo_size) {
    if (mask == VtxAttrMask::None) return 0.0f;

    if (mask == VtxAttrMask::Direct) {
        // Data is inline in FIFO
        if (type == VtxAttrType::F32) {
            if (fifo_offset + 4 > fifo_size) { fifo_offset = fifo_size + 1; return 0.0f; }
            uint32_t v = Read32(fifo_offset); fifo_offset += 4;
            float f; std::memcpy(&f, &v, 4); return f;
        } else if (type == VtxAttrType::U8) {
            if (fifo_offset + 1 > fifo_size) { fifo_offset = fifo_size + 1; return 0.0f; }
            uint8_t v = g_hw_fifo[fifo_offset++];
            return (float)v / (float)(1 << shift);
        } else if (type == VtxAttrType::S16) {
            if (fifo_offset + 2 > fifo_size) { fifo_offset = fifo_size + 1; return 0.0f; }
            int16_t v = (int16_t)((g_hw_fifo[fifo_offset] << 8) | g_hw_fifo[fifo_offset+1]);
            fifo_offset += 2;
            return (float)v / (float)(1 << shift);
        }
    } else {
        // Data is in memory (MEM1/MEM2). FIFO contains only the index.
        uint32_t index = 0;
        if (mask == VtxAttrMask::Index8) {
            if (fifo_offset + 1 > fifo_size) { fifo_offset = fifo_size + 1; return 0.0f; }
            index = g_hw_fifo[fifo_offset++];
        } else if (mask == VtxAttrMask::Index16) {
            if (fifo_offset + 2 > fifo_size) { fifo_offset = fifo_size + 1; return 0.0f; }
            index = (g_hw_fifo[fifo_offset] << 8) | g_hw_fifo[fifo_offset+1];
            fifo_offset += 2;
        }

        uint32_t data_addr = g_state.arrayBase[array_idx] + (index * g_state.arrayStride[array_idx]);

        if (type == VtxAttrType::F32) {
            return g_mmu->read_f32(data_addr);
        } else if (type == VtxAttrType::S16) {
            int16_t v = (int16_t)g_mmu->read16(data_addr);
            return (float)v / (float)(1 << shift);
        }
    }
    return 0.0f;
}

} // anon namespace

void ProcessGXFifo() {
    std::lock_guard<std::mutex> lock(g_fifo_mutex);
    if (g_hw_fifo.empty()) return;

    size_t offset = 0;
    while (offset < g_hw_fifo.size()) {
        uint8_t cmd = g_hw_fifo[offset];

        // BP Register Write
        if (cmd == 0x61) {
            if (offset + 5 > g_hw_fifo.size()) break;
            uint8_t reg = g_hw_fifo[offset + 1];
            uint32_t val = Read24(offset + 2);
            ParseBP(reg, val);
            offset += 5;
        }
        // CP Register Write
        else if (cmd == 0x08) {
            if (offset + 6 > g_hw_fifo.size()) break;
            uint8_t reg = g_hw_fifo[offset + 1];
            uint32_t val = Read32(offset + 2);
            ParseCP(reg, val);
            offset += 6;
        }
        // XF Register Write
        else if (cmd == 0x10) {
            if (offset + 5 > g_hw_fifo.size()) break;
            uint16_t length = (g_hw_fifo[offset + 1] << 8) | g_hw_fifo[offset + 2];
            uint32_t total_size = 5 + ((length + 1) * 4);
            if (offset + total_size > g_hw_fifo.size()) break;
            offset += total_size;
        }
        // Draw Primitives
        else if (cmd >= 0x80 && cmd <= 0x9F) {
            if (offset + 3 > g_hw_fifo.size()) break;
            
            uint16_t vtx_count = (g_hw_fifo[offset + 1] << 8) | g_hw_fifo[offset + 2];
            uint8_t vat_idx = cmd & 0x07; // Lower 3 bits = VAT index
            uint8_t prim_type = cmd & 0xF8;
            
            VATSlot& vat = g_state.vat[vat_idx];
            const size_t fifo_size = g_hw_fifo.size();
            size_t curr_offset = offset + 3;

            // Map GX Primitives to OpenGL Primitives
            int gl_mode = RL_TRIANGLES;
            if      (prim_type == 0x90) gl_mode = RL_TRIANGLES;
            else if (prim_type == 0x98) gl_mode = RL_TRIANGLES; // TriStrip (unrolled)
            else if (prim_type == 0x80) gl_mode = RL_QUADS;

            rlBegin(gl_mode);

            // If no color attribute in VAT, reset to white so we don't
            // inherit the last color set by DrawGrid or other raylib calls.
            if (vat.clrMask[0] == VtxAttrMask::None) {
                rlColor4ub(255, 255, 255, 255);
            }

            bool parse_ok = true;
            // Dynamic vertex parsing according to hardware VAT
            for (int i = 0; i < vtx_count && parse_ok; i++) {
                // 1. Position Matrices (Index 8-bit)
                // if (vat.posMatMask) { if (curr_offset+1 > fifo_size) { parse_ok=false; break; } curr_offset++; }

                // 2. Tex Matrices
                // ...

                // 3. Color0 BEFORE position — rlgl latches color per-vertex.
                //    RGB8 = 3 bytes (what PushColor() writes).
                if (vat.clrMask[0] == VtxAttrMask::Direct) {
                    if (curr_offset + 3 > fifo_size) { parse_ok = false; break; }
                    uint8_t r = g_hw_fifo[curr_offset+0];
                    uint8_t g = g_hw_fifo[curr_offset+1];
                    uint8_t b = g_hw_fifo[curr_offset+2];
                    curr_offset += 3;
                    rlColor4ub(r, g, b, 255);
                }

                // 4. Position (X, Y, Z) — Array Index 0
                if (vat.posMask != VtxAttrMask::None) {
                    float x = ReadAttribute(curr_offset, vat.posMask, vat.posType, vat.posShift, 0, fifo_size);
                    float y = ReadAttribute(curr_offset, vat.posMask, vat.posType, vat.posShift, 0, fifo_size);
                    float z = ReadAttribute(curr_offset, vat.posMask, vat.posType, vat.posShift, 0, fifo_size);
                    if (curr_offset > fifo_size) { parse_ok = false; break; }
                    rlVertex3f(x, y, z);
                }

                // 5. Normal — Array Index 1
                if (vat.nrmMask != VtxAttrMask::None) {
                    float nx = ReadAttribute(curr_offset, vat.nrmMask, vat.nrmType, 0, 1, fifo_size);
                    float ny = ReadAttribute(curr_offset, vat.nrmMask, vat.nrmType, 0, 1, fifo_size);
                    float nz = ReadAttribute(curr_offset, vat.nrmMask, vat.nrmType, 0, 1, fifo_size);
                    if (curr_offset > fifo_size) { parse_ok = false; break; }
                    rlNormal3f(nx, ny, nz);
                }

                // 6. TexCoords — Array Index 4..11
                for (int t = 0; t < 8 && parse_ok; t++) {
                    if (vat.texMask[t] != VtxAttrMask::None) {
                        float u = ReadAttribute(curr_offset, vat.texMask[t], vat.texType[t], vat.texShift[t], 4+t, fifo_size);
                        float v = ReadAttribute(curr_offset, vat.texMask[t], vat.texType[t], vat.texShift[t], 4+t, fifo_size);
                        if (curr_offset > fifo_size) { parse_ok = false; break; }
                        if (t == 0) rlTexCoord2f(u, v);
                    }
                }
            }

            rlEnd();
            // Flush rlgl batch — default holds only 8192 verts; large meshes need explicit flush.
            rlDrawRenderBatchActive();

            if (!parse_ok || curr_offset > fifo_size) break;
            offset = curr_offset;
        }
        else {
            // NOP
            offset++;
        }
    }

    if (offset > 0 && offset <= g_hw_fifo.size()) {
        g_hw_fifo.erase(g_hw_fifo.begin(), g_hw_fifo.begin() + offset);
    }
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

} // namespace

// Dolphin-accurate CP FIFO Drainer
