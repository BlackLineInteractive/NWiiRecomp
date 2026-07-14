#include "runtime/gx/renderer.h"
#include "runtime/cpu_context.h"
#include "runtime/gx_state.h"
#include "runtime/texture_cache.h"
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <raylib.h>
#include <rlgl.h>
#include <vector>
#include <unordered_map>
#include "runtime/gx/tev_shader_gen.h"

// HLE GX renderer on top of raylib/rlgl.
//
// Pipeline placement: texture decoding stays on the CPU (decoded RGBA8 is
// uploaded once and cached — this is how every GC emulator works), but the
// per-vertex position transform below is ALSO done on the CPU for now.
// That is the wrong side of the fence long-term: the position matrix
// belongs in the modelview matrix so the GPU applies it. It is kept
// CPU-side deliberately until per-vertex matrix indices (skinning) get a
// proper batching story, since rlgl has a single modelview per batch.

namespace nwii::runtime {
extern MMU *g_mmu;
extern CPUContext *g_ctx_ptr;
}

namespace nwii::runtime::gx {

static bool gx_trace() {
  static bool t = std::getenv("NWII_GXTRACE") != nullptr;
  return t;
}

// Builds the GL projection from the raw GX projection (XF 0x1020-0x1026).
// Layout per Dolphin VertexShaderManager: perspective uses [A C][B D][E F]
// with w = -z; orthographic uses [A tx][B ty][E F] with w = 1. Console
// clip-space z is [-w, 0]; GL expects [-w, w], so z' = 2*z + w.
static void ApplyProjection() {
  const float *p = g_state.projection;
  Matrix m = {0};
  if (!g_state.projSet) {
    // No projection loaded yet: identity (nothing meaningful can be on
    // screen before the game's first GXSetProjection anyway).
    m.m0 = m.m5 = m.m10 = m.m15 = 1.0f;
  } else if (p[6] == 0.0f) { // GX_PERSPECTIVE
    m.m0 = p[0];
    m.m8 = p[1];
    m.m5 = p[2];
    m.m9 = p[3];
    m.m10 = 2.0f * p[4] - 1.0f;
    m.m14 = 2.0f * p[5];
    m.m11 = -1.0f;
  } else { // GX_ORTHOGRAPHIC
    m.m0 = p[0];
    m.m12 = p[1];
    m.m5 = p[2];
    m.m13 = p[3];
    m.m10 = 2.0f * p[4];
    m.m14 = 2.0f * p[5] + 1.0f;
    m.m15 = 1.0f;
  }

  rlMatrixMode(RL_PROJECTION);
  rlLoadIdentity();
  rlMultMatrixf((float *)&m);
  rlMatrixMode(RL_MODELVIEW);
  rlLoadIdentity();
}

static void ApplyZMode() {
  if (g_state.zMode.enable)
    rlEnableDepthTest();
  else
    rlDisableDepthTest();
}

// Emits one vertex through rlgl: color, texcoord, then the CPU-side
// position transform by the vertex's position matrix (XF matrix memory,
// row index * 4 floats per row, 3 rows of [r0 r1 r2 t]).
static void EmitVertex(const VertexData &vtx) {
  if (vtx.has_color)
    rlColor4f(vtx.color[0], vtx.color[1], vtx.color[2], vtx.color[3]);
  else
    rlColor4ub(255, 255, 255, 255);

  // rlgl's immediate API carries a single texcoord set; TEV multi-texturing
  // would need a real shader pipeline.
  if (vtx.has_tex[0])
    rlTexCoord2f(vtx.tex[0][0], vtx.tex[0][1]);

  if (vtx.has_norm)
    rlNormal3f(vtx.norm[0], vtx.norm[1], vtx.norm[2]);

  float x = vtx.pos[0], y = vtx.pos[1], z = vtx.pos[2];
  int mtx_base = vtx.posMtxIdx * 4;
  if (vtx.has_pos && mtx_base + 12 <= 256) {
    const float *mm = g_state.posMatrices;
    float tx = x * mm[mtx_base + 0] + y * mm[mtx_base + 1] +
               z * mm[mtx_base + 2] + mm[mtx_base + 3];
    float ty = x * mm[mtx_base + 4] + y * mm[mtx_base + 5] +
               z * mm[mtx_base + 6] + mm[mtx_base + 7];
    float tz = x * mm[mtx_base + 8] + y * mm[mtx_base + 9] +
               z * mm[mtx_base + 10] + mm[mtx_base + 11];
    rlVertex3f(tx, ty, tz);
  } else {
    rlVertex3f(x, y, z);
  }
}

static std::unordered_map<uint64_t, Shader> s_shader_cache;

// Draws one GX primitive, unrolling strip/fan topologies (rlgl only
// batches triangles, quads and lines).
static void DrawPrimitive(const GXCommand &cmd) {
  const auto &v = cmd.vertices;
  const size_t n = v.size();
  if (n == 0)
    return;

  bool use_shader = (std::getenv("NWII_NOSHADER") == nullptr);
  Shader active_shader = {0};

  if (use_shader) {
    uint64_t hash = g_state.GetShaderHash(cmd.prim_type);
    auto it = s_shader_cache.find(hash);
    if (it != s_shader_cache.end()) {
      active_shader = it->second;
    } else {
      auto src = GenerateTEVShader(g_state, cmd.prim_type);
      active_shader = LoadShaderFromMemory(src.vertex_source.c_str(), src.fragment_source.c_str());
      s_shader_cache[hash] = active_shader;
      if (gx_trace()) {
        printf("[GXTRACE] Compiled new shader! Hash: %llx\n", (unsigned long long)hash);
      }
    }
    
    // Bind shader
    BeginShaderMode(active_shader);
    
    // Bind uniforms: tex maps
    for (int i = 0; i < 8; ++i) {
        if (g_state.texStages[i].base_addr != 0) {
            int loc = GetShaderLocation(active_shader, TextFormat("uTex%d", i));
            if (loc >= 0) {
                // For now, rlSetTexture handles the active texture slot 0 correctly for the first texture.
            }
        }
    }
    
    // Bind uniforms: TEV Colors and Konst Colors
    float kcolor[16] = {0};
    float color[16] = {0};
    
    auto decode11 = [](uint32_t val, int shift) -> float {
        int v = (val >> shift) & 0x7FF;
        if (v & 0x400) v |= ~0x7FF; // sign extend 11-bit
        return (float)v / 255.0f;
    };
    
    for (int i = 0; i < 4; i++) {
        uint32_t ra = g_state.bp[0xE0 + i*2];
        uint32_t bg = g_state.bp[0xE1 + i*2];
        
        if ((ra >> 23) & 1) { // Constant
            kcolor[i*4 + 0] = decode11(ra, 0); // r
            kcolor[i*4 + 3] = decode11(ra, 12); // a
        } else {
            color[i*4 + 0] = decode11(ra, 0);
            color[i*4 + 3] = decode11(ra, 12);
        }
        
        if ((bg >> 23) & 1) { // Constant
            kcolor[i*4 + 2] = decode11(bg, 0); // b
            kcolor[i*4 + 1] = decode11(bg, 12); // g
        } else {
            color[i*4 + 2] = decode11(bg, 0);
            color[i*4 + 1] = decode11(bg, 12);
        }
    }
    
    int kloc = GetShaderLocation(active_shader, "uTevKColor");
    if (kloc >= 0) SetShaderValueV(active_shader, kloc, kcolor, SHADER_UNIFORM_VEC4, 4);
    
    int cloc = GetShaderLocation(active_shader, "uTevColor");
    if (cloc >= 0) SetShaderValueV(active_shader, cloc, color, SHADER_UNIFORM_VEC4, 4);
  }

  switch (cmd.prim_type) {
  case 0x80: // GX_QUADS
    rlBegin(RL_QUADS);
    for (const auto &vtx : v)
      EmitVertex(vtx);
    rlEnd();
    break;
  case 0x90: // GX_TRIANGLES
    rlBegin(RL_TRIANGLES);
    for (const auto &vtx : v)
      EmitVertex(vtx);
    rlEnd();
    break;
  case 0x98: // GX_TRIANGLESTRIP
    rlBegin(RL_TRIANGLES);
    for (size_t i = 2; i < n; i++) {
      // Alternate winding so every triangle faces the same way.
      if (i & 1) {
        EmitVertex(v[i - 1]); EmitVertex(v[i - 2]); EmitVertex(v[i]);
      } else {
        EmitVertex(v[i - 2]); EmitVertex(v[i - 1]); EmitVertex(v[i]);
      }
    }
    rlEnd();
    break;
  case 0xA0: // GX_TRIANGLEFAN
    rlBegin(RL_TRIANGLES);
    for (size_t i = 2; i < n; i++) {
      EmitVertex(v[0]); EmitVertex(v[i - 1]); EmitVertex(v[i]);
    }
    rlEnd();
    break;
  case 0xA8: // GX_LINES
    rlBegin(RL_LINES);
    for (const auto &vtx : v)
      EmitVertex(vtx);
    rlEnd();
    break;
  case 0xB0: // GX_LINESTRIP
    rlBegin(RL_LINES);
    for (size_t i = 1; i < n; i++) {
      EmitVertex(v[i - 1]); EmitVertex(v[i]);
    }
    rlEnd();
    break;
  default: // GX_POINTS and anything unexpected: no rlgl equivalent.
    if (gx_trace()) {
      static int warn = 0;
      if (warn++ < 8)
        printf("[GXTRACE] unhandled primitive 0x%02X (%zu verts)\n",
               cmd.prim_type, n);
    }
    break;
  }

  if (use_shader && active_shader.id != 0) {
    EndShaderMode();
  }
}

void Renderer::Render(const std::vector<GXCommand> &commands) {
  rlDisableBackfaceCulling();
  ApplyZMode();
  ApplyProjection();

  for (const auto &cmd : commands) {
    if (cmd.type == GXCommandType::XFRegister) {
      int addr = cmd.reg;

      if (gx_trace() && addr < 0x100) {
        static int mn = 0;
        if (mn++ < 16) {
          printf("[GXTRACE] XF mtx write addr=0x%03X n=%zu [", addr,
                 cmd.payload.size());
          for (size_t i = 0; i < cmd.payload.size() && i < 12; i++)
            printf("%.3f ", cmd.payload[i]);
          printf("]\n");
        }
      }
      bool touched_proj = false;
      for (size_t i = 0; i < cmd.payload.size(); ++i) {
        int current_addr = addr + (int)i;
        if (current_addr >= 0x1000 && current_addr < 0x1000 + 256) {
          std::memcpy(&g_state.xf[current_addr - 0x1000], &cmd.payload[i], 4);
        }
        if (current_addr >= 0x0000 && current_addr <= 0x00FF) {
          g_state.posMatrices[current_addr] = cmd.payload[i];
        } else if (current_addr >= 0x1020 && current_addr <= 0x1026) {
          g_state.projection[current_addr - 0x1020] = cmd.payload[i];
          touched_proj = true;
        }
      }

      if (touched_proj) {
        g_state.projSet = true;
        // rlgl applies the projection uniform when a batch is flushed, so
        // geometry already recorded must be drawn under the OLD matrix
        // before it changes.
        rlDrawRenderBatchActive();
        ApplyProjection();
        if (gx_trace()) {
          static int pn = 0;
          if (pn++ < 8)
            printf("[GXTRACE] proj: %f %f %f %f %f %f type=%f\n",
                   g_state.projection[0], g_state.projection[1],
                   g_state.projection[2], g_state.projection[3],
                   g_state.projection[4], g_state.projection[5],
                   g_state.projection[6]);
        }
      }
    } else if (cmd.type == GXCommandType::BPRegister) {
      if (cmd.reg == 0x40) { // zMode already decoded into g_state by the parser
        rlDrawRenderBatchActive();
        ApplyZMode();
      }
    } else if (cmd.type == GXCommandType::DrawPrimitive) {
      // Texture selection: the first TEV stage samples texmap 0 in the
      // simple pipelines these 2D scenes use. (Proper texMap routing via
      // TEV stage state comes with a shader-based TEV.)
      bool use_tex0 = false;
      for (const auto &vtx : cmd.vertices) {
        if (vtx.has_tex[0]) {
          use_tex0 = true;
          break;
        }
      }

      const auto &stage0 = g_state.texStages[0];
      if (gx_trace() && use_tex0) {
        static int qn = 0;
        if (qn++ < 6) {
          printf("[GXTRACE] texquad prim=0x%02X proj=[%.4f %.3f %.4f %.3f %.3f %.3f t=%.0f]\n",
                 cmd.prim_type, g_state.projection[0], g_state.projection[1],
                 g_state.projection[2], g_state.projection[3],
                 g_state.projection[4], g_state.projection[5],
                 g_state.projection[6]);
          for (size_t vi = 0; vi < cmd.vertices.size() && vi < 4; vi++) {
            const auto &vv = cmd.vertices[vi];
            const float *m2 = &g_state.posMatrices[vv.posMtxIdx * 4];
            float vx = vv.pos[0]*m2[0] + vv.pos[1]*m2[1] + vv.pos[2]*m2[2] + m2[3];
            float vy = vv.pos[0]*m2[4] + vv.pos[1]*m2[5] + vv.pos[2]*m2[6] + m2[7];
            float vz = vv.pos[0]*m2[8] + vv.pos[1]*m2[9] + vv.pos[2]*m2[10] + m2[11];
            printf("  v%zu raw=(%.1f,%.1f,%.1f) view=(%.1f,%.1f,%.2f) uv=(%.2f,%.2f)\n",
                   vi, vv.pos[0], vv.pos[1], vv.pos[2], vx, vy, vz,
                   vv.tex[0][0], vv.tex[0][1]);
          }
        }
      }
      if (gx_trace()) {
        static int dn = 0;
        if (dn++ < 40) {
          const auto &v0 = cmd.vertices.empty() ? VertexData{} : cmd.vertices[0];
          const float *mm = &g_state.posMatrices[v0.posMtxIdx * 4];
          float tx = v0.pos[0]*mm[0] + v0.pos[1]*mm[1] + v0.pos[2]*mm[2] + mm[3];
          float ty = v0.pos[0]*mm[4] + v0.pos[1]*mm[5] + v0.pos[2]*mm[6] + mm[7];
          float tz = v0.pos[0]*mm[8] + v0.pos[1]*mm[9] + v0.pos[2]*mm[10] + mm[11];
          printf("[GXTRACE] draw prim=0x%02X n=%zu tex0=%d stage0{addr=0x%X %ux%u fmt=%d} "
                 "v0=(%.1f,%.1f,%.1f)->(%.1f,%.1f,%.2f) mtx=%d hasC=%d col=(%.2f,%.2f,%.2f,%.2f) uv=(%.2f,%.2f) "
                 "row0=[%.3f %.3f %.3f %.1f] row2=[%.3f %.3f %.3f %.2f]\n",
                 cmd.prim_type, cmd.vertices.size(), (int)use_tex0,
                 stage0.base_addr, stage0.width, stage0.height, stage0.format,
                 v0.pos[0], v0.pos[1], v0.pos[2], tx, ty, tz, v0.posMtxIdx,
                 (int)v0.has_color,
                 v0.color[0], v0.color[1], v0.color[2], v0.color[3],
                 v0.tex[0][0], v0.tex[0][1],
                 mm[0], mm[1], mm[2], mm[3], mm[8], mm[9], mm[10], mm[11]);
        }
      }
      bool use_shader = (std::getenv("NWII_NOSHADER") == nullptr);
      
      if (use_shader) {
        rlDrawRenderBatchActive(); // flush before state changes
        for (int i = 0; i < 8; ++i) {
          const auto &stage = g_state.texStages[i];
          if (stage.base_addr != 0 && stage.width > 0 && stage.height > 0 && nwii::runtime::g_ctx_ptr) {
            Texture2D tex = nwii::runtime::hle::TextureCache::get().get_texture(
                *nwii::runtime::g_ctx_ptr, stage);
            // We'll pass the texture IDs into DrawPrimitive so it can bind them via SetShaderValueTexture
            // Actually, we can just bind tex0 to rlSetTexture for fallback compatibility,
            // and the shader will use it.
            if (i == 0) rlSetTexture(tex.id);
          }
        }
      } else {
        if (use_tex0 && stage0.base_addr != 0 && stage0.width > 0 &&
            stage0.height > 0 && nwii::runtime::g_ctx_ptr) {
          rlDrawRenderBatchActive();
          Texture2D tex = nwii::runtime::hle::TextureCache::get().get_texture(
              *nwii::runtime::g_ctx_ptr, stage0);
          rlSetTexture(tex.id);
        } else {
          rlSetTexture(0);
        }
      }

      DrawPrimitive(cmd);
    }
  }

  rlSetTexture(0);
  rlDrawRenderBatchActive();
}

} // namespace nwii::runtime::gx
