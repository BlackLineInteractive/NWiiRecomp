#pragma once
#include <array>
#include <cstdint>

namespace nwii::runtime::gx {

enum class VtxAttrMask { None = 0, Direct = 1, Index8 = 2, Index16 = 3 };

enum class VtxAttrType { U8 = 0, S8 = 1, U16 = 2, S16 = 3, F32 = 4 };

// Vertex Attribute Table (VAT)
struct VATSlot {
  // VCD_LO bits 0-8: per-vertex matrix indices (each one direct u8 when
  // present). Missing them desyncs the whole FIFO stream.
  bool posMatIdx;
  bool texMatIdx[8];
  VtxAttrMask posMask;
  VtxAttrType posType;
  uint8_t posShift;
  bool posElements; // false = XY, true = XYZ
  VtxAttrMask nrmMask;
  VtxAttrType nrmType;
  bool nrmElements; // false = NRM, true = NBT
  VtxAttrMask clrMask[2];
  VtxAttrType clrType[2];
  bool clrElements[2]; // false = RGB, true = RGBA
  VtxAttrMask texMask[8];
  VtxAttrType texType[8];
  uint8_t texShift[8];
  bool texElements[8]; // false = S, true = ST
};

struct TEVStage {
  uint8_t colorInA, colorInB, colorInC, colorInD;
  uint8_t colorOp, colorBias, colorScale, colorClamp, colorRegId;
  uint8_t alphaInA, alphaInB, alphaInC, alphaInD;
  uint8_t alphaOp, alphaBias, alphaScale, alphaClamp, alphaRegId;
  uint8_t texMap, texCoord, colorChan;
};

struct TexStage {
  uint32_t base_addr;
  uint32_t width;
  uint32_t height;
  uint8_t format;
  // TLUT binding for paletted formats (C4/C8/C14X2): byte offset into the
  // TLUT bank (from BP SETTLUT) and palette entry format
  // (0 = IA8, 1 = RGB565, 2 = RGB5A3).
  uint32_t tlut_offset;
  uint8_t tlut_format;
};

// Alpha test (BP 0xF3): two comparisons against reference values, combined
// by a logic op. Games use it for cutouts (fonts, foliage, UI edges) — a
// pixel that fails is discarded before blending.
struct AlphaTest {
  uint8_t ref0, ref1;
  uint8_t comp0, comp1; // CompareMode: 0=never 1=< 2=== 3=<= 4=> 5=!= 6=>= 7=always
  uint8_t logic;        // 0=and 1=or 2=xor 3=xnor
};

struct ZMode {
  uint8_t enable;
  uint8_t func;
  uint8_t update;
};

// GX
struct GXState {
  // Raw register files for shader cache hashing
  uint32_t cp[256];
  uint32_t xf[256];
  uint32_t bp[256];

  // CP (Command Processor) State. The 16 vertex arrays are: pos, nrm,
  // clr0, clr1, tex0-7, and the 4 indexed-XF arrays A-D (LOAD_INDX).
  VATSlot vat[8];
  uint32_t arrayBase[16];
  uint32_t arrayStride[16];
  // Default position/normal matrix index (CP MATINDEX_A bits 0-5); used
  // when the VCD carries no per-vertex matrix index byte.
  uint8_t defPosMtxIdx;

  // BP (Bypass / TEV) State
  uint8_t numTevStages;
  uint8_t numTexGens;
  uint8_t numChans;
  TEVStage tevStages[16];
  std::array<TexStage, 16> texStages;
  ZMode zMode;
  AlphaTest alphaTest;
  uint32_t blendMode;
  // EFB copy-clear color (BP 0x4F = A<<8|R, 0x50 = G<<8|B): the real
  // background color of the frame.
  uint32_t clearAR;
  uint32_t clearGB;
  // EFB copy (GXCopyDisp) source rectangle + destination. When PE_COPY
  // (BP 0x52) fires with the copy-to-XFB bit, the game has composed a
  // frame into the XFB buffer in main RAM; xfb* latch the last one so the
  // frame loop can present it (YUYV) regardless of the GX rasterizer.
  uint16_t efbSrcX, efbSrcY, efbW, efbH;
  uint32_t efbCopyDest;   // XFB/texture dest in RAM (BP 0x4B << 5)
  uint32_t efbCopyStride; // BP 0x4D << 5, bytes per line
  uint32_t xfbAddr;
  uint16_t xfbW, xfbH, xfbStride;
  bool pe_clear_pending;

  // XF (Transform) State
  float projection[7];
  // XF 0x1026 is an INTEGER enum (0 = perspective, 1 = orthographic),
  // not a float — reading it as one made a denormal that compares unequal
  // to 0.0 (or flushes to zero), picking the wrong projection entirely.
  uint32_t projType;
  bool projSet; // the game has loaded a projection at least once
  float posMatrices[256];

  // TLUT bank (the high 512KB of TMEM on real hardware). BP LOADTLUT
  // copies palette data here from main RAM; paletted texture decoding
  // reads entries back out. Big-endian u16 entries, as loaded.
  uint8_t tlutMem[0x80000];
  // Latched by BP LOADTLUT0 (source RAM address), consumed by LOADTLUT1.
  uint32_t tlutSrcAddr;

  uint64_t GetShaderHash(uint8_t prim_type) const {
      uint64_t hash = 14695981039346656037ull;
      auto add_u8 = [&](uint8_t b) { hash = (hash ^ b) * 1099511628211ull; };
      auto add_u32 = [&](uint32_t w) {
          add_u8(w & 0xFF); add_u8((w >> 8) & 0xFF);
          add_u8((w >> 16) & 0xFF); add_u8((w >> 24) & 0xFF);
      };

      add_u8(prim_type);
      
      // BP registers
      add_u32(bp[0x00]); // NumTexGens, NumChans, NumTevs
      add_u32(bp[0x40]); // ZMode
      add_u32(bp[0x41]); // Blend
      add_u32(bp[0x42]); // Blend logic
      add_u32(bp[0x43]); // Blend dest alpha
      add_u32(bp[0xF3]); // Alpha test
      for (int i = 0xC0; i <= 0xDF; ++i) add_u32(bp[i]); // TEV
      for (int i = 0x28; i <= 0x2F; ++i) add_u32(bp[i]); // TexCoord
      for (int i = 0xE9; i <= 0xEE; ++i) add_u32(bp[i]); // Fog
      
      // Tex formats
      for (int i = 0; i < 8; ++i) {
          add_u8(texStages[i].format);
          add_u8(texStages[i].tlut_format);
      }
      
      // CP registers
      add_u32(cp[0x50]); // VCD_LO
      add_u32(cp[0x60]); // VCD_HI
      
      // XF registers
      add_u32(xf[0x103F - 0x1000]); // NumColors
      
      return hash;
  }
};

extern GXState g_state;

} // namespace nwii::runtime::gx
