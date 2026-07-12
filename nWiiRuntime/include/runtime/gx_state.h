#pragma once
#include <array>
#include <cstdint>

namespace nwii::runtime::gx {

enum class VtxAttrMask { None = 0, Direct = 1, Index8 = 2, Index16 = 3 };

enum class VtxAttrType { U8 = 0, S8 = 1, U16 = 2, S16 = 3, F32 = 4 };

// Vertex Attribute Table (VAT)
struct VATSlot {
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
  uint32_t width;
  uint32_t height;
  uint8_t format;
};

struct ZMode {
  uint8_t enable;
  uint8_t func;
  uint8_t update;
};

// GX
struct GXState {
  // CP (Command Processor) State
  VATSlot vat[8];
  uint32_t arrayBase[13];
  uint32_t arrayStride[13];

  // BP (Bypass / TEV) State
  uint8_t numTevStages;
  uint8_t numTexGens;
  uint8_t numChans;
  TEVStage tevStages[16];
  std::array<TexStage, 16> texStages;
  ZMode zMode;
  uint32_t blendMode;

  // XF (Transform) State
  float projection[6];
  float posMatrices[256];
};

extern GXState g_state;

} // namespace nwii::runtime::gx
