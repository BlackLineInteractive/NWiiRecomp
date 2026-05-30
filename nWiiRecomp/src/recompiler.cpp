#include "recompiler/recompiler.h"
#include "ppc/instruction.h"
#include <fstream>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <filesystem>

namespace nwii {
namespace recomp {

Recompiler::Recompiler(const analyzer::Analyzer& analyzer, const SymbolTable* symbols) 
    : analyzer_(analyzer), symbols_(symbols) {}

static bool is_hle_function(const std::string& name) {
    const char* prefixes[] = {"OS", "GX", "DVD", "VI", "WPAD", "AX", "EXI", "PAD", "mtx_", "vec_"};
    for (const char* prefix : prefixes) {
        if (name.starts_with(prefix)) {
            return true;
        }
    }
    return false;
}

bool Recompiler::generate_cpp(const std::string& output_path) {
    std::ofstream out(output_path);
    if (!out.is_open()) {
        std::cerr << "Failed to open output file: " << output_path << std::endl;
        return false;
    }

    // Emit headers
    out << "#include \"runtime/cpu_context.h\"\n";
    out << "#include <stdint.h>\n";
    out << "#include <bit>\n\n";
    out << "using namespace nwii::runtime;\n\n";

    out << "// --- Forward Declarations ---\n";
    for (const auto& [start_addr, func] : analyzer_.get_functions()) {
        std::string func_name;
        if (symbols_ && symbols_->has_symbol(start_addr)) {
            func_name = symbols_->get_symbol(start_addr);
        } else {
            std::stringstream ss;
            ss << "func_" << std::hex << std::uppercase << std::setfill('0') << std::setw(8) << start_addr;
            func_name = ss.str();
        }
        out << "void " << func_name << "(CPUContext& ctx);\n";
    }
    out << "\n// --- Function Bodies ---\n";

    // Emit all functions
    for (const auto& [start_addr, func] : analyzer_.get_functions()) {
        if (symbols_ && symbols_->has_symbol(start_addr)) {
            std::string name = symbols_->get_symbol(start_addr);
            if (is_hle_function(name)) {
                out << "// [HLE Hook] Skipping generation for " << name << "\n\n";
                continue;
            }
        }
        emit_function(out, func);
    }

    return true;
}

bool Recompiler::generate_cmake_project(const std::string& output_dir, const std::string& runtime_source_path) {
    if (!std::filesystem::exists(output_dir)) {
        std::filesystem::create_directories(output_dir);
    }
    
    // 1. Generate output.cpp
    std::string cpp_path = output_dir + "/output.cpp";
    if (!generate_cpp(cpp_path)) {
        return false;
    }
    
    // 2. Copy nWiiRuntime
    std::string runtime_dest = output_dir + "/nWiiRuntime";
    try {
        std::filesystem::copy(runtime_source_path, runtime_dest, 
            std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing);
    } catch (const std::exception& e) {
        std::cerr << "Failed to copy runtime: " << e.what() << "\n";
        return false;
    }
    
    // 3. Generate CMakeLists.txt
    std::string cmake_path = output_dir + "/CMakeLists.txt";
    std::ofstream out(cmake_path);
    if (!out.is_open()) return false;
    
    out << "cmake_minimum_required(VERSION 3.20)\n";
    out << "project(RecompiledGame LANGUAGES CXX)\n\n";
    out << "set(CMAKE_CXX_STANDARD 20)\n";
    out << "set(CMAKE_CXX_STANDARD_REQUIRED ON)\n\n";
    out << "add_subdirectory(nWiiRuntime)\n\n";
    out << "add_executable(RecompiledGame output.cpp)\n";
    out << "target_link_libraries(RecompiledGame PRIVATE nwiiruntime)\n";
    
    return true;
}

std::string Recompiler::generate_function_cpp(const analyzer::Function& func) {
    std::stringstream ss;
    emit_function(ss, func);
    return ss.str();
}

void Recompiler::emit_function(std::ostream& out, const analyzer::Function& func) {
    std::string func_name;
    if (symbols_ && symbols_->has_symbol(func.start_address)) {
        func_name = symbols_->get_symbol(func.start_address);
    } else {
        std::stringstream ss;
        ss << "func_" << std::hex << std::uppercase << std::setfill('0') << std::setw(8) << func.start_address;
        func_name = ss.str();
    }

    out << "void " << func_name << "(CPUContext& ctx) {\n";

    for (const auto& inst : func.instructions) {
        emit_instruction(out, inst, func);
    }

    out << "}\n\n";
}

void Recompiler::emit_instruction(std::ostream& out, const analyzer::Instruction& inst, const analyzer::Function& func) {
    ppc::Instruction ppc_inst(inst.opcode);
    
    out << "loc_" << std::hex << std::uppercase << inst.address << std::dec << ":\n";
    out << "    // 0x" << std::hex << std::setfill('0') << std::setw(8) << inst.address << ": ";
    out << std::setfill('0') << std::setw(8) << inst.opcode << std::dec << "\n";

    if (ppc_inst.is_branch_to_lr()) {
        out << "    return;\n";
    } else if (ppc_inst.opcode() == 14) {
        // ADDI: rD = (rA|0) + SIMM
        uint32_t rD = ppc_inst.rd();
        uint32_t rA = ppc_inst.ra();
        int16_t simm = ppc_inst.simm();
        
        if (rA == 0) {
            out << "    ctx.gpr[" << rD << "] = " << simm << "; // li r" << rD << ", " << simm << "\n";
        } else {
            out << "    ctx.gpr[" << rD << "] = ctx.gpr[" << rA << "] + " << simm << "; // addi r" << rD << ", r" << rA << ", " << simm << "\n";
        }
    } else if (ppc_inst.opcode() == 31 && ppc_inst.extended_opcode() == 444) {
        // OR rA, rS, rB
        uint32_t rS = ppc_inst.rs();
        uint32_t rA = ppc_inst.ra();
        uint32_t rB = ppc_inst.rb();
        if (rS == rB) {
            out << "    ctx.gpr[" << rA << "] = ctx.gpr[" << rS << "]; // mr r" << rA << ", r" << rS << "\n";
        } else {
            out << "    ctx.gpr[" << rA << "] = ctx.gpr[" << rS << "] | ctx.gpr[" << rB << "]; // or r" << rA << ", r" << rS << ", r" << rB << "\n";
        }
    } else if (ppc_inst.opcode() == 31 && ppc_inst.extended_opcode() == 339) {
        // mfspr
        uint32_t rD = ppc_inst.rd();
        uint32_t spr_bottom = (ppc_inst.value() >> 16) & 0x1F;
        uint32_t spr_top = (ppc_inst.value() >> 11) & 0x1F;
        uint32_t spr = (spr_top << 5) | spr_bottom;
        if (spr == 8) {
            out << "    ctx.gpr[" << rD << "] = ctx.lr; // mflr r" << rD << "\n";
        } else {
            out << "    // TODO: mfspr r" << rD << ", " << spr << "\n";
        }
    } else if (ppc_inst.opcode() == 31 && ppc_inst.extended_opcode() == 467) {
        // mtspr
        uint32_t rS = ppc_inst.rs();
        uint32_t spr_bottom = (ppc_inst.value() >> 16) & 0x1F;
        uint32_t spr_top = (ppc_inst.value() >> 11) & 0x1F;
        uint32_t spr = (spr_top << 5) | spr_bottom;
        if (spr == 8) {
            out << "    ctx.lr = ctx.gpr[" << rS << "]; // mtlr r" << rS << "\n";
        } else {
            out << "    // TODO: mtspr " << spr << ", r" << rS << "\n";
        }
    } else if (ppc_inst.opcode() == 31 && ppc_inst.extended_opcode() == 0) {
        // CMPW crD, rA, rB
        uint32_t crD = ppc_inst.rd() >> 2; // crfD is top 3 bits of rD field
        uint32_t rA = ppc_inst.ra();
        uint32_t rB = ppc_inst.rb();
        
        out << "    int32_t cmp_val_A = (int32_t)ctx.gpr[" << rA << "];\n";
        out << "    int32_t cmp_val_B = (int32_t)ctx.gpr[" << rB << "];\n";
        out << "    ctx.cr[" << crD << "].lt = (cmp_val_A < cmp_val_B);\n";
        out << "    ctx.cr[" << crD << "].gt = (cmp_val_A > cmp_val_B);\n";
        out << "    ctx.cr[" << crD << "].eq = (cmp_val_A == cmp_val_B);\n";
    } else if (ppc_inst.opcode() == 32) {
        // LWZ: rD = read32(rA + SIMM)
        uint32_t rD = ppc_inst.rd();
        uint32_t rA = ppc_inst.ra();
        int16_t simm = ppc_inst.simm();
        if (rA == 0) {
            out << "    ctx.gpr[" << rD << "] = ctx.mmu.read32(" << simm << "); // lwz r" << rD << ", " << simm << "(0)\n";
        } else {
            out << "    ctx.gpr[" << rD << "] = ctx.mmu.read32(ctx.gpr[" << rA << "] + " << simm << "); // lwz r" << rD << ", " << simm << "(r" << rA << ")\n";
        }
    } else if (ppc_inst.opcode() == 36) {
        // STW: write32(rA + SIMM, rS)
        uint32_t rS = ppc_inst.rs();
        uint32_t rA = ppc_inst.ra();
        int16_t simm = ppc_inst.simm();
        if (rA == 0) {
            out << "    ctx.mmu.write32(" << simm << ", ctx.gpr[" << rS << "]); // stw r" << rS << ", " << simm << "(0)\n";
        } else {
            out << "    ctx.mmu.write32(ctx.gpr[" << rA << "] + " << simm << ", ctx.gpr[" << rS << "]); // stw r" << rS << ", " << simm << "(r" << rA << ")\n";
        }
    } else if (ppc_inst.opcode() == 37) {
        // stwu rS, d(rA)
        uint32_t rS = ppc_inst.rs();
        uint32_t rA = ppc_inst.ra();
        int16_t simm = ppc_inst.simm();
        out << "    ctx.gpr[" << rA << "] = ctx.gpr[" << rA << "] + " << simm << ";\n";
        out << "    ctx.mmu.write32(ctx.gpr[" << rA << "], ctx.gpr[" << rS << "]); // stwu r" << rS << ", " << simm << "(r" << rA << ")\n";
    } else if (ppc_inst.opcode() == 16) {
        // BC (Branch Conditional)
        uint32_t bo = ppc_inst.bo();
        uint32_t bi = ppc_inst.bi();
        uint32_t cr_idx = bi / 4;
        uint32_t cr_bit = bi % 4;
        
        std::string cond_field;
        if (cr_bit == 0) cond_field = "lt";
        else if (cr_bit == 1) cond_field = "gt";
        else if (cr_bit == 2) cond_field = "eq";
        else cond_field = "so";
        
        // Target calculation
        int16_t bd = ppc_inst.simm() & 0xFFFC; // mask out AA and LK
        int32_t target = bd; // simple branch relative
        if ((inst.opcode & 2) == 2) { // AA bit
            target = bd; // absolute
            out << "    // Absolute branches not fully supported in simple string emit\n";
        } else {
            target = inst.address + bd;
        }

        std::stringstream ss;
        ss << "loc_" << std::hex << std::uppercase << target;
        std::string target_lbl = ss.str();

        // Simple BO decodes:
        if ((bo & 0x14) == 0x04) {
            // Branch if condition false (e.g. bne is 'branch if not equal')
            out << "    if (!ctx.cr[" << cr_idx << "]." << cond_field << ") goto " << target_lbl << "; // bne/blt/bgt false\n";
        } else if ((bo & 0x14) == 0x0C) {
            // Branch if condition true (e.g. beq is 'branch if equal')
            out << "    if (ctx.cr[" << cr_idx << "]." << cond_field << ") goto " << target_lbl << "; // beq/blt/bgt true\n";
        } else {
            out << "    // TODO: implement complex branch conditional BO=" << bo << "\n";
        }
    } else if (ppc_inst.opcode() == 18) {
        // b, bl, ba, bla
        uint32_t target = ppc_inst.branch_target(inst.address);
        std::string target_name;
        if (symbols_ && symbols_->has_symbol(target)) {
            target_name = symbols_->get_symbol(target);
        } else {
            std::stringstream ss;
            ss << "func_" << std::hex << std::uppercase << std::setfill('0') << std::setw(8) << target;
            target_name = ss.str();
        }

        if (ppc_inst.is_branch_link()) {
            out << "    ctx.lr = 0x" << std::hex << std::uppercase << (inst.address + 4) << std::dec << "; // save return address\n";
            out << "    " << target_name << "(ctx);\n";
        } else {
            // unconditional jump
            out << "    goto loc_" << std::hex << std::uppercase << target << ";\n";
        }
    } else if (ppc_inst.opcode() == 48) {
        // lfs
        uint32_t fD = ppc_inst.rd();
        uint32_t rA = ppc_inst.ra();
        int16_t simm = ppc_inst.simm();
        if (rA == 0) {
            out << "    ctx.fpr[" << fD << "] = ctx.mmu.read_f32(" << simm << "); // lfs f" << fD << ", " << simm << "(0)\n";
        } else {
            out << "    ctx.fpr[" << fD << "] = ctx.mmu.read_f32(ctx.gpr[" << rA << "] + " << simm << "); // lfs f" << fD << ", " << simm << "(r" << rA << ")\n";
        }
    } else if (ppc_inst.opcode() == 52) {
        // stfs
        uint32_t fS = ppc_inst.rs();
        uint32_t rA = ppc_inst.ra();
        int16_t simm = ppc_inst.simm();
        if (rA == 0) {
            out << "    ctx.mmu.write_f32(" << simm << ", (float)ctx.fpr[" << fS << "]); // stfs f" << fS << ", " << simm << "(0)\n";
        } else {
            out << "    ctx.mmu.write_f32(ctx.gpr[" << rA << "] + " << simm << ", (float)ctx.fpr[" << fS << "]); // stfs f" << fS << ", " << simm << "(r" << rA << ")\n";
        }
    } else if (ppc_inst.opcode() == 59) {
        uint32_t xo = (ppc_inst.value() >> 1) & 0x1F;
        uint32_t fD = ppc_inst.rd();
        uint32_t fA = ppc_inst.ra();
        uint32_t fB = ppc_inst.rb();
        uint32_t fC = ppc_inst.rc();
        
        if (xo == 21) {
            out << "    ctx.fpr[" << fD << "] = (float)(ctx.fpr[" << fA << "] + ctx.fpr[" << fB << "]); // fadds f" << fD << ", f" << fA << ", f" << fB << "\n";
        } else if (xo == 20) {
            out << "    ctx.fpr[" << fD << "] = (float)(ctx.fpr[" << fA << "] - ctx.fpr[" << fB << "]); // fsubs f" << fD << ", f" << fA << ", f" << fB << "\n";
        } else if (xo == 25) {
            out << "    ctx.fpr[" << fD << "] = (float)(ctx.fpr[" << fA << "] * ctx.fpr[" << fC << "]); // fmuls f" << fD << ", f" << fA << ", f" << fC << "\n";
        } else {
            out << "    // TODO: implement Opcode 59 XO " << xo << "\n";
        }
    } else if (ppc_inst.opcode() == 63) {
        uint32_t xo = (ppc_inst.value() >> 1) & 0x3FF;
        uint32_t fD = ppc_inst.rd();
        uint32_t fB = ppc_inst.rb();
        if (xo == 72) {
            out << "    ctx.fpr[" << fD << "] = ctx.fpr[" << fB << "]; // fmr f" << fD << ", f" << fB << "\n";
        } else {
            out << "    // TODO: implement Opcode 63 XO " << xo << "\n";
        }
    } else if (ppc_inst.opcode() == 21) {
        // rlwinm
        uint32_t rS = ppc_inst.rs();
        uint32_t rA = ppc_inst.ra();
        uint32_t sh = (ppc_inst.value() >> 11) & 0x1F;
        uint32_t mb = (ppc_inst.value() >> 6) & 0x1F;
        uint32_t me = (ppc_inst.value() >> 1) & 0x1F;
        
        uint32_t mask = 0;
        if (mb <= me) {
            for (int i = mb; i <= me; ++i) mask |= (1 << (31 - i));
        } else {
            for (int i = 0; i <= me; ++i) mask |= (1 << (31 - i));
            for (int i = mb; i <= 31; ++i) mask |= (1 << (31 - i));
        }
        
        out << "    ctx.gpr[" << rA << "] = std::rotl(ctx.gpr[" << rS << "], " << sh << ") & 0x" << std::hex << std::uppercase << mask << std::dec << "; // rlwinm r" << rA << ", r" << rS << ", " << sh << ", " << mb << ", " << me << "\n";
    } else if (ppc_inst.opcode() == 31) {
        uint32_t xo = ppc_inst.extended_opcode();
        uint32_t rD = ppc_inst.rd();
        uint32_t rA = ppc_inst.ra();
        uint32_t rB = ppc_inst.rb();
        uint32_t rS = ppc_inst.rs();
        
        if (xo == 87) {
            if (rA == 0) out << "    ctx.gpr[" << rD << "] = ctx.mmu.read8(ctx.gpr[" << rB << "]); // lbzx\n";
            else out << "    ctx.gpr[" << rD << "] = ctx.mmu.read8(ctx.gpr[" << rA << "] + ctx.gpr[" << rB << "]); // lbzx\n";
        } else if (xo == 215) {
            if (rA == 0) out << "    ctx.mmu.write8(ctx.gpr[" << rB << "], (uint8_t)ctx.gpr[" << rS << "]); // stbx\n";
            else out << "    ctx.mmu.write8(ctx.gpr[" << rA << "] + ctx.gpr[" << rB << "], (uint8_t)ctx.gpr[" << rS << "]); // stbx\n";
        } else if (xo == 279) {
            if (rA == 0) out << "    ctx.gpr[" << rD << "] = ctx.mmu.read16(ctx.gpr[" << rB << "]); // lhzx\n";
            else out << "    ctx.gpr[" << rD << "] = ctx.mmu.read16(ctx.gpr[" << rA << "] + ctx.gpr[" << rB << "]); // lhzx\n";
        } else if (xo == 407) {
            if (rA == 0) out << "    ctx.mmu.write16(ctx.gpr[" << rB << "], (uint16_t)ctx.gpr[" << rS << "]); // sthx\n";
            else out << "    ctx.mmu.write16(ctx.gpr[" << rA << "] + ctx.gpr[" << rB << "], (uint16_t)ctx.gpr[" << rS << "]); // sthx\n";
        } else if (xo == 23) {
            if (rA == 0) out << "    ctx.gpr[" << rD << "] = ctx.mmu.read32(ctx.gpr[" << rB << "]); // lwzx\n";
            else out << "    ctx.gpr[" << rD << "] = ctx.mmu.read32(ctx.gpr[" << rA << "] + ctx.gpr[" << rB << "]); // lwzx\n";
        } else if (xo == 151) {
            if (rA == 0) out << "    ctx.mmu.write32(ctx.gpr[" << rB << "], ctx.gpr[" << rS << "]); // stwx\n";
            else out << "    ctx.mmu.write32(ctx.gpr[" << rA << "] + ctx.gpr[" << rB << "], ctx.gpr[" << rS << "]); // stwx\n";
        } else if (xo == 40) {
            out << "    ctx.gpr[" << rD << "] = ctx.gpr[" << rB << "] - ctx.gpr[" << rA << "]; // subf\n";
        } else if (xo == 235) {
            out << "    ctx.gpr[" << rD << "] = ctx.gpr[" << rA << "] * ctx.gpr[" << rB << "]; // mullw\n";
        } else if (xo == 491) {
            out << "    if (ctx.gpr[" << rB << "] != 0) ctx.gpr[" << rD << "] = (uint32_t)((int32_t)ctx.gpr[" << rA << "] / (int32_t)ctx.gpr[" << rB << "]); // divw\n";
        } else if (xo == 459) {
            out << "    if (ctx.gpr[" << rB << "] != 0) ctx.gpr[" << rD << "] = ctx.gpr[" << rA << "] / ctx.gpr[" << rB << "]; // divwu\n";
        } else if (xo == 954) {
            out << "    ctx.gpr[" << rA << "] = (uint32_t)(int32_t)(int8_t)ctx.gpr[" << rS << "]; // extsb\n";
        } else if (xo == 922) {
            out << "    ctx.gpr[" << rA << "] = (uint32_t)(int32_t)(int16_t)ctx.gpr[" << rS << "]; // extsh\n";
        } else if (xo == 28) {
            out << "    ctx.gpr[" << rA << "] = ctx.gpr[" << rS << "] & ctx.gpr[" << rB << "]; // and\n";
        } else if (xo == 60) {
            out << "    ctx.gpr[" << rA << "] = ctx.gpr[" << rS << "] & ~ctx.gpr[" << rB << "]; // andc\n";
        } else if (xo == 316) {
            out << "    ctx.gpr[" << rA << "] = ctx.gpr[" << rS << "] ^ ctx.gpr[" << rB << "]; // xor\n";
        } else if (xo == 24) {
            out << "    ctx.gpr[" << rA << "] = ctx.gpr[" << rS << "] << (ctx.gpr[" << rB << "] & 0x1F); // slw\n";
        } else if (xo == 536) {
            out << "    ctx.gpr[" << rA << "] = ctx.gpr[" << rS << "] >> (ctx.gpr[" << rB << "] & 0x1F); // srw\n";
        } else if (xo == 792) {
            out << "    ctx.gpr[" << rA << "] = (uint32_t)((int32_t)ctx.gpr[" << rS << "] >> (ctx.gpr[" << rB << "] & 0x1F)); // sraw\n";
        } else {
            out << "    // TODO: implement opcode 31 xo " << xo << "\n";
        }
    } else {
        out << "    // TODO: implement opcode " << ppc_inst.opcode() << "\n";
    }
}

} // namespace recomp
} // namespace nwii
