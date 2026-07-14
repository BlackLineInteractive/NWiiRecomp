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

struct ZMode {
  uint8_t enable;
  uint8_t func;
  uint8_t update;
};

// GX
struct GXState {
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
  uint32_t blendMode;
  // EFB copy-clear color (BP 0x4F = A<<8|R, 0x50 = G<<8|B): the real
  // background color of the frame.
  uint32_t clearAR;
  uint32_t clearGB;

  // XF (Transform) State
  float projection[7];
  bool projSet; // the game has loaded a projection at least once
  float posMatrices[256];

  // TLUT bank (the high 512KB of TMEM on real hardware). BP LOADTLUT
  // copies palette data here from main RAM; paletted texture decoding
  // reads entries back out. Big-endian u16 entries, as loaded.
  uint8_t tlutMem[0x80000];
  // Latched by BP LOADTLUT0 (source RAM address), consumed by LOADTLUT1.
  uint32_t tlutSrcAddr;
};

extern GXState g_state;

} // namespace nwii::runtime::gx
