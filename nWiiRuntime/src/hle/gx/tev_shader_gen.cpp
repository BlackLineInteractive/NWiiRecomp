#include "runtime/gx/tev_shader_gen.h"
#include <sstream>

namespace nwii::runtime::gx {

GeneratedShader GenerateTEVShader(const GXState& state, uint8_t prim_type) {
    GeneratedShader shader;
    
    // VERTEX SHADER
    std::stringstream vs;
    vs << "#version 330 core\n";
    vs << "layout (location = 0) in vec3 aPos;\n";
    vs << "layout (location = 1) in vec4 aColor0;\n";
    vs << "layout (location = 2) in vec4 aColor1;\n";
    vs << "layout (location = 3) in vec2 aTex0;\n";
    vs << "layout (location = 4) in vec2 aTex1;\n";
    vs << "layout (location = 5) in vec2 aTex2;\n";
    vs << "layout (location = 6) in vec2 aTex3;\n";
    vs << "layout (location = 7) in vec2 aTex4;\n";
    vs << "layout (location = 8) in vec2 aTex5;\n";
    vs << "layout (location = 9) in vec2 aTex6;\n";
    vs << "layout (location = 10) in vec2 aTex7;\n";
    
    vs << "out vec4 vColor0;\n";
    vs << "out vec4 vColor1;\n";
    vs << "out vec2 vTex0;\n";
    vs << "out vec2 vTex1;\n";
    vs << "out vec2 vTex2;\n";
    vs << "out vec2 vTex3;\n";
    vs << "out vec2 vTex4;\n";
    vs << "out vec2 vTex5;\n";
    vs << "out vec2 vTex6;\n";
    vs << "out vec2 vTex7;\n";
    
    vs << "uniform mat4 mvp;\n";
    
    vs << "void main() {\n";
    vs << "    gl_Position = mvp * vec4(aPos, 1.0);\n";
    vs << "    vColor0 = aColor0;\n";
    vs << "    vColor1 = aColor1;\n";
    vs << "    vTex0 = aTex0;\n";
    vs << "    vTex1 = aTex1;\n";
    vs << "    vTex2 = aTex2;\n";
    vs << "    vTex3 = aTex3;\n";
    vs << "    vTex4 = aTex4;\n";
    vs << "    vTex5 = aTex5;\n";
    vs << "    vTex6 = aTex6;\n";
    vs << "    vTex7 = aTex7;\n";
    vs << "}\n";
    shader.vertex_source = vs.str();
    
    // FRAGMENT SHADER
    std::stringstream fs;
    fs << "#version 330 core\n";
    fs << "in vec4 vColor0;\n";
    fs << "in vec4 vColor1;\n";
    fs << "in vec2 vTex0;\n";
    fs << "in vec2 vTex1;\n";
    fs << "in vec2 vTex2;\n";
    fs << "in vec2 vTex3;\n";
    fs << "in vec2 vTex4;\n";
    fs << "in vec2 vTex5;\n";
    fs << "in vec2 vTex6;\n";
    fs << "in vec2 vTex7;\n";
    fs << "out vec4 FragColor;\n";
    
    for (int i = 0; i < 8; ++i) {
        fs << "uniform sampler2D uTex" << i << ";\n";
    }
    fs << "uniform vec4 uTevColor[4];\n";
    fs << "uniform vec4 uTevKColor[4];\n";
    
    fs << "vec4 tevReg[4];\n"; // 0=prev, 1=reg0, 2=reg1, 3=reg2
    fs << "vec4 texColor;\n";
    fs << "vec4 rasColor;\n";
    
    fs << "void main() {\n";
    fs << "    tevReg[0] = uTevColor[0];\n";
    fs << "    tevReg[1] = uTevColor[1];\n";
    fs << "    tevReg[2] = uTevColor[2];\n";
    fs << "    tevReg[3] = uTevColor[3];\n";
    
    uint8_t numTevs = state.numTevStages;
    if (numTevs == 0) numTevs = 1;
    
    for (int i = 0; i < numTevs; ++i) {
        const auto& stage = state.tevStages[i];
        fs << "    // TEV Stage " << i << "\n";
        
        // Rasterized color selection
        if (stage.colorChan == 0) fs << "    rasColor = vColor0;\n";
        else if (stage.colorChan == 1) fs << "    rasColor = vColor1;\n";
        else fs << "    rasColor = vec4(0.0, 0.0, 0.0, 1.0);\n";
        
        // Texture selection
        if (stage.texMap != 0xFF) {
            fs << "    texColor = texture(uTex" << (int)stage.texMap << ", vTex" << (int)stage.texCoord << ");\n";
        } else {
            fs << "    texColor = vec4(1.0);\n";
        }
        
        uint32_t ksel_reg = state.bp[0xF6 + (i / 2)];
        int kcsel = 0, kasel = 0;
        if ((i % 2) == 0) {
            kcsel = (ksel_reg >> 4) & 0x1F;
            kasel = (ksel_reg >> 9) & 0x1F;
        } else {
            kcsel = (ksel_reg >> 14) & 0x1F;
            kasel = (ksel_reg >> 19) & 0x1F;
        }
        
        auto get_konst_color = [&](int kc) -> std::string {
            if (kc <= 0x07) {
                switch(kc) {
                    case 0x00: return "vec3(1.0)";
                    case 0x01: return "vec3(0.875)";
                    case 0x02: return "vec3(0.25)";
                    case 0x03: return "vec3(0.125)";
                    default: return "vec3(0.0)";
                }
            } else if (kc >= 0x0C && kc <= 0x0F) {
                return "uTevKColor[" + std::to_string(kc - 0x0C) + "].rgb";
            } else if (kc >= 0x10 && kc <= 0x1F) {
                int idx = (kc - 0x10) % 4;
                int comp = (kc - 0x10) / 4;
                const char* comps[] = {"r", "g", "b", "a"};
                return "vec3(uTevKColor[" + std::to_string(idx) + "]." + comps[comp] + ")";
            }
            return "vec3(0.0)";
        };

        auto get_konst_alpha = [&](int ka) -> std::string {
            if (ka <= 0x07) {
                switch(ka) {
                    case 0x00: return "1.0";
                    case 0x01: return "0.875";
                    case 0x02: return "0.25";
                    case 0x03: return "0.125";
                    default: return "0.0";
                }
            } else if (ka >= 0x10 && ka <= 0x1F) {
                int idx = (ka - 0x10) % 4;
                int comp = (ka - 0x10) / 4;
                const char* comps[] = {"r", "g", "b", "a"};
                return "uTevKColor[" + std::to_string(idx) + "]." + comps[comp];
            }
            return "0.0";
        };
        std::string kc_str = get_konst_color(kcsel);
        std::string ka_str = get_konst_alpha(kasel);
        
        fs << "    vec3 cIn[4];\n";
        auto map_color_input = [&](int in_idx, int arg) {
            switch(arg) {
                case 0: fs << "    cIn[" << in_idx << "] = tevReg[0].rgb;\n"; break;
                case 1: fs << "    cIn[" << in_idx << "] = vec3(tevReg[0].a);\n"; break;
                case 2: fs << "    cIn[" << in_idx << "] = tevReg[1].rgb;\n"; break;
                case 3: fs << "    cIn[" << in_idx << "] = vec3(tevReg[1].a);\n"; break;
                case 4: fs << "    cIn[" << in_idx << "] = tevReg[2].rgb;\n"; break;
                case 5: fs << "    cIn[" << in_idx << "] = vec3(tevReg[2].a);\n"; break;
                case 6: fs << "    cIn[" << in_idx << "] = tevReg[3].rgb;\n"; break;
                case 7: fs << "    cIn[" << in_idx << "] = vec3(tevReg[3].a);\n"; break;
                case 8: fs << "    cIn[" << in_idx << "] = texColor.rgb;\n"; break;
                case 9: fs << "    cIn[" << in_idx << "] = vec3(texColor.a);\n"; break;
                case 10: fs << "    cIn[" << in_idx << "] = rasColor.rgb;\n"; break;
                case 11: fs << "    cIn[" << in_idx << "] = vec3(rasColor.a);\n"; break;
                case 12: fs << "    cIn[" << in_idx << "] = vec3(1.0);\n"; break;
                case 13: fs << "    cIn[" << in_idx << "] = vec3(0.5);\n"; break;
                case 14: fs << "    cIn[" << in_idx << "] = " << kc_str << ";\n"; break; // Konst
                case 15: fs << "    cIn[" << in_idx << "] = vec3(0.0);\n"; break;
                default: fs << "    cIn[" << in_idx << "] = vec3(0.0);\n"; break;
            }
        };
        map_color_input(0, stage.colorInA);
        map_color_input(1, stage.colorInB);
        map_color_input(2, stage.colorInC);
        map_color_input(3, stage.colorInD);
        
        fs << "    vec3 cBias;\n";
        if (stage.colorBias == 1) fs << "    cBias = vec3(0.5);\n";
        else if (stage.colorBias == 2) fs << "    cBias = vec3(-0.5);\n";
        else fs << "    cBias = vec3(0.0);\n";

        fs << "    vec3 cOut;\n";
        if (stage.colorOp == 0) { // Add
            fs << "    cOut = cIn[3] + mix(cIn[0], cIn[1], cIn[2]) + cBias;\n";
        } else if (stage.colorOp == 1) { // Sub
            fs << "    cOut = cIn[3] - mix(cIn[0], cIn[1], cIn[2]) + cBias;\n";
        } else {
            fs << "    cOut = cIn[3] + mix(cIn[0], cIn[1], cIn[2]) + cBias;\n";
        }
        
        if (stage.colorScale == 1) fs << "    cOut *= 2.0;\n";
        else if (stage.colorScale == 2) fs << "    cOut *= 4.0;\n";
        else if (stage.colorScale == 3) fs << "    cOut *= 0.5;\n";

        if (stage.colorClamp == 1) fs << "    cOut = clamp(cOut, 0.0, 1.0);\n";

        fs << "    tevReg[" << (int)stage.colorRegId << "].rgb = cOut;\n";
        
        fs << "    float aIn[4];\n";
        auto map_alpha_input = [&](int in_idx, int arg) {
            switch(arg) {
                case 0: fs << "    aIn[" << in_idx << "] = tevReg[0].a;\n"; break;
                case 1: fs << "    aIn[" << in_idx << "] = tevReg[1].a;\n"; break;
                case 2: fs << "    aIn[" << in_idx << "] = tevReg[2].a;\n"; break;
                case 3: fs << "    aIn[" << in_idx << "] = tevReg[3].a;\n"; break;
                case 4: fs << "    aIn[" << in_idx << "] = texColor.a;\n"; break;
                case 5: fs << "    aIn[" << in_idx << "] = rasColor.a;\n"; break;
                case 6: fs << "    aIn[" << in_idx << "] = " << ka_str << ";\n"; break; // Konst
                case 7: fs << "    aIn[" << in_idx << "] = 0.0;\n"; break;
                default: fs << "    aIn[" << in_idx << "] = 0.0;\n"; break;
            }
        };
        map_alpha_input(0, stage.alphaInA);
        map_alpha_input(1, stage.alphaInB);
        map_alpha_input(2, stage.alphaInC);
        map_alpha_input(3, stage.alphaInD);
        
        fs << "    float aBias;\n";
        if (stage.alphaBias == 1) fs << "    aBias = 0.5;\n";
        else if (stage.alphaBias == 2) fs << "    aBias = -0.5;\n";
        else fs << "    aBias = 0.0;\n";

        fs << "    float aOut;\n";
        if (stage.alphaOp == 0) { // Add
            fs << "    aOut = aIn[3] + mix(aIn[0], aIn[1], aIn[2]) + aBias;\n";
        } else if (stage.alphaOp == 1) { // Sub
            fs << "    aOut = aIn[3] - mix(aIn[0], aIn[1], aIn[2]) + aBias;\n";
        } else {
            fs << "    aOut = aIn[3] + mix(aIn[0], aIn[1], aIn[2]) + aBias;\n";
        }
        
        if (stage.alphaScale == 1) fs << "    aOut *= 2.0;\n";
        else if (stage.alphaScale == 2) fs << "    aOut *= 4.0;\n";
        else if (stage.alphaScale == 3) fs << "    aOut *= 0.5;\n";

        if (stage.alphaClamp == 1) fs << "    aOut = clamp(aOut, 0.0, 1.0);\n";

        fs << "    tevReg[" << (int)stage.alphaRegId << "].a = aOut;\n";
    }
    
    fs << "    FragColor = tevReg[0];\n";
    
    // Alpha test placeholder
    fs << "    if (FragColor.a < 0.0) discard;\n";
    
    fs << "}\n";
    shader.fragment_source = fs.str();
    
    return shader;
}

} // namespace nwii::runtime::gx
