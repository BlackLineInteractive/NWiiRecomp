#pragma once
#include <array>
#include <cstdint>

namespace nwii::runtime::gx {

enum class VtxAttrMask { None = 0, Direct = 1, Index8 = 2, Index16 = 3 };

enum class VtxAttrType { U8 = 0, S8 = 1, U16 = 2, S16 = 3, F32 = 4 };

struct VATSlot {

  bool posMatIdx;
  bool texMatIdx[8];
  VtxAttrMask posMask;
  VtxAttrType posType;
  uint8_t posShift;
  bool posElements; 
  VtxAttrMask nrmMask;
  VtxAttrType nrmType;
  bool nrmElements; 
  VtxAttrMask clrMask[2];
  VtxAttrType clrType[2];
  bool clrElements[2]; 
  VtxAttrMask texMask[8];
  VtxAttrType texType[8];
  uint8_t texShift[8];
  bool texElements[8]; 
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

  
  uint32_t tlut_offset;
  uint8_t tlut_format;
};



struct AlphaTest {
  uint8_t ref0 = 0, ref1 = 0;

  
  uint8_t comp0 = 7, comp1 = 7;
  uint8_t logic = 0; 
};

struct ZMode {
  uint8_t enable;
  uint8_t func;
  uint8_t update;
};

struct GXState {
  
  uint32_t cp[256];
  uint32_t xf[256];
  uint32_t bp[256];

  
  VATSlot vat[8];
  uint32_t arrayBase[16];
  uint32_t arrayStride[16];

  uint8_t defPosMtxIdx;
  // Default texture-matrix index per texcoord (CP matrix index A/B, tex fields).
  // Overridden per-vertex when the VAT has texMatIdx set.
  uint8_t defTexMtxIdx[8] = {0};

  uint8_t numTevStages;
  uint8_t numTexGens;
  uint8_t numChans;
  TEVStage tevStages[16];
  std::array<TexStage, 16> texStages;
  ZMode zMode;
  AlphaTest alphaTest;
  uint32_t blendMode;

  uint32_t clearAR;
  uint32_t clearGB;

  
  
  uint16_t efbSrcX, efbSrcY, efbW, efbH;
  uint32_t efbCopyDest;   
  uint32_t efbCopyStride; 
  uint32_t xfbAddr;
  uint16_t xfbW, xfbH, xfbStride;
  bool pe_clear_pending;
  bool frame_ready; 

  float projection[7];

  
  uint32_t projType;
  bool projSet; 
  float posMatrices[256];
  // XF 0x0400 normal matrices, 0x0600 lights (8 x 16 words, Light layout:
  // 3 unused, colour as packed RGBA, cosatt[3], distatt[3], pos[3], dir[3]).
  float normalMatrices[96];
  float lights[8][16];

  
  
  uint8_t tlutMem[0x80000];
  
  uint32_t tlutSrcAddr;

  uint64_t GetShaderHash(uint8_t prim_type) const {
      uint64_t hash = 14695981039346656037ull;
      auto add_u8 = [&](uint8_t b) { hash = (hash ^ b) * 1099511628211ull; };
      auto add_u32 = [&](uint32_t w) {
          add_u8(w & 0xFF); add_u8((w >> 8) & 0xFF);
          add_u8((w >> 16) & 0xFF); add_u8((w >> 24) & 0xFF);
      };

      add_u8(prim_type);

      add_u32(bp[0x00]); 
      add_u32(bp[0x40]); 
      add_u32(bp[0x41]); 
      add_u32(bp[0x42]); 
      add_u32(bp[0x43]); 
      add_u32(bp[0xF3]); 
      for (int i = 0xC0; i <= 0xDF; ++i) add_u32(bp[i]); 
      for (int i = 0x28; i <= 0x2F; ++i) add_u32(bp[i]); 
      for (int i = 0xE9; i <= 0xEE; ++i) add_u32(bp[i]); 

      for (int i = 0; i < 8; ++i) {
          add_u8(texStages[i].format);
          add_u8(texStages[i].tlut_format);
      }

      add_u32(cp[0x50]); 
      add_u32(cp[0x60]); 

      add_u32(xf[0x103F - 0x1000]); 
      
      return hash;
  }
};

extern GXState g_state;

} 
