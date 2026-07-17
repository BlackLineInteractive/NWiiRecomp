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

        
        
        if (reg == 0x45 && (val & 0xF) == 2) {
            nwii::runtime::hw::pe_signal_finish();
        } else if (reg == 0x47) {
            nwii::runtime::hw::pe_signal_token(val & 0xFFFF, false);
        } else if (reg == 0x48) {
            nwii::runtime::hw::pe_signal_token(val & 0xFFFF, true);
        }

        
        // from the wrong state is cached for the rest of the run. Dolphin's

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
            
            // Layout per Dolphin BPMemory TevStageCombiner — the inputs are

            
            int stage = (reg - 0xC0) / 2;
            auto& s = g_state.tevStages[stage];
            if (((reg - 0xC0) & 1) == 0) { 
                s.colorInD    = val & 0xF;
                s.colorInC    = (val >> 4) & 0xF;
                s.colorInB    = (val >> 8) & 0xF;
                s.colorInA    = (val >> 12) & 0xF;
                s.colorBias   = (val >> 16) & 0x3;
                s.colorOp     = (val >> 18) & 0x1;
                s.colorClamp  = (val >> 19) & 0x1;
                s.colorScale  = (val >> 20) & 0x3;
                s.colorRegId  = (val >> 22) & 0x3;
            } else {                       
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
            
            // 10-19, format bits 20-23 (Dolphin BPMemory TexImage0).
            int idx = (reg >= 0x88 && reg <= 0x8B) ? (reg - 0x88) : (reg - 0xA8 + 4);
            if (idx < (int)g_state.texStages.size()) {
                g_state.texStages[idx].width  = ((val >>  0) & 0x3FF) + 1;
                g_state.texStages[idx].height = ((val >> 10) & 0x3FF) + 1;
                g_state.texStages[idx].format = (val >> 20) & 0xF;
            }
        } else if ((reg >= 0x94 && reg <= 0x97) || (reg >= 0xB4 && reg <= 0xB7)) {
            
            int idx = (reg >= 0x94 && reg <= 0x97) ? (reg - 0x94) : (reg - 0xB4 + 4);
            if (idx < (int)g_state.texStages.size()) {
                g_state.texStages[idx].base_addr = (val & 0xFFFFFF) << 5;
            }
        } else if ((reg >= 0x98 && reg <= 0x9F) || (reg >= 0xB8 && reg <= 0xBF)) {

            int idx = (reg >= 0x98 && reg <= 0x9F) ? (reg - 0x98) : (reg - 0xB8 + 4);
            if (idx < (int)g_state.texStages.size()) {
                g_state.texStages[idx].tlut_offset = (val & 0x3FF) << 9;
                g_state.texStages[idx].tlut_format = (val >> 10) & 0x3;
            }
        } else if (reg == 0x64) {

            // (Dolphin names Wind Waker and Double Dash; MP7 does it too, which

            uint32_t addr = (val & 0xFFFFFF) << 5;
            if (nwii::runtime::Config::get().platform ==
                nwii::runtime::Platform::GameCube)
                addr &= 0x01FFFFFF;
            g_state.tlutSrcAddr = addr;
        } else if (reg == 0x65) {

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

            g_state.alphaTest.ref0  = val & 0xFF;
            g_state.alphaTest.ref1  = (val >> 8) & 0xFF;
            g_state.alphaTest.comp0 = (val >> 16) & 0x7;
            g_state.alphaTest.comp1 = (val >> 19) & 0x7;
            g_state.alphaTest.logic = (val >> 22) & 0x3;
        } else if (reg == 0x4F) {
            g_state.clearAR = val & 0xFFFF; 
        } else if (reg == 0x50) {
            g_state.clearGB = val & 0xFFFF; 
        } else if (reg == 0x49) { 
            g_state.efbSrcX = val & 0x3FF;
            g_state.efbSrcY = (val >> 10) & 0x3FF;
        } else if (reg == 0x4A) { 
            g_state.efbW = (val & 0x3FF) + 1;
            g_state.efbH = ((val >> 10) & 0x3FF) + 1;
        } else if (reg == 0x4B) { 
            g_state.efbCopyDest = (val & 0xFFFFFF) << 5;
        } else if (reg == 0x4D) { 
            g_state.efbCopyStride = (val & 0x3FF) << 5;
        } else if (reg == 0x52) { 

            if (gx_trace()) {
                printf("[GXTRACE] BP 0x52 PE_COPY_EXECUTE val=0x%08X (bit14=%d bit11=%d)\n",
                       val, (val & 0x4000) != 0, (val & 0x800) != 0);
            }

            
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

    

    
    void ParseCP(uint8_t reg, uint32_t val) {
        g_state.cp[reg] = val;
        if (reg >= 0xA0 && reg <= 0xAF) {
            g_state.arrayBase[reg - 0xA0] = val & 0x3FFFFFFF;
        } else if (reg >= 0xB0 && reg <= 0xBF) {
            g_state.arrayStride[reg - 0xB0] = val & 0xFF;
        } else if (reg == 0x30) {

            g_state.defPosMtxIdx = val & 0x3F;
        } else if (reg == 0x50) {

            
            
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
            
            for (int i = 0; i < 8; i++)
                for (int t = 0; t < 8; t++)
                    g_state.vat[i].texMask[t] = (VtxAttrMask)((val >> (t * 2)) & 3);
        } else if (reg >= 0x70 && reg <= 0x77) {
            
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
            default: return 4; 
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
        } else { 
            if (fifo_offset + 2 > fifo_size) return false;
            index = (fifo[fifo_offset] << 8) | fifo[fifo_offset + 1];
            fifo_offset += 2;
        }
        if (!nwii::runtime::g_mmu) return true;
        uint32_t base = g_state.arrayBase[array_idx] + index * g_state.arrayStride[array_idx];

        

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

    
    
    int ColorBytes(VtxAttrType clrType) {
        switch ((int)clrType) {
            case 0: case 3: return 2;
            case 1: case 4: return 3;
            default:        return 4; 
        }
    }

    void DecodeColor(const uint8_t* p, VtxAttrType clrType, float out[4]) {
        switch ((int)clrType) {
            case 0: { 
                uint16_t c = (p[0] << 8) | p[1];
                out[0] = ((c >> 11) & 0x1F) / 31.0f;
                out[1] = ((c >> 5) & 0x3F) / 63.0f;
                out[2] = (c & 0x1F) / 31.0f;
                out[3] = 1.0f;
                break;
            }
            case 1: 
            case 2: 
                out[0] = p[0] / 255.0f;
                out[1] = p[1] / 255.0f;
                out[2] = p[2] / 255.0f;
                out[3] = 1.0f;
                break;
            case 3: { 
                uint16_t c = (p[0] << 8) | p[1];
                out[0] = ((c >> 12) & 0xF) / 15.0f;
                out[1] = ((c >> 8) & 0xF) / 15.0f;
                out[2] = ((c >> 4) & 0xF) / 15.0f;
                out[3] = (c & 0xF) / 15.0f;
                break;
            }
            case 4: { 
                uint32_t c = (p[0] << 16) | (p[1] << 8) | p[2];
                out[0] = ((c >> 18) & 0x3F) / 63.0f;
                out[1] = ((c >> 12) & 0x3F) / 63.0f;
                out[2] = ((c >> 6) & 0x3F) / 63.0f;
                out[3] = (c & 0x3F) / 63.0f;
                break;
            }
            default: 
                out[0] = p[0] / 255.0f;
                out[1] = p[1] / 255.0f;
                out[2] = p[2] / 255.0f;
                out[3] = p[3] / 255.0f;
                break;
        }
    }

    
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


void ApplyBPRegister(uint8_t reg, uint32_t val) { ApplyBPRegisterImpl(reg, val); }

static void ParseStream(const std::vector<uint8_t>& fifo, size_t& offset, std::vector<GXCommand>& commands, int depth);



static void ExpandDisplayList(uint32_t addr, uint32_t size,
                              std::vector<GXCommand>& commands, int depth) {
    uint32_t phys = addr & 0x1FFFFFFF;
    if (!nwii::runtime::g_mmu || size == 0 || size > 0x400000 ||
        phys + size > 0x01800000 || depth >= 4)
        return;
    std::vector<uint8_t> dl(size);

    
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

    
    if (offset > 0 && offset <= fifo.size())
        fifo.erase(fifo.begin(), fifo.begin() + offset);
}

static void ParseStream(const std::vector<uint8_t>& fifo, size_t& offset, std::vector<GXCommand>& commands, int depth) {
    const size_t fifo_size = fifo.size();
    while (offset < fifo_size) {
        uint8_t cmd = fifo[offset];

        if (cmd == 0x00) {
            
            offset++;
        } else if (cmd == 0x40) {
            
            if (offset + 9 > fifo_size) break;
            uint32_t dl_addr = Read32(fifo, offset + 1);
            uint32_t dl_size = Read32(fifo, offset + 5);
            ExpandDisplayList(dl_addr, dl_size, commands, depth);
            offset += 9;
        } else if (cmd == 0x48) {
            
            offset++;
        } else if (cmd == 0x20 || cmd == 0x28 || cmd == 0x30 || cmd == 0x38) {

            

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

            int num_floats = length + 1;
            c.payload.resize(num_floats);
            for (int i = 0; i < num_floats; ++i) {
                uint32_t val = Read32(fifo, offset + 5 + (i * 4));
                std::memcpy(&c.payload[i], &val, 4);
            }

            commands.push_back(std::move(c));

            offset += total_size;
        } else if (cmd >= 0x80 && cmd <= 0xBF) {
            
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

                vtx.has_pos = (vat.posMask != VtxAttrMask::None);
                if (!ReadVectorAttr(fifo, curr_offset, fifo_size, vat.posMask, vat.posType,
                                    vat.posShift, 0, vat.posElements ? 3 : 2, vtx.pos)) {
                    parse_ok = false; break;
                }

                if (vat.nrmMask != VtxAttrMask::None) {
                    vtx.has_norm = true;

                    float nbt[9];
                    if (!ReadVectorAttr(fifo, curr_offset, fifo_size, vat.nrmMask, vat.nrmType,
                                        0, 1, vat.nrmElements ? 9 : 3, nbt)) {
                        parse_ok = false; break;
                    }
                    vtx.norm[0] = nbt[0]; vtx.norm[1] = nbt[1]; vtx.norm[2] = nbt[2];
                }

                
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

            
            if (!parse_ok || curr_offset > fifo_size) break;

            commands.push_back(std::move(c));
            offset = curr_offset;
        } else {
            
            offset++;
        }
    }
}

} 
