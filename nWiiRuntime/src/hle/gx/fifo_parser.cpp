#include "runtime/gx/fifo_parser.h"
#include "runtime/gx_state.h"
#include "runtime/cpu_context.h"
#include "runtime/hw/hw.h"
#include "runtime/config.h"
#include <cstdlib>
#include <cstring>
#include <cstdio>

namespace nwii::runtime {
    extern MMU* g_mmu;
}

namespace nwii::runtime::gx {

extern GXState g_state;

namespace {
    inline bool gx_trace() {
        static bool t = std::getenv("NWII_GXTRACE") != nullptr;
        return t;
    }

    inline uint32_t Read24(const std::vector<uint8_t>& fifo, size_t offset) {
        return (fifo[offset] << 16) | (fifo[offset+1] << 8) | fifo[offset+2];
    }

    inline uint32_t Read32(const std::vector<uint8_t>& fifo, size_t offset) {
        return (fifo[offset] << 24) | (fifo[offset+1] << 16) | (fifo[offset+2] << 8) | fifo[offset+3];
    }

    void ApplyBPRegisterImpl(uint8_t reg, uint32_t val);

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
        // Seeding the raw register file here is a look-ahead over whatever the
        // CPU thread has pushed so far, and GetShaderHash() reads bp[] — so a
        // draw's shader hash depends on thread timing, and a shader compiled
        // from the wrong state is cached for the rest of the run. Dolphin's
        // equivalent pass (LoadBPRegPreprocess) never touches bpmem; the
        // register file is written in stream order by ApplyBPRegister at render
        // time. NWII_NOSEED=1 takes the Dolphin behaviour.
        static const bool no_seed = std::getenv("NWII_NOSEED") != nullptr;
        if (!no_seed)
            g_state.bp[reg] = val;
    }

    void ApplyBPRegisterImpl(uint8_t reg, uint32_t val) {
        g_state.bp[reg] = val;
        if (reg == 0x00) {
            g_state.numTexGens = (val & 0xF);
            g_state.numChans = ((val >> 4) & 0x7);
            g_state.numTevStages = ((val >> 10) & 0xF) + 1;
        } else if (reg >= 0x28 && reg <= 0x2F) {
            // TEV order (BPMEM_TREF): each register configures TWO stages —
            // which texmap/texcoord feeds them and which rasterized color
            // channel. Without this every stage sampled texmap 0.
            int stage = (reg - 0x28) * 2;
            for (int half = 0; half < 2; ++half) {
                uint32_t f = val >> (half * 12);
                auto& s = g_state.tevStages[stage + half];
                bool enabled = (f >> 6) & 1;
                s.texMap = enabled ? (uint8_t)(f & 0x7) : 0xFF;
                s.texCoord = (f >> 3) & 0x7;
                s.colorChan = (f >> 7) & 0x7;
            }
        } else if (reg >= 0xC0 && reg <= 0xDF) {
            // TEV combiners: colour at 0xC0+2i, alpha at 0xC1+2i (16 stages).
            // Layout per Dolphin BPMemory TevStageCombiner — the inputs are
            // 4 bits (colour) / 3 bits (alpha), NOT 5, and clamp/dest sit at
            // bits 19 and 22. Reading them one bit off fed the shader
            // generator garbage for every stage.
            int stage = (reg - 0xC0) / 2;
            auto& s = g_state.tevStages[stage];
            if (((reg - 0xC0) & 1) == 0) { // ColorCombiner
                s.colorInD    = val & 0xF;
                s.colorInC    = (val >> 4) & 0xF;
                s.colorInB    = (val >> 8) & 0xF;
                s.colorInA    = (val >> 12) & 0xF;
                s.colorBias   = (val >> 16) & 0x3;
                s.colorOp     = (val >> 18) & 0x1;
                s.colorClamp  = (val >> 19) & 0x1;
                s.colorScale  = (val >> 20) & 0x3;
                s.colorRegId  = (val >> 22) & 0x3;
            } else {                       // AlphaCombiner
                s.alphaInD    = (val >> 4) & 0x7;
                s.alphaInC    = (val >> 7) & 0x7;
                s.alphaInB    = (val >> 10) & 0x7;
                s.alphaInA    = (val >> 13) & 0x7;
                s.alphaBias   = (val >> 16) & 0x3;
                s.alphaOp     = (val >> 18) & 0x1;
                s.alphaClamp  = (val >> 19) & 0x1;
                s.alphaScale  = (val >> 20) & 0x3;
                s.alphaRegId  = (val >> 22) & 0x3;
            }
        } else if ((reg >= 0x88 && reg <= 0x8B) || (reg >= 0xA8 && reg <= 0xAB)) {
            // TX_SETIMAGE0, maps 0-3 / 4-7: width bits 0-9, height bits
            // 10-19, format bits 20-23 (Dolphin BPMemory TexImage0).
            int idx = (reg >= 0x88 && reg <= 0x8B) ? (reg - 0x88) : (reg - 0xA8 + 4);
            if (idx < (int)g_state.texStages.size()) {
                g_state.texStages[idx].width  = ((val >>  0) & 0x3FF) + 1;
                g_state.texStages[idx].height = ((val >> 10) & 0x3FF) + 1;
                g_state.texStages[idx].format = (val >> 20) & 0xF;
            }
        } else if ((reg >= 0x94 && reg <= 0x97) || (reg >= 0xB4 && reg <= 0xB7)) {
            // TX_SETIMAGE3, maps 0-3 / 4-7: physical image base >> 5.
            int idx = (reg >= 0x94 && reg <= 0x97) ? (reg - 0x94) : (reg - 0xB4 + 4);
            if (idx < (int)g_state.texStages.size()) {
                g_state.texStages[idx].base_addr = (val & 0xFFFFFF) << 5;
            }
        } else if ((reg >= 0x98 && reg <= 0x9F) || (reg >= 0xB8 && reg <= 0xBF)) {
            // TX_SETTLUT, maps 0-3 / 4-7: TLUT offset (bits 0-9, <<9) and
            // palette entry format (bits 10-11).
            int idx = (reg >= 0x98 && reg <= 0x9F) ? (reg - 0x98) : (reg - 0xB8 + 4);
            if (idx < (int)g_state.texStages.size()) {
                g_state.texStages[idx].tlut_offset = (val & 0x3FF) << 9;
                g_state.texStages[idx].tlut_format = (val >> 10) & 0x3;
            }
        } else if (reg == 0x64) {
            // LOADTLUT0: source address in main RAM (>> 5). The GameCube
            // ignores the upper address bits and some games set them anyway
            // (Dolphin names Wind Waker and Double Dash; MP7 does it too, which
            // sent us reading a palette of zeros from 0x047E4FA0 instead of the
            // real one at 0x007E4FA0). Wii honours the full address.
            uint32_t addr = (val & 0xFFFFFF) << 5;
            if (nwii::runtime::Config::get().platform ==
                nwii::runtime::Platform::GameCube)
                addr &= 0x01FFFFFF;
            g_state.tlutSrcAddr = addr;
        } else if (reg == 0x65) {
            // LOADTLUT1: destination TLUT offset (bits 0-9, <<9) and count of
            // 16-entry blocks (bits 10-20). Copy palette data RAM -> TLUT bank.
            uint32_t dst = (val & 0x3FF) << 9;
            uint32_t bytes = ((val >> 10) & 0x7FF) * 32;
            if (nwii::runtime::g_mmu && dst + bytes <= sizeof(g_state.tlutMem)) {
                if (const uint8_t* p =
                        nwii::runtime::g_mmu->get_ptr(g_state.tlutSrcAddr)) {
                    std::memcpy(&g_state.tlutMem[dst], p, bytes);
                } else {
                    for (uint32_t i = 0; i < bytes; i++)
                        g_state.tlutMem[dst + i] =
                            nwii::runtime::g_mmu->read8(g_state.tlutSrcAddr + i);
                }
            }
        } else if (reg == 0x40) {
            g_state.zMode.enable  = (val >> 0) & 1;
            g_state.zMode.func    = (val >> 1) & 7;
            g_state.zMode.update  = (val >> 4) & 1;
        } else if (reg == 0xF3) {
            // BPMEM_ALPHACOMPARE: ref0[0:7] ref1[8:15] comp0[16:18]
            // comp1[19:21] logic[22:23].
            g_state.alphaTest.ref0  = val & 0xFF;
            g_state.alphaTest.ref1  = (val >> 8) & 0xFF;
            g_state.alphaTest.comp0 = (val >> 16) & 0x7;
            g_state.alphaTest.comp1 = (val >> 19) & 0x7;
            g_state.alphaTest.logic = (val >> 22) & 0x3;
        } else if (reg == 0x4F) {
            g_state.clearAR = val & 0xFFFF; // copy-clear alpha<<8 | red
        } else if (reg == 0x50) {
            g_state.clearGB = val & 0xFFFF; // copy-clear green<<8 | blue
        } else if (reg == 0x49) { // EFB_ADDR_TOP: source x,y
            g_state.efbSrcX = val & 0x3FF;
            g_state.efbSrcY = (val >> 10) & 0x3FF;
        } else if (reg == 0x4A) { // EFB_ADDR_BOTTOM: width-1, height-1
            g_state.efbW = (val & 0x3FF) + 1;
            g_state.efbH = ((val >> 10) & 0x3FF) + 1;
        } else if (reg == 0x4B) { // EFB copy dest address (>>5)
            g_state.efbCopyDest = (val & 0xFFFFFF) << 5;
        } else if (reg == 0x4D) { // display copy stride (>>5)
            g_state.efbCopyStride = (val & 0x3FF) << 5;
        } else if (reg == 0x52) { // PE_COPY_EXECUTE

            if (gx_trace()) {
                printf("[GXTRACE] BP 0x52 PE_COPY_EXECUTE val=0x%08X (bit14=%d bit11=%d)\n",
                       val, (val & 0x4000) != 0, (val & 0x800) != 0);
            }
            // Bit 14 (0x4000) = copy to XFB (vs a texture). On that copy the
            // game has finished a frame: latch the XFB for presentation and
            // signal draw-done.
            if (val & 0x4000) {
                g_state.xfbAddr = g_state.efbCopyDest;
                g_state.xfbW = g_state.efbW;
                g_state.xfbH = g_state.efbH;
                g_state.xfbStride = g_state.efbCopyStride
                                        ? g_state.efbCopyStride
                                        : (uint32_t)g_state.efbW * 2;
                g_state.frame_ready = true;
            }
            if (val & 0x800) {
                g_state.pe_clear_pending = true;
            }
            nwii::runtime::hw::pe_signal_finish();
        }
    }

    // CP register writes (via the 0x08 FIFO opcode). The vertex descriptor
    // (VCD_LO 0x50 / VCD_HI 0x60 = which attributes are present and how they
    // are referenced) is GLOBAL to the pipe, so it is mirrored into all 8 VAT
    // slots; the vertex attribute *formats* (VAT_A/B/C 0x70/0x80/0x90) are
    // per-slot. Bit layouts follow the GX SetVtxDesc/SetVtxAttrFmt encoding.
    void ParseCP(uint8_t reg, uint32_t val) {
        g_state.cp[reg] = val;
        if (reg >= 0xA0 && reg <= 0xAF) {
            g_state.arrayBase[reg - 0xA0] = val & 0x3FFFFFFF;
        } else if (reg >= 0xB0 && reg <= 0xBF) {
            g_state.arrayStride[reg - 0xB0] = val & 0xFF;
        } else if (reg == 0x30) {
            // MATINDEX_A: default matrix indices when the VCD has no
            // per-vertex index byte. Bits 0-5 = position/normal matrix.
            g_state.defPosMtxIdx = val & 0x3F;
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
        // Resolve the array region to a host pointer once. read8/read16/read_f32
        // each run the full watch/HW-reg/translate path; the model streamer
        // reads 3-9 indexed components per vertex over tens of thousands of
        // vertices, so those calls dominated parse_ms. Big-endian assembly by
        // hand matches the MMU's own read16/read32 byte order.
        const uint8_t* p = nwii::runtime::g_mmu->get_ptr(base);
        if (!p) return true;
        float inv = 1.0f / (float)(1 << shift);
        for (int i = 0; i < ncomp; i++) {
            const uint8_t* q = p + (size_t)sz * i;
            switch (type) {
                case VtxAttrType::U8:  out[i] = (float)q[0] * inv; break;
                case VtxAttrType::S8:  out[i] = (float)(int8_t)q[0] * inv; break;
                case VtxAttrType::U16: out[i] = (float)(uint16_t)((q[0] << 8) | q[1]) * inv; break;
                case VtxAttrType::S16: out[i] = (float)(int16_t)((q[0] << 8) | q[1]) * inv; break;
                default: {
                    uint32_t w = ((uint32_t)q[0] << 24) | ((uint32_t)q[1] << 16) |
                                 ((uint32_t)q[2] << 8) | q[3];
                    std::memcpy(&out[i], &w, 4);
                    break;
                }
            }
        }
        return true;
    }

    // Byte count of a color attribute in a vertex array or the FIFO, keyed
    // by the GX color format stored in clrType (GX_RGB565=0, RGB8=1,
    // RGBX8=2, RGBA4=3, RGBA6=4, RGBA8=5).
    int ColorBytes(VtxAttrType clrType) {
        switch ((int)clrType) {
            case 0: case 3: return 2;
            case 1: case 4: return 3;
            default:        return 4; // 2, 5
        }
    }

    // Decodes one color value from raw big-endian bytes per the GX format.
    void DecodeColor(const uint8_t* p, VtxAttrType clrType, float out[4]) {
        switch ((int)clrType) {
            case 0: { // RGB565
                uint16_t c = (p[0] << 8) | p[1];
                out[0] = ((c >> 11) & 0x1F) / 31.0f;
                out[1] = ((c >> 5) & 0x3F) / 63.0f;
                out[2] = (c & 0x1F) / 31.0f;
                out[3] = 1.0f;
                break;
            }
            case 1: // RGB8
            case 2: // RGBX8 (X byte present but ignored)
                out[0] = p[0] / 255.0f;
                out[1] = p[1] / 255.0f;
                out[2] = p[2] / 255.0f;
                out[3] = 1.0f;
                break;
            case 3: { // RGBA4
                uint16_t c = (p[0] << 8) | p[1];
                out[0] = ((c >> 12) & 0xF) / 15.0f;
                out[1] = ((c >> 8) & 0xF) / 15.0f;
                out[2] = ((c >> 4) & 0xF) / 15.0f;
                out[3] = (c & 0xF) / 15.0f;
                break;
            }
            case 4: { // RGBA6 (24 bits packed)
                uint32_t c = (p[0] << 16) | (p[1] << 8) | p[2];
                out[0] = ((c >> 18) & 0x3F) / 63.0f;
                out[1] = ((c >> 12) & 0x3F) / 63.0f;
                out[2] = ((c >> 6) & 0x3F) / 63.0f;
                out[3] = (c & 0x3F) / 63.0f;
                break;
            }
            default: // RGBA8
                out[0] = p[0] / 255.0f;
                out[1] = p[1] / 255.0f;
                out[2] = p[2] / 255.0f;
                out[3] = p[3] / 255.0f;
                break;
        }
    }

    // Reads a color attribute (Direct from the FIFO, or Indexed from the
    // color vertex array in guest RAM — array slots 2/3).
    bool ReadColorAttr(const std::vector<uint8_t>& fifo, size_t& fifo_offset, size_t fifo_size,
                       VtxAttrMask mask, VtxAttrType clrType, int chan, float out[4]) {
        out[0] = out[1] = out[2] = out[3] = 1.0f;
        if (mask == VtxAttrMask::None) return true;

        int nbytes = ColorBytes(clrType);
        if (mask == VtxAttrMask::Direct) {
            if (fifo_offset + (size_t)nbytes > fifo_size) return false;
            DecodeColor(&fifo[fifo_offset], clrType, out);
            fifo_offset += nbytes;
            return true;
        }

        uint32_t index = 0;
        if (mask == VtxAttrMask::Index8) {
            if (fifo_offset + 1 > fifo_size) return false;
            index = fifo[fifo_offset++];
        } else {
            if (fifo_offset + 2 > fifo_size) return false;
            index = (fifo[fifo_offset] << 8) | fifo[fifo_offset + 1];
            fifo_offset += 2;
        }
        if (!nwii::runtime::g_mmu) return true;
        uint32_t base = g_state.arrayBase[2 + chan] + index * g_state.arrayStride[2 + chan];
        const uint8_t* p = nwii::runtime::g_mmu->get_ptr(base);
        if (!p) return true;
        DecodeColor(p, clrType, out);
        return true;
    }
}

// Applies one BP register's rendering state. Called by the renderer as it
// walks the command list, so each draw sees its own state.
void ApplyBPRegister(uint8_t reg, uint32_t val) { ApplyBPRegisterImpl(reg, val); }

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
    // Bulk-copy from the host MEM1 pointer instead of size× read8 (each
    // read8 runs the full watch/HW-reg/translate path). Display lists are
    // the bulk of MP7's geometry, so this dominated parse time.
    if (const uint8_t* p = nwii::runtime::g_mmu->get_ptr(0x80000000u | phys)) {
        std::memcpy(dl.data(), p, size);
    } else {
        for (uint32_t i = 0; i < size; i++)
            dl[i] = nwii::runtime::g_mmu->read8(addr + i);
    }
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
            // LOAD_INDX A-D: indexed load into XF memory from CP array 12-15
            // (position matrices, normal matrices, tex matrices, light data).
            // Payload: index (bits 16-31), XF address (bits 0-11), count-1
            // (bits 12-15). Emitted as an XFRegister command so it applies in
            // stream order alongside direct XF loads.
            if (offset + 5 > fifo_size) break;
            uint32_t val = Read32(fifo, offset + 1);
            int array = 12 + (cmd - 0x20) / 8;
            uint32_t index = val >> 16;
            uint32_t xf_addr = val & 0xFFF;
            int num = ((val >> 12) & 0xF) + 1;
            if (nwii::runtime::g_mmu) {
                GXCommand c;
                c.type = GXCommandType::XFRegister;
                c.reg = xf_addr;
                c.length = num - 1;
                c.payload.resize(num);
                uint32_t base = g_state.arrayBase[array] +
                                index * g_state.arrayStride[array];
                for (int i = 0; i < num; i++)
                    c.payload[i] = nwii::runtime::g_mmu->read_f32(base + i * 4);
                if (gx_trace()) {
                    static int ln = 0;
                    if (ln++ < 24) {
                        printf("[GXTRACE] LOAD_INDX arr=%d idx=%u xf=0x%03X n=%d base=0x%X stride=%u [",
                               array, index, xf_addr, num, base,
                               g_state.arrayStride[array]);
                        for (int i = 0; i < num && i < 8; i++)
                            printf("%.3f ", c.payload[i]);
                        printf("]\n");
                    }
                }
                commands.push_back(std::move(c));
            }
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
            commands.push_back(std::move(c));

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
            commands.push_back(std::move(c));

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
            c.reg = (fifo[offset + 3] << 8) | fifo[offset + 4];

            if (gx_trace() && c.reg >= 0x1020 && c.reg <= 0x1027) {
                static int xf_print_proj = 0;
                if (xf_print_proj++ < 5)
                    printf("[GXTRACE] XF proj load: len=%d reg=0x%04X\n", length, c.reg);
            }
            if (gx_trace() && c.reg < 0x100) {
                static int xf_print_mtx = 0;
                if (xf_print_mtx++ < 24)
                    printf("[GXTRACE] XF direct mtx: reg=0x%03X len=%d\n", c.reg, length + 1);
            }

            // Read payload (length+1) * 4 bytes as floats
            int num_floats = length + 1;
            c.payload.resize(num_floats);
            for (int i = 0; i < num_floats; ++i) {
                uint32_t val = Read32(fifo, offset + 5 + (i * 4));
                std::memcpy(&c.payload[i], &val, 4);
            }

            commands.push_back(std::move(c));

            offset += total_size;
        } else if (cmd >= 0x80 && cmd <= 0xBF) {
            // Draw primitive: prim type in bits 3-7, VAT slot in bits 0-2.
            if (offset + 3 > fifo_size) break;

            uint16_t vtx_count = (fifo[offset + 1] << 8) | fifo[offset + 2];
            uint8_t vat_idx = cmd & 0x07;

            VATSlot& vat = g_state.vat[vat_idx];
            size_t curr_offset = offset + 3;

            GXCommand c;
            c.type = GXCommandType::DrawPrimitive;
            c.prim_type = cmd & 0xF8;

            bool parse_ok = true;
            for (int i = 0; i < vtx_count && parse_ok; i++) {
                VertexData vtx;

                // Matrix indices (direct u8 each) precede all attributes.
                // Without a per-vertex index the CP default applies.
                vtx.posMtxIdx = g_state.defPosMtxIdx;
                {
                    int midx_offset = 0;
                    if (vat.posMatIdx) {
                        if (curr_offset + midx_offset + 1 > fifo_size) { parse_ok = false; break; }
                        vtx.posMtxIdx = fifo[curr_offset + midx_offset];
                        midx_offset++;
                    }
                    for (int t = 0; t < 8; t++) {
                        if (vat.texMatIdx[t]) {
                            if (curr_offset + midx_offset + 1 > fifo_size) { parse_ok = false; break; }
                            vtx.texMtxIdx[t] = fifo[curr_offset + midx_offset];
                            midx_offset++;
                        }
                    }
                    curr_offset += midx_offset;
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

                // Both color channels are consumed to keep the stream in
                // sync; CLR0 drives rendering.
                for (int ci = 0; ci < 2 && parse_ok; ci++) {
                    if (vat.clrMask[ci] == VtxAttrMask::None) continue;
                    float col[4];
                    if (!ReadColorAttr(fifo, curr_offset, fifo_size, vat.clrMask[ci],
                                       vat.clrType[ci], ci, col)) {
                        parse_ok = false; break;
                    }
                    if (ci == 0) {
                        vtx.has_color = true;
                        vtx.color[0] = col[0]; vtx.color[1] = col[1];
                        vtx.color[2] = col[2]; vtx.color[3] = col[3];
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

            commands.push_back(std::move(c));
            offset = curr_offset;
        } else {
            // Unknown opcode: skip a byte to stay in sync (best effort).
            offset++;
        }
    }
}

} // namespace nwii::runtime::gx
