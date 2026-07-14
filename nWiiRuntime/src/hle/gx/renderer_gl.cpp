#include "runtime/gx/renderer.h"
#include "runtime/cpu_context.h"
#include "runtime/gx_state.h"
#include "runtime/texture_cache_gl.h"
#include <glad/glad.h>
#include <vector>
#include <unordered_map>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <string>
#include "runtime/gx/tev_shader_gen.h"

namespace nwii::runtime {
extern MMU *g_mmu;
extern CPUContext *g_ctx_ptr;
}

void GX_GetClearColor(unsigned char rgba[4]);

namespace nwii::runtime::gx {

static bool gx_trace() {
  static bool t = std::getenv("NWII_GXTRACE") != nullptr;
  return t;
}

struct GLShader {
    GLuint id;
    GLint uTex[8];
    GLint uTexMtx[8];
    GLint uTevKColor;
    GLint uTevColor;
    GLint uProjMtx;
};
static std::unordered_map<uint64_t, GLShader> s_shader_cache;

struct BatchVertex {
    float x, y, z;
    float r, g, b, a;
    float u, v;
};

static std::vector<BatchVertex> s_batch;
static GLuint s_vao = 0, s_vbo = 0;
static GLShader s_active_shader = {0};

static GLuint CompileShader(const char* vs, const char* fs) {
    GLuint v = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(v, 1, &vs, nullptr);
    glCompileShader(v);

    GLuint f = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(f, 1, &fs, nullptr);
    glCompileShader(f);

    GLuint p = glCreateProgram();
    glAttachShader(p, v);
    glAttachShader(p, f);
    glLinkProgram(p);

    glDeleteShader(v);
    glDeleteShader(f);
    return p;
}

static void FlushBatch() {
    if (s_batch.empty()) return;
    
    if (!s_vao) {
        glGenVertexArrays(1, &s_vao);
        glGenBuffers(1, &s_vbo);
        glBindVertexArray(s_vao);
        glBindBuffer(GL_ARRAY_BUFFER, s_vbo);
        glEnableVertexAttribArray(0);
        glEnableVertexAttribArray(1);
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(BatchVertex), (void*)offsetof(BatchVertex, x));
        glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(BatchVertex), (void*)offsetof(BatchVertex, r));
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(BatchVertex), (void*)offsetof(BatchVertex, u));
    }
    
    glBindVertexArray(s_vao);
    glBindBuffer(GL_ARRAY_BUFFER, s_vbo);
    glBufferData(GL_ARRAY_BUFFER, s_batch.size() * sizeof(BatchVertex), s_batch.data(), GL_STREAM_DRAW);
    
    glDrawArrays(GL_TRIANGLES, 0, s_batch.size());
    s_batch.clear();
}

static void ApplyProjection(GLint uProj) {
    if (uProj < 0) return;
    const float *sp = g_state.projection;
    float p[16] = {0};
    
    if (!g_state.projSet) {
        p[0]=1; p[5]=1; p[10]=1; p[15]=1;
    } else if (g_state.projType == 0) { // GX_PERSPECTIVE
        p[0] = sp[0]; p[4] = 0; p[8] = sp[1]; p[12] = 0;
        p[1] = 0; p[5] = sp[2]; p[9] = sp[3]; p[13] = 0;
        p[2] = 0; p[6] = 0; p[10] = 2.0f * sp[4] - 1.0f; p[14] = 2.0f * sp[5];
        p[3] = 0; p[7] = 0; p[11] = -1.0f; p[15] = 0;
    } else { // GX_ORTHOGRAPHIC
        p[0] = sp[0]; p[4] = 0; p[8] = 0; p[12] = sp[1];
        p[1] = 0; p[5] = sp[2]; p[9] = 0; p[13] = sp[3];
        p[2] = 0; p[6] = 0; p[10] = 2.0f * sp[4]; p[14] = 2.0f * sp[5] + 1.0f;
        p[3] = 0; p[7] = 0; p[11] = 0; p[15] = 1.0f;
    }
    glUniformMatrix4fv(uProj, 1, GL_FALSE, p);
}

static void ApplyZMode() {
    if (g_state.zMode.enable) {
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL); // Dolphin default approx
    } else {
        glDisable(GL_DEPTH_TEST);
    }
}

static void EmitVertex(const VertexData &vtx) {
    BatchVertex bv = {0};
    if (vtx.has_color) {
        bv.r = vtx.color[0]; bv.g = vtx.color[1]; bv.b = vtx.color[2]; bv.a = vtx.color[3];
    } else {
        uint32_t mat0 = g_state.xf[12];
        bv.r = ((mat0 >> 24) & 0xFF) / 255.f;
        bv.g = ((mat0 >> 16) & 0xFF) / 255.f;
        bv.b = ((mat0 >> 8) & 0xFF) / 255.f;
        bv.a = (mat0 & 0xFF) / 255.f;
    }
    if (vtx.has_tex[0]) {
        bv.u = vtx.tex[0][0]; bv.v = vtx.tex[0][1];
    }
    
    float x = vtx.pos[0], y = vtx.pos[1], z = vtx.pos[2];
    int mtx_base = vtx.posMtxIdx * 4;
    if (vtx.has_pos && mtx_base + 12 <= 256) {
        const float *mm = g_state.posMatrices;
        bv.x = x * mm[mtx_base + 0] + y * mm[mtx_base + 1] + z * mm[mtx_base + 2] + mm[mtx_base + 3];
        bv.y = x * mm[mtx_base + 4] + y * mm[mtx_base + 5] + z * mm[mtx_base + 6] + mm[mtx_base + 7];
        bv.z = x * mm[mtx_base + 8] + y * mm[mtx_base + 9] + z * mm[mtx_base + 10] + mm[mtx_base + 11];
    } else {
        bv.x = x; bv.y = y; bv.z = z;
    }
    s_batch.push_back(bv);
}

static void DrawPrimitive(const GXCommand &cmd) {
    const auto &v = cmd.vertices;
    const size_t n = v.size();
    if (n == 0) return;

    FlushBatch(); // Start new batch for new state

    bool use_shader = (std::getenv("NWII_NOSHADER") == nullptr);
    if (!use_shader) return;

    uint64_t hash = g_state.GetShaderHash(cmd.prim_type);
    auto it = s_shader_cache.find(hash);
    if (it != s_shader_cache.end()) {
        s_active_shader = it->second;
    } else {
        auto src = GenerateTEVShader(g_state, cmd.prim_type);
        GLShader sh;
        sh.id = CompileShader(src.vertex_source.c_str(), src.fragment_source.c_str());
        for (int i=0; i<8; i++) {
            sh.uTex[i] = glGetUniformLocation(sh.id, ("uTex" + std::to_string(i)).c_str());
            sh.uTexMtx[i] = glGetUniformLocation(sh.id, ("uTexMtx[" + std::to_string(i) + "]").c_str());
        }
        sh.uTevKColor = glGetUniformLocation(sh.id, "uTevKColor");
        sh.uTevColor = glGetUniformLocation(sh.id, "uTevColor");
        sh.uProjMtx = glGetUniformLocation(sh.id, "uProjMtx");
        
        s_shader_cache[hash] = sh;
        s_active_shader = sh;
        if (gx_trace()) printf("[GXTRACE] Compiled new shader! Hash: %llx\n", (unsigned long long)hash);
    }
    
    glUseProgram(s_active_shader.id);
    ApplyProjection(s_active_shader.uProjMtx);
    ApplyZMode();

    for (int i = 0; i < 8; ++i) {
        if (g_state.texStages[i].base_addr != 0 && s_active_shader.uTex[i] >= 0) {
            glUniform1i(s_active_shader.uTex[i], i);
        }
    }
    
    float kcolor[16] = {0};
    float color[16] = {0};
    
    for (int i = 0; i < 8; i++) {
        if (s_active_shader.uTexMtx[i] >= 0) {
            uint32_t texgenInfo = g_state.xf[0x1040 + i];
            uint32_t texgenType = (texgenInfo >> 4) & 7;
            uint32_t sourceRow = (texgenInfo >> 7) & 0x1F;
            
            float mat[16] = {0};
            if (texgenType == 0 && sourceRow != 30) {
                int base = sourceRow * 4;
                mat[0] = g_state.posMatrices[base + 0];
                mat[4] = g_state.posMatrices[base + 1];
                mat[8] = g_state.posMatrices[base + 2];
                mat[12] = g_state.posMatrices[base + 3];
                mat[1] = g_state.posMatrices[base + 4];
                mat[5] = g_state.posMatrices[base + 5];
                mat[9] = g_state.posMatrices[base + 6];
                mat[13] = g_state.posMatrices[base + 7];
                mat[2] = g_state.posMatrices[base + 8];
                mat[6] = g_state.posMatrices[base + 9];
                mat[10] = g_state.posMatrices[base + 10];
                mat[14] = g_state.posMatrices[base + 11];
                mat[15] = 1.0f;
            } else {
                mat[0]=1; mat[5]=1; mat[10]=1; mat[15]=1;
            }
            glUniformMatrix4fv(s_active_shader.uTexMtx[i], 1, GL_FALSE, mat);
        }
    }
    
    auto decode11 = [](uint32_t val, int shift) -> float {
        int v = (val >> shift) & 0x7FF;
        if (v & 0x400) v |= ~0x7FF; // sign extend 11-bit
        return (float)v / 255.0f;
    };
    
    for (int i = 0; i < 4; i++) {
        uint32_t ra = g_state.bp[0xE0 + i*2];
        uint32_t bg = g_state.bp[0xE1 + i*2];
        if (((ra >> 23) & 1) == 0) {
            kcolor[i*4 + 0] = decode11(ra, 0);
            kcolor[i*4 + 3] = decode11(ra, 12);
        } else {
            color[i*4 + 0] = decode11(ra, 0);
            color[i*4 + 3] = decode11(ra, 12);
        }
        if (((bg >> 23) & 1) == 0) {
            kcolor[i*4 + 2] = decode11(bg, 0);
            kcolor[i*4 + 1] = decode11(bg, 12);
        } else {
            color[i*4 + 2] = decode11(bg, 0);
            color[i*4 + 1] = decode11(bg, 12);
        }
    }
    
    if (s_active_shader.uTevKColor >= 0) glUniform4fv(s_active_shader.uTevKColor, 4, kcolor);
    if (s_active_shader.uTevColor >= 0) glUniform4fv(s_active_shader.uTevColor, 4, color);

    switch (cmd.prim_type) {
        case 0x80: // GX_QUADS
            for (size_t i = 0; i + 3 < n; i += 4) {
                EmitVertex(v[i]); EmitVertex(v[i+1]); EmitVertex(v[i+2]);
                EmitVertex(v[i+2]); EmitVertex(v[i+3]); EmitVertex(v[i]);
            }
            break;
        case 0x90: // GX_TRIANGLES
            for (const auto &vtx : v) EmitVertex(vtx);
            break;
        case 0x98: // GX_TRIANGLESTRIP
            for (size_t i = 2; i < n; i++) {
                if (i & 1) { EmitVertex(v[i - 1]); EmitVertex(v[i - 2]); EmitVertex(v[i]); }
                else { EmitVertex(v[i - 2]); EmitVertex(v[i - 1]); EmitVertex(v[i]); }
            }
            break;
        case 0xA0: // GX_TRIANGLEFAN
            for (size_t i = 2; i < n; i++) {
                EmitVertex(v[0]); EmitVertex(v[i - 1]); EmitVertex(v[i]);
            }
            break;
    }
    
    FlushBatch(); // Ensure it gets drawn immediately before state updates
}

class RendererGL : public IRenderer {
public:
    RendererGL() = default;
    ~RendererGL() override = default;

    void Initialize() override {
    }

    void Render(const std::vector<GXCommand> &commands) override {
    if (g_state.pe_clear_pending) {
        g_state.pe_clear_pending = false;
        unsigned char cc[4];
        ::GX_GetClearColor(cc);
        glClearColor(cc[0]/255.f, cc[1]/255.f, cc[2]/255.f, cc[3]/255.f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }

    for (const auto &cmd : commands) {
        if (cmd.type == GXCommandType::XFRegister) {
            int addr = cmd.reg;
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
                    if (current_addr == 0x1026) {
                        std::memcpy(&g_state.projType, &cmd.payload[i], 4);
                    }
                    touched_proj = true;
                }
            }
            if (touched_proj) {
                g_state.projSet = true;
                ApplyProjection(s_active_shader.uProjMtx);
            }
        } else if (cmd.type == GXCommandType::BPRegister) {
            if (cmd.reg == 0x40) {
                ApplyZMode();
            }
        } else if (cmd.type == GXCommandType::DrawPrimitive) {
            if (g_ctx_ptr) {
                for (int i = 0; i < 8; ++i) {
                    const auto &stage = g_state.texStages[i];
                    if (stage.base_addr != 0 && stage.width > 0 && stage.height > 0) {
                        GLuint tex = nwii::runtime::hle::TextureCache::get().get_texture(*g_ctx_ptr, stage);
                        glActiveTexture(GL_TEXTURE0 + i);
                        glBindTexture(GL_TEXTURE_2D, tex);
                    }
                }
            }
            DrawPrimitive(cmd);
        }
    }
}

    }
};

std::unique_ptr<IRenderer> CreateRendererGL() {
    return std::make_unique<RendererGL>();
}

} // namespace nwii::runtime::gx
