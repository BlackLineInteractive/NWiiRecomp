#include "runtime/gx/renderer.h"
#include "runtime/gx/fifo_parser.h"
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
// Reported in the [STAT] line: see the note at the GetShaderHash call site.
uint64_t g_stat_hash0 = 0;
uint64_t g_stat_shaders = 0;

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
    
    if (std::getenv("NWII_GXTRACE")) {
        static int dn = 0;
        if (dn++ < 5)
            printf("[GXTRACE] flush #%d verts=%zu\n", dn, s_batch.size());
    }
    glDrawArrays(GL_TRIANGLES, 0, s_batch.size());
    if (std::getenv("NWII_GXTRACE")) {
        static int en = 0;
        if (en++ < 8) {
            GLint prog = 0, linked = 0;
            glGetIntegerv(GL_CURRENT_PROGRAM, &prog);
            if (prog) glGetProgramiv(prog, GL_LINK_STATUS, &linked);
            GLenum err = glGetError();
            printf("[GXTRACE] draw: prog=%d linked=%d glErr=0x%x verts=%zu\n",
                   prog, linked, err, s_batch.size());
            if (prog && !linked) {
                char log[512] = {0}; GLsizei n = 0;
                glGetProgramInfoLog(prog, 511, &n, log);
                printf("[GXTRACE] link log: %s\n", log);
            }
        }
    }
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
    if (std::getenv("NWII_VTXTRACE")) {
        static int pn = 0;
        if (pn++ < 2)
            printf("[PROJ] type=%u set=%d raw=[%.5f %.2f %.5f %.2f %.2f %.2f]\n",
                   g_state.projType, (int)g_state.projSet, sp[0], sp[1], sp[2],
                   sp[3], sp[4], sp[5]);
    }
    glUniformMatrix4fv(uProj, 1, GL_FALSE, p);
}

static void ApplyZMode() {
    if (g_state.zMode.enable) {
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);
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
    if (std::getenv("NWII_FLATZ")) bv.z = 0.0f;
    if (std::getenv("NWII_VTXTRACE")) {
        static int vn = 0;
        if (vn++ < 14)
            printf("[VTX] raw=(%.1f,%.1f,%.1f) view=(%.1f,%.1f,%.1f) uv=(%.3f,%.3f) tex0=%ux%u fmt=%u\n",
                   vtx.pos[0], vtx.pos[1], vtx.pos[2], bv.x, bv.y, bv.z,
                   bv.u, bv.v, g_state.texStages[0].width,
                   g_state.texStages[0].height, g_state.texStages[0].format);
    }
    s_batch.push_back(bv);
}

static void DrawPrimitive(const GXCommand &cmd) {
    const auto &v = cmd.vertices;
    const size_t n = v.size();
    if (n == 0) return;

    // NWII_DRAWLOG=1: one line per draw for the first N draws, so a good and a
    // bad run can be diffed directly. Every aggregate (shader hash, TEV regs,
    // projection, viewport) is identical between them, so the divergence has to
    // be per-draw.
    if (std::getenv("NWII_DRAWLOG")) {
        static int dn = 0;
        if (dn < 40) {
            // Everything else about a draw matches between a good and a bad
            // run, so print exactly what feeds the hash.
            printf("[DRAW %02d] prim=0x%02X verts=%zu hash=%llx tex0=%dx%d fmt=%d map=%d "
                   "| bp00=%06X F3=%06X C0=%06X C1=%06X C2=%06X C3=%06X 28=%06X cp50=%08X\n",
                   dn, cmd.prim_type, n,
                   (unsigned long long)g_state.GetShaderHash(cmd.prim_type),
                   g_state.texStages[0].width, g_state.texStages[0].height,
                   g_state.texStages[0].format, (int)g_state.tevStages[0].texMap,
                   g_state.bp[0x00], g_state.bp[0xF3], g_state.bp[0xC0],
                   g_state.bp[0xC1], g_state.bp[0xC2], g_state.bp[0xC3],
                   g_state.bp[0x28], g_state.cp[0x50]);
            // The palette is what colours a C8 texture, and it is copied out of
            // guest RAM once, at render time. Print it: a zeroed TLUT means we
            // captured it before the game's DMA landed, and nothing re-captures.
            uint32_t th = 2166136261u, nz = 0;
            uint32_t off = g_state.texStages[0].tlut_offset;
            for (uint32_t i = 0; i < 512 && off + i < sizeof(g_state.tlutMem); ++i) {
                uint8_t b = g_state.tlutMem[off + i];
                th = (th ^ b) * 16777619u;
                if (b) ++nz;
            }
            // The texel data itself: the last input to a draw never compared
            // between a good and a bad run.
            uint32_t dh = 2166136261u, dnz = 0;
            uint32_t base = g_state.texStages[0].base_addr;
            if (g_ctx_ptr) {
                for (uint32_t i = 0; i < 2048; ++i) {
                    uint8_t b = g_ctx_ptr->mmu.read8(base + i);
                    dh = (dh ^ b) * 16777619u;
                    if (b) ++dnz;
                }
            }
            printf("          tlut off=%u hash=%08X nonzero=%u/512 src=%08X | "
                   "texdata base=%08X hash=%08X nonzero=%u/2048\n",
                   off, th, nz, g_state.tlutSrcAddr, base, dh, dnz);
            ++dn;
        }
    }

    FlushBatch();

    bool use_shader = (std::getenv("NWII_NOSHADER") == nullptr);
    if (!use_shader) return;

    uint64_t hash = g_state.GetShaderHash(cmd.prim_type);
    // Diagnostics: a shader compiled from the wrong state is cached for the
    // whole run, so the FIRST hash decides how the run looks. Reported in [STAT].
    if (!g_stat_hash0)
        g_stat_hash0 = hash;
    g_stat_shaders = (uint64_t)s_shader_cache.size();
    auto it = s_shader_cache.find(hash);
    if (it != s_shader_cache.end()) {
        s_active_shader = it->second;
    } else {
        auto src = GenerateTEVShader(g_state, cmd.prim_type);
        if (std::getenv("NWII_SHADERDUMP")) {
            static int sn = 0;
            if (sn++ < 2)
                printf("[SHADER %d] ==== FS ====\n%s\n============\n",
                       sn, src.fragment_source.c_str());
        }
        GLShader sh = {};
        sh.uProjMtx = -1;
        for (int i = 0; i < 8; i++) { sh.uTex[i] = -1; sh.uTexMtx[i] = -1; }
        sh.uTevColor = sh.uTevKColor = -1;
        sh.id = CompileShader(src.vertex_source.c_str(), src.fragment_source.c_str());
        if (std::getenv("NWII_SHADERDUMP")) {
            static int vn2 = 0;
            if (vn2++ < 1) {
                printf("==== VS ====\n%s\n", src.vertex_source.c_str());
                GLint ok = 0;
                glGetProgramiv(sh.id, GL_LINK_STATUS, &ok);
                printf("link=%d\n", ok);
                char log[1024] = {0}; GLsizei n = 0;
                glGetProgramInfoLog(sh.id, 1023, &n, log);
                if (n) printf("proglog: %s\n", log);
                GLint na = 0;
                glGetProgramiv(sh.id, GL_ACTIVE_UNIFORMS, &na);
                printf("active uniforms=%d:", na);
                for (GLint i = 0; i < na; i++) {
                    char nm[64]; GLsizei len; GLint sz; GLenum ty;
                    glGetActiveUniform(sh.id, i, 63, &len, &sz, &ty, nm);
                    printf(" %s", nm);
                }
                printf("\n");
            }
        }
        for (int i=0; i<8; i++) {
            sh.uTex[i] = glGetUniformLocation(sh.id, ("uTex" + std::to_string(i)).c_str());
            sh.uTexMtx[i] = glGetUniformLocation(sh.id, ("uTexMtx[" + std::to_string(i) + "]").c_str());
        }
        sh.uTevKColor = glGetUniformLocation(sh.id, "uTevKColor[0]");
        if (sh.uTevKColor < 0)
            sh.uTevKColor = glGetUniformLocation(sh.id, "uTevKColor");
        sh.uTevColor = glGetUniformLocation(sh.id, "uTevColor[0]");
        if (sh.uTevColor < 0)
            sh.uTevColor = glGetUniformLocation(sh.id, "uTevColor");
        if (std::getenv("NWII_GXTRACE")) {
            static int un = 0;
            if (un++ < 3)
                printf("[GXTRACE] uniforms: mvp=%d tevColor=%d tevKColor=%d\n",
                       sh.uProjMtx, sh.uTevColor, sh.uTevKColor);
        }
        sh.uProjMtx = glGetUniformLocation(sh.id, "mvp");
        
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
            float mat[16] = {0};
            mat[0] = mat[5] = mat[10] = mat[15] = 1.0f;
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
            color[i*4 + 0] = decode11(ra, 0);
            color[i*4 + 3] = decode11(ra, 12);
        } else {
            kcolor[i*4 + 0] = decode11(ra, 0);
            kcolor[i*4 + 3] = decode11(ra, 12);
        }
        if (((bg >> 23) & 1) == 0) {
            color[i*4 + 2] = decode11(bg, 0);
            color[i*4 + 1] = decode11(bg, 12);
        } else {
            kcolor[i*4 + 2] = decode11(bg, 0);
            kcolor[i*4 + 1] = decode11(bg, 12);
        }
    }
    
    if (std::getenv("NWII_TEVTRACE")) {
        static int tn = 0;
        if (tn++ < 4)
            printf("[TEV] bp E0=%06X E1=%06X E2=%06X E3=%06X | color0=(%.2f,%.2f,%.2f,%.2f) color1=(%.2f,%.2f,%.2f,%.2f)\n",
                   g_state.bp[0xE0], g_state.bp[0xE1], g_state.bp[0xE2], g_state.bp[0xE3],
                   color[0], color[1], color[2], color[3],
                   color[4], color[5], color[6], color[7]);
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
    
    FlushBatch();
}

class RendererGL : public IRenderer {
public:
    RendererGL() = default;
    ~RendererGL() override = default;

    void Initialize(void* window) override {
        m_window = window;
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
            ApplyBPRegister((uint8_t)cmd.reg, cmd.val);
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

    void Present() override {}

private:
    void* m_window = nullptr;

}; // class RendererGL

std::unique_ptr<IRenderer> CreateRendererGL() {
    return std::make_unique<RendererGL>();
}

} // namespace nwii::runtime::gx

// Accessors for the [STAT] diagnostics line in main.cpp.
uint64_t nwii_stat_hash0() { return nwii::runtime::gx::g_stat_hash0; }
uint64_t nwii_stat_shaders() { return nwii::runtime::gx::g_stat_shaders; }

namespace nwii::runtime::gx {

} // namespace nwii::runtime::gx
