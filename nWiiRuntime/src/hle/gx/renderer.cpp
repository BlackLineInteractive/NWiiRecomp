#include "runtime/gx/renderer.h"
#include "runtime/cpu_context.h"
#include "runtime/gx_state.h"
#include <cstdint>
#include <map>
#include <raylib.h>
#include <rlgl.h>
#include <vector>

#include "runtime/cpu_context.h"

namespace nwii::runtime {
extern MMU *g_mmu;
}

namespace nwii::runtime::gx {

std::map<uint32_t, Texture2D> s_texture_cache;

// Texture decoding helper
static void LoadTextureIfNecessary(uint32_t base_addr, uint32_t width,
                                   uint32_t height, uint8_t format) {
  if (base_addr == 0 || width == 0 || height == 0)
    return;
  if (s_texture_cache.find(base_addr) != s_texture_cache.end())
    return; // Already loaded

  if (!nwii::runtime::g_mmu)
    return;

  // Allocate RGBA8 buffer
  std::vector<uint8_t> pixels(width * height * 4, 255);

  // Memory starts at base_addr in guest RAM
  uint32_t addr = base_addr;

  if (format == 0x0) { // GX_TF_I4 (8x8 blocks, 32 bytes)
    for (uint32_t by = 0; by < height; by += 8) {
      for (uint32_t bx = 0; bx < width; bx += 8) {
        uint32_t block_addr = addr;
        for (int y = 0; y < 8; ++y) {
          for (int x = 0; x < 8; ++x) {
            uint32_t px = bx + x;
            uint32_t py = by + y;
            if (px < width && py < height) {
              uint8_t val =
                  ::nwii::runtime::g_mmu->read8(block_addr + (y * 8 + x) / 2);
              uint8_t i = (x % 2 == 0) ? (val >> 4) : (val & 0xF);
              i = (i << 4) | i;

              int out_idx = (py * width + px) * 4;
              pixels[out_idx + 0] = i;
              pixels[out_idx + 1] = i;
              pixels[out_idx + 2] = i;
              pixels[out_idx + 3] = i;
            }
          }
        }
        addr += 32;
      }
    }
  } else if (format == 0x1) { // GX_TF_I8 (8x4 blocks, 32 bytes)
    for (uint32_t by = 0; by < height; by += 4) {
      for (uint32_t bx = 0; bx < width; bx += 8) {
        uint32_t block_addr = addr;
        for (int y = 0; y < 4; ++y) {
          for (int x = 0; x < 8; ++x) {
            uint32_t px = bx + x;
            uint32_t py = by + y;
            if (px < width && py < height) {
              uint8_t i =
                  ::nwii::runtime::g_mmu->read8(block_addr + (y * 8 + x));

              int out_idx = (py * width + px) * 4;
              pixels[out_idx + 0] = i;
              pixels[out_idx + 1] = i;
              pixels[out_idx + 2] = i;
              pixels[out_idx + 3] = i;
            }
          }
        }
        addr += 32;
      }
    }
  } else if (format == 0x2) { // GX_TF_IA4 (8x4 blocks, 32 bytes)
    for (uint32_t by = 0; by < height; by += 4) {
      for (uint32_t bx = 0; bx < width; bx += 8) {
        uint32_t block_addr = addr;
        for (int y = 0; y < 4; ++y) {
          for (int x = 0; x < 8; ++x) {
            uint32_t px = bx + x;
            uint32_t py = by + y;
            if (px < width && py < height) {
              uint8_t val =
                  ::nwii::runtime::g_mmu->read8(block_addr + (y * 8 + x));
              uint8_t a = val >> 4;
              a = (a << 4) | a;
              uint8_t i = val & 0xF;
              i = (i << 4) | i;

              int out_idx = (py * width + px) * 4;
              pixels[out_idx + 0] = i;
              pixels[out_idx + 1] = i;
              pixels[out_idx + 2] = i;
              pixels[out_idx + 3] = a;
            }
          }
        }
        addr += 32;
      }
    }
  } else if (format == 0x3) { // GX_TF_IA8 (4x4 blocks, 32 bytes)
    for (uint32_t by = 0; by < height; by += 4) {
      for (uint32_t bx = 0; bx < width; bx += 4) {
        uint32_t block_addr = addr;
        for (int y = 0; y < 4; ++y) {
          for (int x = 0; x < 4; ++x) {
            uint32_t px = bx + x;
            uint32_t py = by + y;
            if (px < width && py < height) {
              uint16_t val =
                  ::nwii::runtime::g_mmu->read16(block_addr + (y * 4 + x) * 2);
              uint8_t a = val >> 8;
              uint8_t i = val & 0xFF;

              int out_idx = (py * width + px) * 4;
              pixels[out_idx + 0] = i;
              pixels[out_idx + 1] = i;
              pixels[out_idx + 2] = i;
              pixels[out_idx + 3] = a;
            }
          }
        }
        addr += 32;
      }
    }
  } else if (format == 0x4) { // GX_TF_RGB565 (4x4 blocks, 32 bytes)
    for (uint32_t by = 0; by < height; by += 4) {
      for (uint32_t bx = 0; bx < width; bx += 4) {
        uint32_t block_addr = addr;
        for (int y = 0; y < 4; ++y) {
          for (int x = 0; x < 4; ++x) {
            uint32_t px = bx + x;
            uint32_t py = by + y;
            if (px < width && py < height) {
              uint16_t val =
                  ::nwii::runtime::g_mmu->read16(block_addr + (y * 4 + x) * 2);
              uint8_t r = ((val >> 11) & 0x1F) * 255 / 31;
              uint8_t g = ((val >> 5) & 0x3F) * 255 / 63;
              uint8_t b = (val & 0x1F) * 255 / 31;

              int out_idx = (py * width + px) * 4;
              pixels[out_idx + 0] = r;
              pixels[out_idx + 1] = g;
              pixels[out_idx + 2] = b;
              pixels[out_idx + 3] = 255;
            }
          }
        }
        addr += 32;
      }
    }
  } else if (format == 0x5) { // GX_TF_RGB5A3 (4x4 blocks, 32 bytes)
    for (uint32_t by = 0; by < height; by += 4) {
      for (uint32_t bx = 0; bx < width; bx += 4) {
        uint32_t block_addr = addr;
        for (int y = 0; y < 4; ++y) {
          for (int x = 0; x < 4; ++x) {
            uint32_t px = bx + x;
            uint32_t py = by + y;
            if (px < width && py < height) {
              uint16_t val =
                  ::nwii::runtime::g_mmu->read16(block_addr + (y * 4 + x) * 2);
              uint8_t r, g, b, a;
              if (val & 0x8000) {
                r = ((val >> 10) & 0x1F) * 255 / 31;
                g = ((val >> 5) & 0x1F) * 255 / 31;
                b = (val & 0x1F) * 255 / 31;
                a = 255;
              } else {
                a = ((val >> 12) & 0x7) * 255 / 7;
                r = ((val >> 8) & 0xF) * 255 / 15;
                g = ((val >> 4) & 0xF) * 255 / 15;
                b = (val & 0xF) * 255 / 15;
              }

              int out_idx = (py * width + px) * 4;
              pixels[out_idx + 0] = r;
              pixels[out_idx + 1] = g;
              pixels[out_idx + 2] = b;
              pixels[out_idx + 3] = a;
            }
          }
        }
        addr += 32;
      }
    }
  } else if (format == 0x6) { // GX_TF_RGBA8 (4x4 blocks, 64 bytes)
    for (uint32_t by = 0; by < height; by += 4) {
      for (uint32_t bx = 0; bx < width; bx += 4) {
        uint32_t block_addr = addr;
        for (int y = 0; y < 4; ++y) {
          for (int x = 0; x < 4; ++x) {
            uint32_t px = bx + x;
            uint32_t py = by + y;
            if (px < width && py < height) {
              uint8_t a =
                  nwii::runtime::g_mmu->read8(block_addr + (y * 4 + x) * 2 + 0);
              uint8_t r =
                  nwii::runtime::g_mmu->read8(block_addr + (y * 4 + x) * 2 + 1);
              uint8_t g = nwii::runtime::g_mmu->read8(block_addr + 32 +
                                                      (y * 4 + x) * 2 + 0);
              uint8_t b = nwii::runtime::g_mmu->read8(block_addr + 32 +
                                                      (y * 4 + x) * 2 + 1);

              int out_idx = (py * width + px) * 4;
              pixels[out_idx + 0] = r;
              pixels[out_idx + 1] = g;
              pixels[out_idx + 2] = b;
              pixels[out_idx + 3] = a;
            }
          }
        }
        addr += 64;
      }
    }
  } else if (format == 0xE) { // GX_TF_CMPR
    // CMPR (DXT1) is 8x8 blocks, which are composed of four 4x4 sub-blocks.
    // Each 4x4 sub-block is 8 bytes.
    // Wait, for simplicity, we can do full CMPR decode!
    for (uint32_t by = 0; by < height; by += 8) {
      for (uint32_t bx = 0; bx < width; bx += 8) {
        for (int sub = 0; sub < 4; ++sub) {
          int sub_x = (sub & 1) * 4;
          int sub_y = (sub >> 1) * 4;

          uint16_t c0 = nwii::runtime::g_mmu->read16(addr + 0);
          uint16_t c1 = nwii::runtime::g_mmu->read16(addr + 2);
          uint32_t indices = nwii::runtime::g_mmu->read32(addr + 4);
          addr += 8;

          // Decode RGB565 to RGB8
          uint8_t pal[4][4]; // [4 colors][R,G,B,A]
          pal[0][0] = ((c0 >> 11) & 0x1F) * 255 / 31;
          pal[0][1] = ((c0 >> 5) & 0x3F) * 255 / 63;
          pal[0][2] = (c0 & 0x1F) * 255 / 31;
          pal[0][3] = 255;

          pal[1][0] = ((c1 >> 11) & 0x1F) * 255 / 31;
          pal[1][1] = ((c1 >> 5) & 0x3F) * 255 / 63;
          pal[1][2] = (c1 & 0x1F) * 255 / 31;
          pal[1][3] = 255;

          if (c0 > c1) {
            for (int i = 0; i < 3; ++i)
              pal[2][i] = (2 * pal[0][i] + pal[1][i]) / 3;
            for (int i = 0; i < 3; ++i)
              pal[3][i] = (pal[0][i] + 2 * pal[1][i]) / 3;
            pal[2][3] = 255;
            pal[3][3] = 255;
          } else {
            for (int i = 0; i < 3; ++i)
              pal[2][i] = (pal[0][i] + pal[1][i]) / 2;
            pal[2][3] = 255;
            pal[3][0] = pal[3][1] = pal[3][2] = pal[3][3] = 0; // Transparent
          }

          for (int y = 0; y < 4; ++y) {
            for (int x = 0; x < 4; ++x) {
              uint32_t px = bx + sub_x + x;
              uint32_t py = by + sub_y + y;
              if (px < width && py < height) {
                // Indices are 2 bits each, stored big-endian.
                // The highest 2 bits of the 32-bit word correspond to the first
                // pixel.
                int bit_offset = 30 - ((y * 4 + x) * 2);
                int idx = (indices >> bit_offset) & 3;

                int out_idx = (py * width + px) * 4;
                pixels[out_idx + 0] = pal[idx][0];
                pixels[out_idx + 1] = pal[idx][1];
                pixels[out_idx + 2] = pal[idx][2];
                pixels[out_idx + 3] = pal[idx][3];
              }
            }
          }
        }
      }
    }
  } else {
    // Fallback or unknown format: red debug pixels
    for (size_t i = 0; i < pixels.size(); i += 4) {
      pixels[i + 0] = 255;
      pixels[i + 1] = 0;
      pixels[i + 2] = 0;
      pixels[i + 3] = 128;
    }
  }

  Image img = {.data = pixels.data(),
               .width = (int)width,
               .height = (int)height,
               .mipmaps = 1,
               .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};

  Texture2D tex = LoadTextureFromImage(img);
  SetTextureFilter(tex, TEXTURE_FILTER_POINT);

  // Unload old texture if it exists
  if (s_texture_cache.find(base_addr) != s_texture_cache.end()) {
    UnloadTexture(s_texture_cache[base_addr]);
  }

  s_texture_cache[base_addr] = tex;
}

void Renderer::Render(const std::vector<GXCommand> &commands) {
  rlDisableDepthTest();
  rlDisableBackfaceCulling();

  // FORCE PROJECTION MATRIX
  Matrix proj = {0};
  proj.m0 = 2.0f / 640.0f;
  proj.m5 = -2.0f / 480.0f;
  proj.m10 = -1.0f;
  proj.m12 = -1.0f;
  proj.m13 = 1.0f;
  proj.m15 = 1.0f;

  rlMatrixMode(RL_PROJECTION);
  rlLoadIdentity();
  rlMultMatrixf((float *)&proj);
  rlMatrixMode(RL_MODELVIEW);
  rlLoadIdentity();

  for (const auto &cmd : commands) {
    if (cmd.type == GXCommandType::XFRegister) {
      int addr = cmd.reg;

      for (size_t i = 0; i < cmd.payload.size(); ++i) {
        int current_addr = addr + i;
        if (current_addr >= 0x0000 && current_addr <= 0x00FF) {
          g_state.posMatrices[current_addr] = cmd.payload[i];
        } else if (current_addr >= 0x1020 && current_addr <= 0x1026) {
          g_state.projection[current_addr - 0x1020] = cmd.payload[i];
        }
      }

      // Only update Raylib Projection Matrix if we actually touched it in this
      // command
      if (addr <= 0x1026 && (addr + cmd.payload.size()) > 0x1020) {
        printf("PROJ: %f, %f, %f, %f, %f, %f, type=%f\n", g_state.projection[0],
               g_state.projection[1], g_state.projection[2],
               g_state.projection[3], g_state.projection[4],
               g_state.projection[5], g_state.projection[6]);

        // The GameCube Projection matrix isn't being sent for the splash
        // screen? For now, let's just force a standard 2D ortho matrix so we
        // can see the 2D UI
        Matrix proj = {0};
        proj.m0 = g_state.projection[0];
        proj.m5 = g_state.projection[2];
        proj.m10 = g_state.projection[4];
        proj.m12 = g_state.projection[1];
        proj.m13 = g_state.projection[3];
        proj.m14 = g_state.projection[5];
        proj.m15 = 1.0f;

        rlMatrixMode(RL_PROJECTION);
        rlLoadIdentity();
        rlMultMatrixf((float *)&proj);
        rlMatrixMode(RL_MODELVIEW);
        rlLoadIdentity();
      }
    } else if (cmd.type == GXCommandType::DrawPrimitive) {
      // Check if TEX0 is used
      bool use_tex0 = false;
      for (const auto &vtx : cmd.vertices) {
        if (vtx.has_tex[0]) {
          use_tex0 = true;
          break;
        }
      }

      if (use_tex0 && g_state.texStages[0].base_addr != 0) {
        LoadTextureIfNecessary(
            g_state.texStages[0].base_addr, g_state.texStages[0].width,
            g_state.texStages[0].height, g_state.texStages[0].format);

        auto it = s_texture_cache.find(g_state.texStages[0].base_addr);
        if (it != s_texture_cache.end()) {
          rlSetTexture(it->second.id);
        } else {
          rlSetTexture(0);
        }
      } else {
        rlSetTexture(0);
      }

      rlDisableDepthTest();
      rlDisableBackfaceCulling();

      rlBegin(cmd.gl_mode);

      // Default color if none specified
      rlColor4ub(255, 255, 255, 255);

      for (const auto &vtx : cmd.vertices) {
        if (vtx.has_color) {
          rlColor4f(vtx.color[0], vtx.color[1], vtx.color[2], vtx.color[3]);
        } else {
          rlColor4ub(255, 255, 255, 255);
        }

        // Texture coords
        for (int t = 0; t < 8; ++t) {
          if (vtx.has_tex[t]) {
            if (t == 0) {
              static int count = 0;
              if (count++ < 20)
                printf("TEX0: %f, %f\n", vtx.tex[t][0], vtx.tex[t][1]);
            }
            if (t == 0)
              rlTexCoord2f(vtx.tex[t][0], vtx.tex[t][1]);
            // rlgl only natively supports one texcoord in its simple API,
            // but this matches the original implementation.
          }
        }

        if (vtx.has_norm) {
          // We should ideally transform normal by inverse-transpose of upper
          // 3x3 PosMtx
          rlNormal3f(vtx.norm[0], vtx.norm[1], vtx.norm[2]);
        }

        if (vtx.has_pos) {
          // Software ModelView Transformation
          // Matrix index points to a row in posMatrices. Each row is 4 floats.
          // Matrix index is typically a multiple of 3.
          int mtx_base =
              vtx.posMtxIdx * 4; // GameCube uses idx*3 (i.e. 0, 3, 6, 9)
                                 // representing rows. 1 row = 4 floats.
          if (mtx_base + 11 < 256) {
            float x = vtx.pos[0];
            float y = vtx.pos[1];
            float z = vtx.pos[2];

            float tx = x * g_state.posMatrices[mtx_base + 0] +
                       y * g_state.posMatrices[mtx_base + 1] +
                       z * g_state.posMatrices[mtx_base + 2] +
                       g_state.posMatrices[mtx_base + 3];
            float ty = x * g_state.posMatrices[mtx_base + 4] +
                       y * g_state.posMatrices[mtx_base + 5] +
                       z * g_state.posMatrices[mtx_base + 6] +
                       g_state.posMatrices[mtx_base + 7];
            float tz = x * g_state.posMatrices[mtx_base + 8] +
                       y * g_state.posMatrices[mtx_base + 9] +
                       z * g_state.posMatrices[mtx_base + 10] +
                       g_state.posMatrices[mtx_base + 11];

            // Force Z to 0.0 to prevent OpenGL near/far clipping for 2D UI
            rlVertex3f(tx, ty, 0.0f);

            static int vtx_print = 0;
            if (vtx_print++ < 20) {
              printf("VTX_DRAW: %f, %f, %f (raw: %f, %f, %f) mtx_base=%d\n", tx,
                     ty, tz, x, y, z, mtx_base);
            }
          } else {
            rlVertex3f(vtx.pos[0], vtx.pos[1], 0.0f);
          }
        }
      }

      rlEnd();

      // Flush rlgl batch - default holds only 8192 verts; large meshes need
      // explicit flush.
      rlDrawRenderBatchActive();
    }
  }
}

} // namespace nwii::runtime::gx
