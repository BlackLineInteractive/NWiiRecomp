#include "runtime/gx/renderer.h"
#include <raylib.h>
#include <rlgl.h>

namespace nwii::runtime::gx {

void Renderer::Render(const std::vector<GXCommand>& commands) {
    for (const auto& cmd : commands) {
        if (cmd.type == GXCommandType::DrawPrimitive) {
            rlBegin(cmd.gl_mode);

            // Default color if none specified
            rlColor4ub(255, 255, 255, 255);

            for (const auto& vtx : cmd.vertices) {
                if (vtx.has_color) {
                    rlColor4f(vtx.color[0], vtx.color[1], vtx.color[2], vtx.color[3]);
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
                    rlNormal3f(vtx.norm[0], vtx.norm[1], vtx.norm[2]);
                }
                
                if (vtx.has_pos) {
                    rlVertex3f(vtx.pos[0], vtx.pos[1], vtx.pos[2]);
                }
            }

            rlEnd();
            
            // Flush rlgl batch - default holds only 8192 verts; large meshes need explicit flush.
            rlDrawRenderBatchActive();
        }
    }
}

} // namespace nwii::runtime::gx
