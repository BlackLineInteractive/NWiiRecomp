#include "runtime/gx/fifo_parser.h"
#include "runtime/gx_state.h"
#include "runtime/cpu_context.h"
#include <cstring>

namespace nwii::runtime {
    extern MMU* g_mmu;
}

namespace nwii::runtime::gx {

extern GXState g_state;

namespace {
    inline uint32_t Read24(const std::vector<uint8_t>& fifo, size_t offset) {
        return (fifo[offset] << 16) | (fifo[offset+1] << 8) | fifo[offset+2];
    }

    inline uint32_t Read32(const std::vector<uint8_t>& fifo, size_t offset) {
        return (fifo[offset] << 24) | (fifo[offset+1] << 16) | (fifo[offset+2] << 8) | fifo[offset+3];
    }

    void ParseBP(uint8_t reg, uint32_t val) {
        if (reg == 0x00) {
            g_state.numTexGens = (val & 0xF);
            g_state.numChans = ((val >> 4) & 0x7);
            g_state.numTevStages = ((val >> 10) & 0xF) + 1;
        } else if (reg >= 0xC0 && reg <= 0xCF) {
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
        } else if (reg >= 0x80 && reg <= 0x8F) {
            int idx = reg - 0x80;
            if (idx < (int)g_state.texStages.size()) {
                g_state.texStages[idx].width  = ((val >> 10) & 0x3FF) + 1;
                g_state.texStages[idx].height = ((val >>  0) & 0x3FF) + 1;
                g_state.texStages[idx].format = (val >> 20) & 0xF;
            }
        } else if (reg == 0x40) {
            g_state.zMode.enable  = (val >> 0) & 1;
            g_state.zMode.func    = (val >> 1) & 7;
            g_state.zMode.update  = (val >> 4) & 1;
        }
    }

    void ParseCP(uint8_t reg, uint32_t val) {
        if (reg >= 0xA0 && reg <= 0xAC) {
            g_state.arrayBase[reg - 0xA0] = val & 0x3FFFFFFF;
        } else if (reg >= 0xB0 && reg <= 0xBC) {
            g_state.arrayStride[reg - 0xB0] = val & 0xFF;
        } else if (reg >= 0x50 && reg <= 0x57) {
            int vatIdx = reg - 0x50;
            g_state.vat[vatIdx].posMask = (VtxAttrMask)((val >> 9) & 3);
            g_state.vat[vatIdx].posType = (VtxAttrType)((val >> 0) & 7);
            g_state.vat[vatIdx].posShift = (val >> 5) & 0x1F;
            g_state.vat[vatIdx].clrMask[0] = (VtxAttrMask)((val >> 13) & 3);
            g_state.vat[vatIdx].clrType[0] = (VtxAttrType)((val >> 11) & 7);
        }
    }

    float ReadAttribute(const std::vector<uint8_t>& fifo, size_t& fifo_offset, VtxAttrMask mask, VtxAttrType type,
                        uint8_t shift, uint32_t array_idx, size_t fifo_size) {
        if (mask == VtxAttrMask::None) return 0.0f;

        if (mask == VtxAttrMask::Direct) {
            if (type == VtxAttrType::F32) {
                if (fifo_offset + 4 > fifo_size) { fifo_offset = fifo_size + 1; return 0.0f; }
                uint32_t v = Read32(fifo, fifo_offset); fifo_offset += 4;
                float f; std::memcpy(&f, &v, 4); return f;
            } else if (type == VtxAttrType::U8) {
                if (fifo_offset + 1 > fifo_size) { fifo_offset = fifo_size + 1; return 0.0f; }
                uint8_t v = fifo[fifo_offset++];
                return (float)v / (float)(1 << shift);
            } else if (type == VtxAttrType::S16) {
                if (fifo_offset + 2 > fifo_size) { fifo_offset = fifo_size + 1; return 0.0f; }
                int16_t v = (int16_t)((fifo[fifo_offset] << 8) | fifo[fifo_offset+1]);
                fifo_offset += 2;
                return (float)v / (float)(1 << shift);
            }
        } else {
            uint32_t index = 0;
            if (mask == VtxAttrMask::Index8) {
                if (fifo_offset + 1 > fifo_size) { fifo_offset = fifo_size + 1; return 0.0f; }
                index = fifo[fifo_offset++];
            } else if (mask == VtxAttrMask::Index16) {
                if (fifo_offset + 2 > fifo_size) { fifo_offset = fifo_size + 1; return 0.0f; }
                index = (fifo[fifo_offset] << 8) | fifo[fifo_offset+1];
                fifo_offset += 2;
            }

            uint32_t data_addr = g_state.arrayBase[array_idx] + (index * g_state.arrayStride[array_idx]);

            if (type == VtxAttrType::F32) {
                return nwii::runtime::g_mmu->read_f32(data_addr);
            } else if (type == VtxAttrType::S16) {
                int16_t v = (int16_t)nwii::runtime::g_mmu->read16(data_addr);
                return (float)v / (float)(1 << shift);
            }
        }
        return 0.0f;
    }
}

void FifoParser::Parse(std::vector<uint8_t>& fifo, std::vector<GXCommand>& commands) {
    if (fifo.empty()) return;

    size_t offset = 0;
    while (offset < fifo.size()) {
        uint8_t cmd = fifo[offset];

        if (cmd == 0x61) {
            if (offset + 5 > fifo.size()) break;
            uint8_t reg = fifo[offset + 1];
            uint32_t val = Read24(fifo, offset + 2);
            ParseBP(reg, val);
            
            GXCommand c;
            c.type = GXCommandType::BPRegister;
            c.reg = reg;
            c.val = val;
            commands.push_back(c);
            
            offset += 5;
        } else if (cmd == 0x08) {
            if (offset + 6 > fifo.size()) break;
            uint8_t reg = fifo[offset + 1];
            uint32_t val = Read32(fifo, offset + 2);
            ParseCP(reg, val);
            
            GXCommand c;
            c.type = GXCommandType::CPRegister;
            c.reg = reg;
            c.val = val;
            commands.push_back(c);
            
            offset += 6;
        } else if (cmd == 0x10) {
            if (offset + 5 > fifo.size()) break;
            uint16_t length = (fifo[offset + 1] << 8) | fifo[offset + 2];
            uint32_t total_size = 5 + ((length + 1) * 4);
            if (offset + total_size > fifo.size()) break;
            
            GXCommand c;
            c.type = GXCommandType::XFRegister;
            c.length = length;
            commands.push_back(c);
            
            offset += total_size;
        } else if (cmd >= 0x80 && cmd <= 0x9F) {
            if (offset + 3 > fifo.size()) break;
            
            uint16_t vtx_count = (fifo[offset + 1] << 8) | fifo[offset + 2];
            uint8_t vat_idx = cmd & 0x07;
            uint8_t prim_type = cmd & 0xF8;
            
            VATSlot& vat = g_state.vat[vat_idx];
            const size_t fifo_size = fifo.size();
            size_t curr_offset = offset + 3;

            GXCommand c;
            c.type = GXCommandType::DrawPrimitive;
            
            // Map GX Primitives to OpenGL Primitives (0x0004 is RL_TRIANGLES, 0x0007 is RL_QUADS)
            c.gl_mode = 0x0004; // RL_TRIANGLES
            if      (prim_type == 0x90) c.gl_mode = 0x0004; // RL_TRIANGLES
            else if (prim_type == 0x98) c.gl_mode = 0x0004; // TriStrip (unrolled)
            else if (prim_type == 0x80) c.gl_mode = 0x0007; // RL_QUADS

            bool parse_ok = true;
            for (int i = 0; i < vtx_count && parse_ok; i++) {
                VertexData vtx;
                
                if (vat.clrMask[0] == VtxAttrMask::Direct) {
                    if (curr_offset + 3 > fifo_size) { parse_ok = false; break; }
                    vtx.has_color = true;
                    vtx.color[0] = fifo[curr_offset+0] / 255.0f;
                    vtx.color[1] = fifo[curr_offset+1] / 255.0f;
                    vtx.color[2] = fifo[curr_offset+2] / 255.0f;
                    vtx.color[3] = 1.0f;
                    curr_offset += 3;
                }

                if (vat.posMask != VtxAttrMask::None) {
                    vtx.has_pos = true;
                    vtx.pos[0] = ReadAttribute(fifo, curr_offset, vat.posMask, vat.posType, vat.posShift, 0, fifo_size);
                    vtx.pos[1] = ReadAttribute(fifo, curr_offset, vat.posMask, vat.posType, vat.posShift, 0, fifo_size);
                    vtx.pos[2] = ReadAttribute(fifo, curr_offset, vat.posMask, vat.posType, vat.posShift, 0, fifo_size);
                    if (curr_offset > fifo_size) { parse_ok = false; break; }
                }

                if (vat.nrmMask != VtxAttrMask::None) {
                    vtx.has_norm = true;
                    vtx.norm[0] = ReadAttribute(fifo, curr_offset, vat.nrmMask, vat.nrmType, 0, 1, fifo_size);
                    vtx.norm[1] = ReadAttribute(fifo, curr_offset, vat.nrmMask, vat.nrmType, 0, 1, fifo_size);
                    vtx.norm[2] = ReadAttribute(fifo, curr_offset, vat.nrmMask, vat.nrmType, 0, 1, fifo_size);
                    if (curr_offset > fifo_size) { parse_ok = false; break; }
                }

                for (int t = 0; t < 8 && parse_ok; t++) {
                    if (vat.texMask[t] != VtxAttrMask::None) {
                        vtx.has_tex[t] = true;
                        vtx.tex[t][0] = ReadAttribute(fifo, curr_offset, vat.texMask[t], vat.texType[t], vat.texShift[t], 4+t, fifo_size);
                        vtx.tex[t][1] = ReadAttribute(fifo, curr_offset, vat.texMask[t], vat.texType[t], vat.texShift[t], 4+t, fifo_size);
                        if (curr_offset > fifo_size) { parse_ok = false; break; }
                    }
                }
                
                c.vertices.push_back(vtx);
            }

            if (!parse_ok || curr_offset > fifo_size) break;
            
            commands.push_back(c);
            offset = curr_offset;
        } else {
            // NOP
            offset++;
        }
    }

    if (offset > 0 && offset <= fifo.size()) {
        fifo.erase(fifo.begin(), fifo.begin() + offset);
    }
}

} // namespace nwii::runtime::gx
