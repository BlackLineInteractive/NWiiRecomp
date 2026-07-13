#include "runtime/gx/renderer.h"
#include "runtime/gx_state.h"
#include <raylib.h>
#include <rlgl.h>

namespace nwii::runtime::gx {

void Renderer::Render(const std::vector<GXCommand>& commands) {
    rlDisableDepthTest();
    rlDisableBackfaceCulling();
    
    // FORCE PROJECTION MATRIX
    Matrix proj = { 0 };
    proj.m0 = 2.0f / 640.0f;
    proj.m5 = -2.0f / 480.0f;
    proj.m10 = -1.0f;
    proj.m12 = -1.0f;
    proj.m13 = 1.0f;
    proj.m15 = 1.0f;
    
    rlMatrixMode(RL_PROJECTION);
    rlLoadIdentity();
    rlMultMatrixf((float*)&proj);
    rlMatrixMode(RL_MODELVIEW);
    rlLoadIdentity();

    for (const auto& cmd : commands) {
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
            
            // Only update Raylib Projection Matrix if we actually touched it in this command
            if (addr <= 0x1026 && (addr + cmd.payload.size()) > 0x1020) {
                printf("PROJ: %f, %f, %f, %f, %f, %f, type=%f\n", 
                       g_state.projection[0], g_state.projection[1], g_state.projection[2], 
                       g_state.projection[3], g_state.projection[4], g_state.projection[5],
                       g_state.projection[6]);
                
                // The GameCube Projection matrix isn't being sent for the splash screen?
                // For now, let's just force a standard 2D ortho matrix so we can see the 2D UI
                Matrix proj = { 0 };
                if (true) { // Temporary override
                    proj.m0 = 2.0f / 640.0f;
                    proj.m5 = -2.0f / 480.0f;
                    proj.m10 = -1.0f;
                    proj.m11 = -1.0f;
                    proj.m12 = -1.0f;
                    proj.m13 = 1.0f;
                    proj.m14 = g_state.projection[3]; // Z Offset
                    proj.m15 = 1.0f;
                }
                
                rlMatrixMode(RL_PROJECTION);
                rlLoadIdentity();
                rlMultMatrixf((float*)&proj);
                rlMatrixMode(RL_MODELVIEW);
                rlLoadIdentity();
            }
        }
        else if (cmd.type == GXCommandType::DrawPrimitive) {
            rlDisableDepthTest();
            rlDisableBackfaceCulling();
            
            rlBegin(cmd.gl_mode);

            // Default color if none specified
            rlColor4ub(255, 255, 255, 255);

            for (const auto& vtx : cmd.vertices) {
                if (vtx.has_color) {
                    rlColor4f(vtx.color[0], vtx.color[1], vtx.color[2], vtx.color[3]);
                } else {
                    rlColor4ub(255, 255, 255, 255);
                }
                
                // Texture coords
                for (int t = 0; t < 8; ++t) {
                    if (vtx.has_tex[t]) {
                        if (t == 0) rlTexCoord2f(vtx.tex[t][0], vtx.tex[t][1]);
                        // rlgl only natively supports one texcoord in its simple API, 
                        // but this matches the original implementation.
                    }
                }
                
                if (vtx.has_norm) {
                    // We should ideally transform normal by inverse-transpose of upper 3x3 PosMtx
                    rlNormal3f(vtx.norm[0], vtx.norm[1], vtx.norm[2]);
                }
                
                if (vtx.has_pos) {
                    // Software ModelView Transformation
                    // Matrix index points to a row in posMatrices. Each row is 4 floats.
                    // Matrix index is typically a multiple of 3.
                    int mtx_base = vtx.posMtxIdx * 4; // GameCube uses idx*3 (i.e. 0, 3, 6, 9) representing rows. 1 row = 4 floats.
                    if (mtx_base + 11 < 256) {
                        float x = vtx.pos[0];
                        float y = vtx.pos[1];
                        float z = vtx.pos[2];
                        
                        float tx = x * g_state.posMatrices[mtx_base + 0] + y * g_state.posMatrices[mtx_base + 1] + z * g_state.posMatrices[mtx_base + 2] + g_state.posMatrices[mtx_base + 3];
                        float ty = x * g_state.posMatrices[mtx_base + 4] + y * g_state.posMatrices[mtx_base + 5] + z * g_state.posMatrices[mtx_base + 6] + g_state.posMatrices[mtx_base + 7];
                        float tz = x * g_state.posMatrices[mtx_base + 8] + y * g_state.posMatrices[mtx_base + 9] + z * g_state.posMatrices[mtx_base + 10] + g_state.posMatrices[mtx_base + 11];
                        
                        // Force Z to 0.0 to prevent OpenGL near/far clipping for 2D UI
                        rlVertex3f(tx, ty, 0.0f);
                        
                        static int vtx_print = 0;
                        if (vtx_print++ < 20) {
                            printf("VTX_DRAW: %f, %f, %f (raw: %f, %f, %f) mtx_base=%d\n", tx, ty, tz, x, y, z, mtx_base);
                        }
                    } else {
                        rlVertex3f(vtx.pos[0], vtx.pos[1], 0.0f);
                    }
                }
            }

            rlEnd();
            
            // Flush rlgl batch - default holds only 8192 verts; large meshes need explicit flush.
            rlDrawRenderBatchActive();
        }
    }
}

} // namespace nwii::runtime::gx
