#pragma once
#include <cstdint>
#include <array>

namespace nwii::runtime::gx {

// Формати присутності атрибутів
enum class VtxAttrMask { None = 0, Direct = 1, Index8 = 2, Index16 = 3 };
// Типи даних атрибутів
enum class VtxAttrType { U8 = 0, S8 = 1, U16 = 2, S16 = 3, F32 = 4 };

// Слот Vertex Attribute Table (VAT)
struct VATSlot {
    VtxAttrMask posMask; VtxAttrType posType; uint8_t posShift;
    VtxAttrMask nrmMask; VtxAttrType nrmType;
    VtxAttrMask clrMask[2]; VtxAttrType clrType[2];
    VtxAttrMask texMask[8]; VtxAttrType texType[8]; uint8_t texShift[8];
};

// TEV Stage Конфігурація
struct TEVStage {
    uint8_t colorInA, colorInB, colorInC, colorInD;
    uint8_t colorOp, colorBias, colorScale, colorClamp, colorRegId;
    uint8_t alphaInA, alphaInB, alphaInC, alphaInD;
    uint8_t alphaOp, alphaBias, alphaScale, alphaClamp, alphaRegId;
    uint8_t texMap, texCoord, colorChan;
};

// Глобальний стан GX
struct GXState {
    // CP (Command Processor) State
    VATSlot vat[8];
    uint32_t arrayBase[13]; // Вказівники на масиви вершин у пам'яті (MEM1/MEM2)
    uint32_t arrayStride[13];
    
    // BP (Bypass / TEV) State
    uint8_t numTevStages;
    uint8_t numTexGens;
    uint8_t numChans;
    TEVStage tevStages[16];
    uint32_t zMode;
    uint32_t blendMode;
    
    // XF (Transform) State
    float projection[6];
    float posMatrices[256]; // Матриці трансформації
};

extern GXState g_state;

} // namespace nwii::runtime::gx
