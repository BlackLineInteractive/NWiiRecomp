#include "runtime/gx/fifo_parser.h"
#include "runtime/gx_state.h"
#include "runtime/cpu_context.h"
#include "runtime/hw/hw.h"
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
        // PE signals live in the BP stream: reg 0x45 val&0xF==2 is the
        // draw-done strobe (GXSetDrawDone), 0x47/0x48 carry the draw-sync
        // token (0x48 also raises the token interrupt). Signalling from the
        // parser sees tokens inside display lists too.
        if (reg == 0x45 && (val & 0xF) == 2) {
            nwii::runtime::hw::pe_signal_finish();
        } else if (reg == 0x47) {
            nwii::runtime::hw::pe_signal_token(val & 0xFFFF, false);
        } else if (reg == 0x48) {
            nwii::runtime::hw::pe_signal_token(val & 0xFFFF, true);
        }
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

    // CP register writes (via the 0x08 FIFO opcode). The vertex descriptor
    // (VCD_LO 0x50 / VCD_HI 0x60 = which attributes are present and how they
    // are referenced) is GLOBAL to the pipe, so it is mirrored into all 8 VAT
    // slots; the vertex attribute *formats* (VAT_A/B/C 0x70/0x80/0x90) are
    // per-slot. Bit layouts follow the GX SetVtxDesc/SetVtxAttrFmt encoding.
    void ParseCP(uint8_t reg, uint32_t val) {
        if (reg >= 0xA0 && reg <= 0xAC) {
            g_state.arrayBase[reg - 0xA0] = val & 0x3FFFFFFF;
        } else if (reg >= 0xB0 && reg <= 0xBC) {
            g_state.arrayStride[reg - 0xB0] = val & 0xFF;
        } else if (reg == 0x50) {
            // VCD_LO (global): matrix indices + pos/nrm/col0/col1 presence.
            // Bit 0 = PosNrm matrix index, bits 1-8 = tex0-7 matrix indices —
            // each adds one direct u8 per vertex and MUST be consumed or the
            // whole stream desyncs.
            for (int i = 0; i < 8; i++) {
                g_state.vat[i].posMatIdx  = (val >> 0) & 1;
                for (int t = 0; t < 8; t++)
                    g_state.vat[i].texMatIdx[t] = (val >> (1 + t)) & 1;
                g_state.vat[i].posMask    = (VtxAttrMask)((val >> 9)  & 3);
                g_state.vat[i].nrmMask    = (VtxAttrMask)((val >> 11) & 3);
                g_state.vat[i].clrMask[0] = (VtxAttrMask)((val >> 13) & 3);
                g_state.vat[i].clrMask[1] = (VtxAttrMask)((val >> 15) & 3);
            }
        } else if (reg == 0x60) {
            // VCD_HI (global): tex0..tex7 presence + index mode.
            for (int i = 0; i < 8; i++)
                for (int t = 0; t < 8; t++)
                    g_state.vat[i].texMask[t] = (VtxAttrMask)((val >> (t * 2)) & 3);
        } else if (reg >= 0x70 && reg <= 0x77) {
            // VAT_A: pos/nrm/col0/col1/tex0 formats for one slot.
            int i = reg - 0x70;
            g_state.vat[i].posElements  = (val >> 0)  & 1;
            g_state.vat[i].posType      = (VtxAttrType)((val >> 1) & 7);
            g_state.vat[i].posShift     = (val >> 4)  & 0x1F;
            g_state.vat[i].nrmElements  = (val >> 9)  & 1;
            g_state.vat[i].nrmType      = (VtxAttrType)((val >> 10) & 7);
            g_state.vat[i].clrElements[0] = (val >> 13) & 1;
            g_state.vat[i].clrType[0]   = (VtxAttrType)((val >> 14) & 7);
            g_state.vat[i].clrElements[1] = (val >> 17) & 1;
            g_state.vat[i].clrType[1]   = (VtxAttrType)((val >> 18) & 7);
            g_state.vat[i].texElements[0] = (val >> 21) & 1;
            g_state.vat[i].texType[0]   = (VtxAttrType)((val >> 22) & 7);
            g_state.vat[i].texShift[0]  = (val >> 25) & 0x1F;
        } else if (reg >= 0x80 && reg <= 0x87) {
            // VAT_B: tex1..tex4 formats for one slot.
            int i = reg - 0x80;
            g_state.vat[i].texElements[1] = (val >> 0)  & 1;
            g_state.vat[i].texType[1]   = (VtxAttrType)((val >> 1) & 7);
            g_state.vat[i].texShift[1]  = (val >> 4)  & 0x1F;
            g_state.vat[i].texElements[2] = (val >> 9)  & 1;
            g_state.vat[i].texType[2]   = (VtxAttrType)((val >> 10) & 7);
            g_state.vat[i].texShift[2]  = (val >> 13) & 0x1F;
            g_state.vat[i].texElements[3] = (val >> 18) & 1;
            g_state.vat[i].texType[3]   = (VtxAttrType)((val >> 19) & 7);
            g_state.vat[i].texShift[3]  = (val >> 22) & 0x1F;
            g_state.vat[i].texElements[4] = (val >> 27) & 1;
            g_state.vat[i].texType[4]   = (VtxAttrType)((val >> 28) & 7);
        } else if (reg >= 0x90 && reg <= 0x97) {
            // VAT_C: tex4 shift + tex5..tex7 formats for one slot.
            int i = reg - 0x90;
            g_state.vat[i].texShift[4]  = (val >> 0)  & 0x1F;
            g_state.vat[i].texElements[5] = (val >> 5)  & 1;
            g_state.vat[i].texType[5]   = (VtxAttrType)((val >> 6) & 7);
            g_state.vat[i].texShift[5]  = (val >> 9)  & 0x1F;
            g_state.vat[i].texElements[6] = (val >> 14) & 1;
            g_state.vat[i].texType[6]   = (VtxAttrType)((val >> 15) & 7);
            g_state.vat[i].texShift[6]  = (val >> 18) & 0x1F;
            g_state.vat[i].texElements[7] = (val >> 23) & 1;
            g_state.vat[i].texType[7]   = (VtxAttrType)((val >> 24) & 7);
            g_state.vat[i].texShift[7]  = (val >> 27) & 0x1F;
        }
    }

    inline int TypeSize(VtxAttrType type) {
        switch (type) {
            case VtxAttrType::U8: case VtxAttrType::S8: return 1;
            case VtxAttrType::U16: case VtxAttrType::S16: return 2;
            default: return 4; // F32
        }
    }

    inline float DecodeScalar(const uint8_t* p, VtxAttrType type, uint8_t shift) {
        switch (type) {
            case VtxAttrType::U8:  return (float)p[0] / (float)(1 << shift);
            case VtxAttrType::S8:  return (float)(int8_t)p[0] / (float)(1 << shift);
            case VtxAttrType::U16: return (float)(uint16_t)((p[0] << 8) | p[1]) / (float)(1 << shift);
            case VtxAttrType::S16: return (float)(int16_t)((p[0] << 8) | p[1]) / (float)(1 << shift);
            default: {
                uint32_t v = ((uint32_t)p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
                float f; std::memcpy(&f, &v, 4); return f;
            }
        }
    }

    // Reads an entire multi-component attribute and advances fifo_offset by its
    // real FIFO footprint: Direct consumes ncomp scalars inline; Index8/16
    // consumes exactly ONE index (never one per component — that desyncs the
    // stream) and fetches the components from the attribute array in guest RAM.
    // Returns false when the FIFO is truncated mid-attribute.
    bool ReadVectorAttr(const std::vector<uint8_t>& fifo, size_t& fifo_offset, size_t fifo_size,
                        VtxAttrMask mask, VtxAttrType type, uint8_t shift,
                        uint32_t array_idx, int ncomp, float* out) {
        for (int i = 0; i < ncomp; i++) out[i] = 0.0f;
        if (mask == VtxAttrMask::None) return true;

        int sz = TypeSize(type);
        if (mask == VtxAttrMask::Direct) {
            if (fifo_offset + (size_t)sz * ncomp > fifo_size) return false;
            for (int i = 0; i < ncomp; i++)
                out[i] = DecodeScalar(&fifo[fifo_offset + (size_t)sz * i], type, shift);
            fifo_offset += (size_t)sz * ncomp;
            return true;
        }

        uint32_t index = 0;
        if (mask == VtxAttrMask::Index8) {
            if (fifo_offset + 1 > fifo_size) return false;
            index = fifo[fifo_offset++];
        } else { // Index16
            if (fifo_offset + 2 > fifo_size) return false;
            index = (fifo[fifo_offset] << 8) | fifo[fifo_offset + 1];
            fifo_offset += 2;
        }
        if (!nwii::runtime::g_mmu) return true;
        uint32_t base = g_state.arrayBase[array_idx] + index * g_state.arrayStride[array_idx];
        for (int i = 0; i < ncomp; i++) {
            uint32_t a = base + (uint32_t)sz * i;
            switch (type) {
                case VtxAttrType::U8:  out[i] = (float)nwii::runtime::g_mmu->read8(a) / (float)(1 << shift); break;
                case VtxAttrType::S8:  out[i] = (float)(int8_t)nwii::runtime::g_mmu->read8(a) / (float)(1 << shift); break;
                case VtxAttrType::U16: out[i] = (float)(uint16_t)nwii::runtime::g_mmu->read16(a) / (float)(1 << shift); break;
                case VtxAttrType::S16: out[i] = (float)(int16_t)nwii::runtime::g_mmu->read16(a) / (float)(1 << shift); break;
                default:               out[i] = nwii::runtime::g_mmu->read_f32(a); break;
            }
        }
        return true;
    }

    // Byte count of a Direct color attribute, keyed by the GX color format
    // stored in clrType (GX_RGB565=0, RGB8=1, RGBX8=2, RGBA4=3, RGBA6=4, RGBA8=5).
    int DirectColorBytes(VtxAttrType clrType) {
        switch ((int)clrType) {
            case 0: case 3: return 2;
            case 1: case 4: return 3;
            default:        return 4; // 2, 5
        }
    }
}

static void ParseStream(const std::vector<uint8_t>& fifo, size_t& offset, std::vector<GXCommand>& commands, int depth);

// GX_CMD_CALL_DL (0x40): addr + size of a display list in guest RAM. Real
// hardware re-reads those bytes through the same command processor, so we
// fetch them and parse recursively (bounded depth guards against garbage).
static void ExpandDisplayList(uint32_t addr, uint32_t size,
                              std::vector<GXCommand>& commands, int depth) {
    uint32_t phys = addr & 0x1FFFFFFF;
    if (!nwii::runtime::g_mmu || size == 0 || size > 0x400000 ||
        phys + size > 0x01800000 || depth >= 4)
        return;
    std::vector<uint8_t> dl(size);
    for (uint32_t i = 0; i < size; i++)
        dl[i] = nwii::runtime::g_mmu->read8(addr + i);
    size_t off = 0;
    ParseStream(dl, off, commands, depth + 1);
}

void FifoParser::Parse(std::vector<uint8_t>& fifo, std::vector<GXCommand>& commands) {
    size_t offset = 0;
    ParseStream(fifo, offset, commands, 0);
    // Drop only the fully-parsed prefix; a trailing incomplete command stays in
    // the buffer so the next WGP chunk can complete it. (Clearing the whole
    // buffer here would split commands and desync the stream permanently.)
    if (offset > 0 && offset <= fifo.size())
        fifo.erase(fifo.begin(), fifo.begin() + offset);
}

static void ParseStream(const std::vector<uint8_t>& fifo, size_t& offset, std::vector<GXCommand>& commands, int depth) {
    const size_t fifo_size = fifo.size();
    while (offset < fifo_size) {
        uint8_t cmd = fifo[offset];

        if (cmd == 0x00) {
            // NOP
            offset++;
        } else if (cmd == 0x40) {
            // CALL_DL: 1 cmd + 4 addr + 4 size
            if (offset + 9 > fifo_size) break;
            uint32_t dl_addr = Read32(fifo, offset + 1);
            uint32_t dl_size = Read32(fifo, offset + 5);
            ExpandDisplayList(dl_addr, dl_size, commands, depth);
            offset += 9;
        } else if (cmd == 0x48) {
            // INVL_VC: invalidate vertex cache, single byte
            offset++;
        } else if (cmd == 0x20 || cmd == 0x28 || cmd == 0x30 || cmd == 0x38) {
            // LOAD_INDX A-D: 1 cmd + 4 payload
            if (offset + 5 > fifo_size) break;
            offset += 5;
        } else if (cmd == 0x61) {
            // BP register load: 1 cmd + 1 reg + 3 value
            if (offset + 5 > fifo_size) break;
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
            // CP register load: 1 cmd + 1 reg + 4 value
            if (offset + 6 > fifo_size) break;
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
            // LOAD_XF_REG: 1 cmd + 2 length + 2 reg + (length+1)*4 payload
            if (offset + 5 > fifo_size) break;
            uint16_t length = (fifo[offset + 1] << 8) | fifo[offset + 2];
            uint32_t total_size = 5 + ((length + 1) * 4);
            if (offset + total_size > fifo_size) break;

            GXCommand c;
            c.type = GXCommandType::XFRegister;
            c.length = length;
            commands.push_back(c);

            offset += total_size;
        } else if (cmd >= 0x80 && cmd <= 0xBF) {
            // Draw primitive: prim type in bits 3-7, VAT slot in bits 0-2.
            if (offset + 3 > fifo_size) break;

            uint16_t vtx_count = (fifo[offset + 1] << 8) | fifo[offset + 2];
            uint8_t vat_idx = cmd & 0x07;
            uint8_t prim_type = cmd & 0xF8;

            VATSlot& vat = g_state.vat[vat_idx];
            size_t curr_offset = offset + 3;

            GXCommand c;
            c.type = GXCommandType::DrawPrimitive;

            // Map GX primitives to raylib/rlgl modes (0x0004 = RL_TRIANGLES, 0x0007 = RL_QUADS).
            c.gl_mode = 0x0004; // default RL_TRIANGLES
            if      (prim_type == 0x90) c.gl_mode = 0x0004; // triangles
            else if (prim_type == 0x98) c.gl_mode = 0x0004; // tristrip (unrolled)
            else if (prim_type == 0x80) c.gl_mode = 0x0007; // quads

            bool parse_ok = true;
            for (int i = 0; i < vtx_count && parse_ok; i++) {
                VertexData vtx;

                // Matrix indices (direct u8 each) precede all attributes.
                {
                    int midx_bytes = vat.posMatIdx ? 1 : 0;
                    for (int t = 0; t < 8; t++)
                        if (vat.texMatIdx[t]) midx_bytes++;
                    if (midx_bytes) {
                        if (curr_offset + midx_bytes > fifo_size) { parse_ok = false; break; }
                        curr_offset += midx_bytes;
                    }
                }

                // Position: XY or XYZ per the VAT element flag.
                vtx.has_pos = (vat.posMask != VtxAttrMask::None);
                if (!ReadVectorAttr(fifo, curr_offset, fifo_size, vat.posMask, vat.posType,
                                    vat.posShift, 0, vat.posElements ? 3 : 2, vtx.pos)) {
                    parse_ok = false; break;
                }

                if (vat.nrmMask != VtxAttrMask::None) {
                    vtx.has_norm = true;
                    // NBT carries binormal + tangent behind the normal (9
                    // scalars inline, still ONE index when indexed).
                    float nbt[9];
                    if (!ReadVectorAttr(fifo, curr_offset, fifo_size, vat.nrmMask, vat.nrmType,
                                        0, 1, vat.nrmElements ? 9 : 3, nbt)) {
                        parse_ok = false; break;
                    }
                    vtx.norm[0] = nbt[0]; vtx.norm[1] = nbt[1]; vtx.norm[2] = nbt[2];
                }

                // Both color channels (CLR0 drives rendering, CLR1 is only
                // consumed — skipping it desyncs the stream).
                for (int ci = 0; ci < 2 && parse_ok; ci++) {
                    if (vat.clrMask[ci] == VtxAttrMask::Direct) {
                        int clrBytes = DirectColorBytes(vat.clrType[ci]);
                        if (curr_offset + clrBytes > fifo_size) { parse_ok = false; break; }
                        if (ci == 0) {
                            vtx.has_color = true;
                            if (clrBytes >= 3) {
                                vtx.color[0] = fifo[curr_offset + 0] / 255.0f;
                                vtx.color[1] = fifo[curr_offset + 1] / 255.0f;
                                vtx.color[2] = fifo[curr_offset + 2] / 255.0f;
                                vtx.color[3] = (clrBytes == 4) ? (fifo[curr_offset + 3] / 255.0f) : 1.0f;
                            } else {
                                // Packed 16-bit color (RGB565/RGBA4): approximate as white.
                                vtx.color[0] = vtx.color[1] = vtx.color[2] = vtx.color[3] = 1.0f;
                            }
                        }
                        curr_offset += clrBytes;
                    } else if (vat.clrMask[ci] == VtxAttrMask::Index8) {
                        if (curr_offset + 1 > fifo_size) { parse_ok = false; break; }
                        curr_offset += 1;
                        if (ci == 0) {
                            vtx.has_color = true;
                            vtx.color[0] = vtx.color[1] = vtx.color[2] = vtx.color[3] = 1.0f;
                        }
                    } else if (vat.clrMask[ci] == VtxAttrMask::Index16) {
                        if (curr_offset + 2 > fifo_size) { parse_ok = false; break; }
                        curr_offset += 2;
                        if (ci == 0) {
                            vtx.has_color = true;
                            vtx.color[0] = vtx.color[1] = vtx.color[2] = vtx.color[3] = 1.0f;
                        }
                    }
                }
                if (!parse_ok) break;

                for (int t = 0; t < 8 && parse_ok; t++) {
                    if (vat.texMask[t] != VtxAttrMask::None) {
                        vtx.has_tex[t] = true;
                        if (!ReadVectorAttr(fifo, curr_offset, fifo_size, vat.texMask[t],
                                            vat.texType[t], vat.texShift[t], 4 + t,
                                            vat.texElements[t] ? 2 : 1, vtx.tex[t])) {
                            parse_ok = false; break;
                        }
                    }
                }

                c.vertices.push_back(vtx);
            }

            // Incomplete draw: leave the whole command in the buffer for the
            // next chunk (offset is not advanced past it).
            if (!parse_ok || curr_offset > fifo_size) break;

            commands.push_back(c);
            offset = curr_offset;
        } else {
            // Unknown opcode: skip a byte to stay in sync (best effort).
            offset++;
        }
    }
}

} // namespace nwii::runtime::gx
