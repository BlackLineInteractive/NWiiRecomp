#include "recompiler/recompiler.h"
#include "ppc/instruction.h"
#include <fstream>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <filesystem>

namespace nwii {
namespace recomp {

Recompiler::Recompiler(const analyzer::Analyzer& analyzer, const SymbolTable* symbols, const RecompilerConfig& config) 
    : analyzer_(analyzer), symbols_(symbols), config_(config) {}

static bool is_hle_function(const std::string& name) {
    const char* prefixes[] = {"OS", "GX", "DVD", "VI", "WPAD", "AX", "EXI", "PAD", "IOS", "mtx_", "vec_"};
    for (const char* prefix : prefixes) {
        if (name.starts_with(prefix)) {
            return true;
        }
    }
    return false;
}

std::vector<std::string> Recompiler::generate_cpp(uint32_t entry_point) {
    std::vector<std::string> generated_files;
    
    std::cout << "[Recompiler] Starting C++ generation. Total functions to emit: " << analyzer_.get_functions().size() << "\n";

    auto emit_headers = [](std::ostream& out) {
        out << "#include \"runtime/cpu_context.h\"\n";
        out << "#include <stdint.h>\n";
        out << "#include <bit>\n";
        out << "#include <cmath>\n";
        out << "#include <iostream>\n";
        out << "#include <cstdlib>\n\n";
        out << "using namespace nwii::runtime;\n\n";
    };

    if (config_.split_output) {
        // Generate functions.h
        std::string header_path = config_.output_dir + "/functions.h";
        std::ofstream hout(header_path);
        hout << "#pragma once\n";
        hout << "#include \"runtime/cpu_context.h\"\n\n";
        
        std::set<std::string> emitted_names;
        std::vector<std::string> all_func_names;
        
        for (const auto& [start_addr, func] : analyzer_.get_functions()) {
            std::string func_name;
            if (symbols_ && symbols_->has_symbol(start_addr)) {
                func_name = symbols_->get_symbol(start_addr);
            } else {
                std::stringstream ss;
                ss << "func_" << std::hex << std::uppercase << std::setfill('0') << std::setw(8) << start_addr;
                func_name = ss.str();
            }
            if (emitted_names.count(func_name)) {
                std::stringstream ss;
                ss << func_name << "_" << std::hex << std::uppercase << start_addr;
                func_name = ss.str();
            }
            emitted_names.insert(func_name);
            all_func_names.push_back(func_name);
            
            hout << "void " << func_name << "(nwii::runtime::CPUContext& ctx);\n";
        }
        hout.close();
        
        // Generate output_X.cpp files
        int file_idx = 0;
        int func_idx = 0;
        int count_in_current_file = 0;
        std::ofstream out;
        
        for (const auto& [start_addr, func] : analyzer_.get_functions()) {
            if (count_in_current_file == 0) {
                if (out.is_open()) {
                    out.close();
                    std::cout << "[Recompiler] Generated " << generated_files.back() << "\n";
                }
                std::stringstream ss;
                ss << "output_" << file_idx++ << ".cpp";
                std::string fname = ss.str();
                generated_files.push_back(fname);
                out.open(config_.output_dir + "/" + fname);
                emit_headers(out);
                out << "#include \"functions.h\"\n\n";
            }
            
            std::string func_name = all_func_names[func_idx++];
            
            if (symbols_ && symbols_->has_symbol(start_addr) && is_hle_function(symbols_->get_symbol(start_addr))) {
                out << "// [HLE Hook] Skipping generation for " << func_name << "\n\n";
            } else {
                emit_function(out, func, func_name);
            }
            
            count_in_current_file += func.instructions.size();
            if (count_in_current_file >= config_.instructions_per_file) {
                count_in_current_file = 0;
            }
        }
        if (out.is_open()) {
            out.close();
            std::cout << "[Recompiler] Generated " << generated_files.back() << "\n";
        }
        
        // Generate main_output.cpp
        std::string main_name = "main_output.cpp";
        generated_files.push_back(main_name);
        out.open(config_.output_dir + "/" + main_name);
        emit_headers(out);
        out << "#include \"functions.h\"\n\n";
        
        std::string entry_func;
        if (symbols_ && symbols_->has_symbol(entry_point)) {
            entry_func = symbols_->get_symbol(entry_point);
        } else {
            std::stringstream ss;
            ss << "func_" << std::hex << std::uppercase << std::setfill('0') << std::setw(8) << entry_point;
            entry_func = ss.str();
        }
        
        out << "// --- Entry Point Wrapper ---\n";
        out << "extern \"C\" void run_game(nwii::runtime::CPUContext& ctx) {\n";
        out << "    ctx.pc = 0x" << std::hex << std::uppercase << entry_point << std::dec << ";\n";
        out << "    while (ctx.pc != 0) {\n";
        out << "        if (ctx.pc < 0x80000000) ctx.pc |= 0x80000000;\n";
        out << "        uint32_t target = ctx.pc;\n        if ((++ctx.inst_count % 100000) == 0) std::cout << "Dispatcher PC: 0x" << std::hex << target << std::endl;\n";
        out << "        switch (target) {\n";
        
        std::set<uint32_t> emitted_cases;
        
        func_idx = 0;
        for (const auto& [start_addr, func] : analyzer_.get_functions()) {
            std::string func_name = all_func_names[func_idx++];
            bool is_hle = (symbols_ && symbols_->has_symbol(start_addr) && is_hle_function(symbols_->get_symbol(start_addr)));
            
            if (emitted_cases.insert(start_addr).second) {
                if (is_hle) {
                    out << "            case 0x" << std::hex << std::uppercase << start_addr << std::dec << ": " << func_name << "(ctx); ctx.pc = ctx.lr; break;\n";
                } else {
                    out << "            case 0x" << std::hex << std::uppercase << start_addr << std::dec << ": " << func_name << "(ctx); break;\n";
                }
            }
            
            if (!is_hle) {
                for (const auto& inst : func.instructions) {
                    ppc::Instruction ppc_inst(inst.opcode);
                    if (ppc_inst.is_branch_link() || ppc_inst.opcode() == 17) {
                        uint32_t ret_addr = inst.address + 4;
                        if (emitted_cases.insert(ret_addr).second) {
                            out << "            case 0x" << std::hex << std::uppercase << ret_addr << std::dec << ": " << func_name << "(ctx); break;\n";
                        }
                    }
                }
                for (uint32_t jump_target : func.jump_table_targets) {
                    if (emitted_cases.insert(jump_target).second) {
                        out << "            case 0x" << std::hex << std::uppercase << jump_target << std::dec << ": " << func_name << "(ctx); break;\n";
                    }
                }
            }
        }
        out << "            default: std::cerr << \"UNKNOWN DISPATCH TO 0x\" << std::hex << target << \"\\n\"; std::exit(1);\n";
        out << "        }\n";
        out << "    }\n";
        out << "}\n";
        out.close();
        std::cout << "[Recompiler] Generated " << main_name << "\n";
    } else {
        // Monolithic generation
        std::string fname = "output.cpp";
        generated_files.push_back(fname);
        std::ofstream out(config_.output_dir + "/" + fname);
        emit_headers(out);
        
        std::set<std::string> emitted_names;
        std::vector<std::string> all_func_names;
        
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
            if (emitted_names.count(func_name)) {
                std::stringstream ss;
                ss << func_name << "_" << std::hex << std::uppercase << start_addr;
                func_name = ss.str();
            }
            emitted_names.insert(func_name);
            all_func_names.push_back(func_name);
            out << "void " << func_name << "(CPUContext& ctx);\n";
        }
        
        out << "\n// --- Function Bodies ---\n";
        int func_idx = 0;
        for (const auto& [start_addr, func] : analyzer_.get_functions()) {
            std::string func_name = all_func_names[func_idx++];
            if (symbols_ && symbols_->has_symbol(start_addr) && is_hle_function(symbols_->get_symbol(start_addr))) {
                out << "// [HLE Hook] Skipping generation for " << func_name << "\n\n";
            } else {
                emit_function(out, func, func_name);
            }
        }
        
        std::string entry_func;
        if (symbols_ && symbols_->has_symbol(entry_point)) {
            entry_func = symbols_->get_symbol(entry_point);
        } else {
            std::stringstream ss;
            ss << "func_" << std::hex << std::uppercase << std::setfill('0') << std::setw(8) << entry_point;
            entry_func = ss.str();
        }
        
        out << "\n// --- Entry Point Wrapper ---\n";
        out << "extern \"C\" void run_game(nwii::runtime::CPUContext& ctx) {\n";
        out << "    ctx.pc = 0x" << std::hex << std::uppercase << entry_point << std::dec << ";\n";
        out << "    while (ctx.pc != 0) {\n";
        out << "        if (ctx.pc < 0x80000000) ctx.pc |= 0x80000000;\n";
        out << "        uint32_t target = ctx.pc;\n        if ((++ctx.inst_count % 100000) == 0) std::cout << "Dispatcher PC: 0x" << std::hex << target << std::endl;\n";
        out << "        switch (target) {\n";
        
        std::set<uint32_t> emitted_cases;
        
        func_idx = 0;
        for (const auto& [start_addr, func] : analyzer_.get_functions()) {
            std::string func_name = all_func_names[func_idx++];
            bool is_hle = (symbols_ && symbols_->has_symbol(start_addr) && is_hle_function(symbols_->get_symbol(start_addr)));
            
            if (emitted_cases.insert(start_addr).second) {
                if (is_hle) {
                    out << "            case 0x" << std::hex << std::uppercase << start_addr << std::dec << ": " << func_name << "(ctx); ctx.pc = ctx.lr; break;\n";
                } else {
                    out << "            case 0x" << std::hex << std::uppercase << start_addr << std::dec << ": " << func_name << "(ctx); break;\n";
                }
            }
            
            if (!is_hle) {
                for (const auto& inst : func.instructions) {
                    ppc::Instruction ppc_inst(inst.opcode);
                    if (ppc_inst.is_branch_link() || ppc_inst.opcode() == 17) {
                        uint32_t ret_addr = inst.address + 4;
                        if (emitted_cases.insert(ret_addr).second) {
                            out << "            case 0x" << std::hex << std::uppercase << ret_addr << std::dec << ": " << func_name << "(ctx); break;\n";
                        }
                    }
                }
                for (uint32_t jump_target : func.jump_table_targets) {
                    if (emitted_cases.insert(jump_target).second) {
                        out << "            case 0x" << std::hex << std::uppercase << jump_target << std::dec << ": " << func_name << "(ctx); break;\n";
                    }
                }
            }
        }
        out << "            default: std::cerr << \"UNKNOWN DISPATCH TO 0x\" << std::hex << target << \"\\n\"; std::exit(1);\n";
        out << "        }\n";
        out << "    }\n";
        out << "}\n";
    }

    return generated_files;
}

bool Recompiler::generate_cmake_project(uint32_t entry_point) {
    if (!std::filesystem::exists(config_.output_dir)) {
        std::filesystem::create_directories(config_.output_dir);
    }
    
    // 1. Generate output.cpp(s)
    std::vector<std::string> generated_files = generate_cpp(entry_point);
    if (generated_files.empty()) {
        return false;
    }
    
    // 2. Copy nWiiRuntime
    std::string runtime_dest = config_.output_dir + "/nWiiRuntime";
    try {
        std::filesystem::copy(config_.runtime_source_dir, runtime_dest, 
            std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing);
    } catch (const std::exception& e) {
        std::cerr << "Failed to copy runtime from " << config_.runtime_source_dir << ": " << e.what() << "\n";
        return false;
    }
    
    // 3. Generate CMakeLists.txt
    std::string cmake_path = config_.output_dir + "/CMakeLists.txt";
    std::ofstream out(cmake_path);
    if (!out.is_open()) return false;
    
    out << "cmake_minimum_required(VERSION 3.20)\n";
    out << "project(" << config_.project_name << " LANGUAGES CXX)\n\n";
    out << "set(CMAKE_POLICY_VERSION_MINIMUM 3.5)\n";
    out << "set(CMAKE_CXX_STANDARD 20)\n";
    out << "set(CMAKE_CXX_STANDARD_REQUIRED ON)\n\n";
    out << "include(FetchContent)\n";
    out << "FetchContent_Declare(\n";
    out << "    raylib\n";
    out << "    URL https://github.com/raysan5/raylib/archive/refs/tags/5.0.tar.gz\n";
    out << ")\n";
    out << "FetchContent_MakeAvailable(raylib)\n\n";
    
    out << "add_subdirectory(nWiiRuntime)\n\n";
    
    out << "add_executable(" << config_.project_name << "";
    for (const auto& file : generated_files) {
        out << " " << file;
    }
    out << ")\n";
    
    out << "target_link_libraries(" << config_.project_name << " PRIVATE nwiiruntime raylib)\n";
    
    return true;
}

std::string Recompiler::generate_function_cpp(const analyzer::Function& func) {
    std::stringstream ss;
    
    std::string func_name;
    if (symbols_ && symbols_->has_symbol(func.start_address)) {
        func_name = symbols_->get_symbol(func.start_address);
    } else {
        std::stringstream ss_name;
        ss_name << "func_" << std::hex << std::uppercase << std::setfill('0') << std::setw(8) << func.start_address;
        func_name = ss_name.str();
    }
    
    emit_function(ss, func, func_name);
    return ss.str();
}

void Recompiler::emit_function(std::ostream& out, const analyzer::Function& func, const std::string& func_name) {
    out << "void " << func_name << "(CPUContext& ctx) {\n";

    // Map all instruction addresses in this function
    std::set<uint32_t> valid_addrs;
    for (const auto& inst : func.instructions) {
        valid_addrs.insert(inst.address);
    }

    // Mid-function entry point switch
    if (valid_addrs.size() > 1) {
        out << "    if (ctx.pc != 0x" << std::hex << std::uppercase << func.start_address << std::dec << ") {\n";
        out << "        switch (ctx.pc) {\n";
        for (uint32_t addr : valid_addrs) {
            if (addr != func.start_address) {
                out << "            case 0x" << std::hex << std::uppercase << addr << std::dec << ": goto loc_" << std::hex << std::uppercase << addr << std::dec << ";\n";
            }
        }
        out << "            default: std::cerr << \"UNKNOWN MID-FUNCTION ENTRY TO 0x\" << std::hex << ctx.pc << \"\\n\"; std::exit(1);\n";
        out << "        }\n";
        out << "    }\n";
    }
    for (const auto& inst : func.instructions) {
        emit_instruction(out, inst, func);
    }

    out << "}\n\n";
}

void Recompiler::emit_instruction(std::ostream& out, const analyzer::Instruction& inst, const analyzer::Function& func) {
    ppc::Instruction ppc_inst(inst.opcode);
    
    out << "loc_" << std::hex << std::uppercase << inst.address << std::dec << ": ;\n";
    out << "    // 0x" << std::hex << std::setfill('0') << std::setw(8) << inst.address << ": ";
    out << std::setfill('0') << std::setw(8) << inst.opcode << std::dec << "\n";

    if (ppc_inst.opcode() == 14) {
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
        } else if (spr == 9) {
            out << "    ctx.gpr[" << rD << "] = ctx.ctr; // mfctr r" << rD << "\n";
        } else if (spr == 26) {
            out << "    ctx.gpr[" << rD << "] = ctx.srr0; // mfsrr0 r" << rD << "\n";
        } else if (spr == 27) {
            out << "    ctx.gpr[" << rD << "] = ctx.srr1; // mfsrr1 r" << rD << "\n";
        } else if (spr >= 912 && spr <= 919) {
            out << "    ctx.gpr[" << rD << "] = ctx.gqr[" << (spr - 912) << "]; // mfgqr" << (spr - 912) << " r" << rD << "\n";
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
        } else if (spr == 9) {
            out << "    ctx.ctr = ctx.gpr[" << rS << "]; // mtctr r" << rS << "\n";
        } else if (spr == 26) {
            out << "    ctx.srr0 = ctx.gpr[" << rS << "]; // mtsrr0 r" << rS << "\n";
        } else if (spr == 27) {
            out << "    ctx.srr1 = ctx.gpr[" << rS << "]; // mtsrr1 r" << rS << "\n";
        } else if (spr >= 912 && spr <= 919) {
            out << "    ctx.gqr[" << (spr - 912) << "] = ctx.gpr[" << rS << "]; // mtgqr" << (spr - 912) << " r" << rS << "\n";
        } else {
            out << "    // TODO: mtspr " << spr << ", r" << rS << "\n";
        }
    } else if (ppc_inst.opcode() == 31 && ppc_inst.extended_opcode() == 0) {
        // CMPW crD, rA, rB
        uint32_t crD = ppc_inst.rd() >> 2; // crfD is top 3 bits of rD field
        uint32_t rA = ppc_inst.ra();
        uint32_t rB = ppc_inst.rb();
        
        out << "    ctx.cr[" << crD << "].lt = ((int32_t)ctx.gpr[" << rA << "] < (int32_t)ctx.gpr[" << rB << "]);\n";
        out << "    ctx.cr[" << crD << "].gt = ((int32_t)ctx.gpr[" << rA << "] > (int32_t)ctx.gpr[" << rB << "]);\n";
        out << "    ctx.cr[" << crD << "].eq = ((int32_t)ctx.gpr[" << rA << "] == (int32_t)ctx.gpr[" << rB << "]);\n";
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
    } else if (ppc_inst.opcode() == 33) {
        // LWZU: rD = read32(rA + SIMM); rA = rA + SIMM
        uint32_t rD = ppc_inst.rd();
        uint32_t rA = ppc_inst.ra();
        int16_t simm = ppc_inst.simm();
        out << "    ctx.gpr[" << rA << "] = ctx.gpr[" << rA << "] + " << simm << ";\n";
        out << "    ctx.gpr[" << rD << "] = ctx.mmu.read32(ctx.gpr[" << rA << "]); // lwzu r" << rD << ", " << simm << "(r" << rA << ")\n";
    } else if (ppc_inst.opcode() == 34) {
        // LBZ: rD = read8(rA|0 + SIMM)
        uint32_t rD = ppc_inst.rd();
        uint32_t rA = ppc_inst.ra();
        int16_t simm = ppc_inst.simm();
        if (rA == 0) {
            out << "    ctx.gpr[" << rD << "] = ctx.mmu.read8(" << simm << "); // lbz r" << rD << ", " << simm << "(0)\n";
        } else {
            out << "    ctx.gpr[" << rD << "] = ctx.mmu.read8(ctx.gpr[" << rA << "] + " << simm << "); // lbz r" << rD << ", " << simm << "(r" << rA << ")\n";
        }
    } else if (ppc_inst.opcode() == 35) {
        // LBZU: rA = rA + SIMM; rD = read8(rA)
        uint32_t rD = ppc_inst.rd();
        uint32_t rA = ppc_inst.ra();
        int16_t simm = ppc_inst.simm();
        out << "    ctx.gpr[" << rA << "] = ctx.gpr[" << rA << "] + " << simm << ";\n";
        out << "    ctx.gpr[" << rD << "] = ctx.mmu.read8(ctx.gpr[" << rA << "]); // lbzu r" << rD << ", " << simm << "(r" << rA << ")\n";
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
    } else if (ppc_inst.opcode() == 38) {
        // STB: write8(rA|0 + SIMM, rS)
        uint32_t rS = ppc_inst.rs();
        uint32_t rA = ppc_inst.ra();
        int16_t simm = ppc_inst.simm();
        if (rA == 0) {
            out << "    ctx.mmu.write8(" << simm << ", (uint8_t)ctx.gpr[" << rS << "]); // stb r" << rS << ", " << simm << "(0)\n";
        } else {
            out << "    ctx.mmu.write8(ctx.gpr[" << rA << "] + " << simm << ", (uint8_t)ctx.gpr[" << rS << "]); // stb r" << rS << ", " << simm << "(r" << rA << ")\n";
        }
    } else if (ppc_inst.opcode() == 39) {
        // STBU: rA = rA + SIMM; write8(rA, rS)
        uint32_t rS = ppc_inst.rs();
        uint32_t rA = ppc_inst.ra();
        int16_t simm = ppc_inst.simm();
        out << "    ctx.gpr[" << rA << "] = ctx.gpr[" << rA << "] + " << simm << ";\n";
        out << "    ctx.mmu.write8(ctx.gpr[" << rA << "], (uint8_t)ctx.gpr[" << rS << "]); // stbu r" << rS << ", " << simm << "(r" << rA << ")\n";
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

        bool is_local = std::find_if(func.instructions.begin(), func.instructions.end(),
                                     [target](const auto& i) { return i.address == target; }) != func.instructions.end();

        if (target <= inst.address && is_local) {
            out << "    ++ctx.inst_count;\n"; out << "    if ((ctx.inst_count % 10000000) == 0) { std::cout << \"Spinning at PC: 0x\" << std::hex << 0x" << std::uppercase << std::hex << inst.address << std::dec << " << std::endl; }\n";
        }

        std::string ext_name;
        if (!is_local) {
            if (symbols_ && symbols_->has_symbol(target)) {
                ext_name = symbols_->get_symbol(target); if (ext_name.find("loc_") == 0) ext_name.replace(0, 4, "func_");
            } else {
                std::stringstream ss_ext;
                ss_ext << "func_" << std::hex << std::uppercase << std::setfill('0') << std::setw(8) << target;
                ext_name = ss_ext.str();
            }
        }

        bool bit0 = (bo & 0x10) != 0;
        bool bit1 = (bo & 0x08) != 0;
        bool bit2 = (bo & 0x04) != 0;
        bool bit3 = (bo & 0x02) != 0;

        bool dec_ctr = !bit2;
        bool test_ctr = !bit2;
        bool test_cr = !bit0;

        std::vector<std::string> conditions;
        if (test_ctr) {
            if (bit3) conditions.push_back("ctx.ctr == 0");
            else conditions.push_back("ctx.ctr != 0");
        }
        if (test_cr) {
            if (bit1) conditions.push_back("ctx.cr[" + std::to_string(cr_idx) + "]." + cond_field);
            else conditions.push_back("!ctx.cr[" + std::to_string(cr_idx) + "]." + cond_field);
        }

        std::string cond_expr;
        if (conditions.empty()) cond_expr = "true";
        else if (conditions.size() == 1) cond_expr = conditions[0];
        else cond_expr = conditions[0] + " && " + conditions[1];

        if (dec_ctr) out << "    ctx.ctr--;\n";
        
        if (ppc_inst.lk()) out << "    ctx.lr = 0x" << std::hex << std::uppercase << (inst.address + 4) << std::dec << "; // save return address\n";

        if (is_local) {
            if (cond_expr == "true") out << "    goto " << target_lbl << ";\n";
            else out << "    if (" << cond_expr << ") goto " << target_lbl << ";\n";
        } else {
            bool is_mapped = true;
            if (!symbols_ || !symbols_->has_symbol(target)) {
                if (analyzer_.get_functions().find(target) == analyzer_.get_functions().end()) {
                    is_mapped = false;
                }
            }
            if (is_mapped) {
                if (cond_expr == "true") {
                    out << "    ctx.pc = 0x" << std::hex << std::uppercase << target << std::dec << "; " << ext_name << "(ctx); return;\n";
                } else {
                    out << "    if (" << cond_expr << ") { ctx.pc = 0x" << std::hex << std::uppercase << target << std::dec << "; " << ext_name << "(ctx); return; }\n";
                }
            } else {
                if (cond_expr == "true") {
                    out << "    ctx.pc = 0x" << std::hex << target << std::dec << "; return;\n";
                } else {
                    out << "    if (" << cond_expr << ") { ctx.pc = 0x" << std::hex << target << std::dec << "; return; }\n";
                }
            }
        }
    } else if (ppc_inst.opcode() == 18) {
        // b, bl, ba, bla
        uint32_t target = ppc_inst.branch_target(inst.address);
        std::stringstream ss;
        std::string target_name;
        
        bool is_local = std::find_if(func.instructions.begin(), func.instructions.end(),
                                     [target](const auto& i) { return i.address == target; }) != func.instructions.end();

        if (target <= inst.address && is_local) {
            out << "    ++ctx.inst_count;\n"; out << "    if ((ctx.inst_count % 10000000) == 0) { std::cout << \"Spinning at PC: 0x\" << std::hex << 0x" << std::uppercase << std::hex << inst.address << std::dec << " << std::endl; }\n";
        }
        
        if (symbols_ && symbols_->has_symbol(target)) {
            target_name = symbols_->get_symbol(target); if (target_name.find("loc_") == 0) target_name.replace(0, 4, "func_");
        } else {
            std::stringstream ss_name;
            ss_name << "func_" << std::hex << std::uppercase << std::setfill('0') << std::setw(8) << target;
            target_name = ss_name.str();
        }

        if (ppc_inst.is_branch_link()) {
            out << "    ctx.lr = 0x" << std::hex << std::uppercase << (inst.address + 4) << std::dec << "; // save return address\n";
            if (is_local) {
                out << "    goto loc_" << std::hex << std::uppercase << std::setfill('0') << std::setw(8) << target << ";\n";
            } else {
                bool is_mapped = true;
                if (!symbols_ || !symbols_->has_symbol(target)) {
                    if (analyzer_.get_functions().find(target) == analyzer_.get_functions().end()) {
                        is_mapped = false;
                    }
                }
                if (is_mapped) {
                    bool target_is_hle = (symbols_ && symbols_->has_symbol(target) && is_hle_function(symbols_->get_symbol(target)));
                    out << "    ctx.pc = 0x" << std::hex << std::uppercase << target << std::dec << "; " << target_name << "(ctx);\n";
                    if (target_is_hle) out << "    ctx.pc = ctx.lr;\n";
                    out << "    if (ctx.pc != 0x" << std::hex << std::uppercase << (inst.address + 4) << std::dec << ") return;\n";
                } else {
                    out << "    ctx.pc = 0x" << std::hex << target << std::dec << "; return; // bl to unmapped target\n";
                }
            }
        } else {
            if (is_local) {
                out << "    goto loc_" << std::hex << std::uppercase << std::setfill('0') << std::setw(8) << target << ";\n";
            } else {
                bool is_mapped = true;
                if (!symbols_ || !symbols_->has_symbol(target)) {
                    if (analyzer_.get_functions().find(target) == analyzer_.get_functions().end()) {
                        is_mapped = false;
                    }
                }
                if (is_mapped) {
                    out << "    ctx.pc = 0x" << std::hex << std::uppercase << target << std::dec << "; " << target_name << "(ctx); return;\n";
                } else {
                    out << "    ctx.pc = 0x" << std::hex << target << std::dec << "; return;\n";
                }
            }
        }
    } else if (ppc_inst.opcode() == 19) {
        uint32_t xo = ppc_inst.extended_opcode();
        if (xo == 150) { // isync
            out << "    // isync\n";
        } else if (xo == 50) { // rfi
            out << "    ctx.pc = ctx.srr0; return; // rfi\n";
        } else if (xo == 16 || xo == 528) { // BCLR (16) and BCCTR (528)
            uint32_t bo = ppc_inst.bo();
            uint32_t bi = ppc_inst.bi();
            uint32_t cr_idx = bi / 4;
            uint32_t cr_bit = bi % 4;
            
            std::string cond_field;
            if (cr_bit == 0) cond_field = "lt";
            else if (cr_bit == 1) cond_field = "gt";
            else if (cr_bit == 2) cond_field = "eq";
            else cond_field = "so";
            
            std::string target_reg = (xo == 16) ? "ctx.lr" : "ctx.ctr";
            
            bool bit0 = (bo & 0x10) != 0;
            bool bit1 = (bo & 0x08) != 0;
            bool bit2 = (bo & 0x04) != 0;
            bool bit3 = (bo & 0x02) != 0;

            bool dec_ctr = !bit2;
            bool test_ctr = !bit2;
            bool test_cr = !bit0;

            std::vector<std::string> conditions;
            if (test_ctr) {
                if (bit3) conditions.push_back("ctx.ctr == 0");
                else conditions.push_back("ctx.ctr != 0");
            }
            if (test_cr) {
                if (bit1) conditions.push_back("ctx.cr[" + std::to_string(cr_idx) + "]." + cond_field);
                else conditions.push_back("!ctx.cr[" + std::to_string(cr_idx) + "]." + cond_field);
            }

            std::string cond_expr;
            if (conditions.empty()) cond_expr = "true";
            else if (conditions.size() == 1) cond_expr = conditions[0];
            else cond_expr = conditions[0] + " && " + conditions[1];

            if (dec_ctr) out << "    ctx.ctr--;\n";

            if (ppc_inst.lk()) out << "    ctx.lr = 0x" << std::hex << std::uppercase << (inst.address + 4) << std::dec << "; // save return address\n";

            if (cond_expr == "true") {
                out << "    ctx.pc = " << target_reg << "; return;\n";
            } else {
                out << "    if (" << cond_expr << ") { ctx.pc = " << target_reg << "; return; }\n";
            }
        } else if (xo == 257 || xo == 129 || xo == 289 || xo == 225 || xo == 33 || xo == 449 || xo == 417 || xo == 193) {
            uint32_t crbD = ppc_inst.rd();
            uint32_t crbA = ppc_inst.ra();
            uint32_t crbB = ppc_inst.rb();
            
            auto get_cr_bit_str = [](uint32_t bit_idx) {
                std::string field = "ctx.cr[" + std::to_string(bit_idx / 4) + "]";
                uint32_t bit = bit_idx % 4;
                if (bit == 0) return field + ".lt";
                if (bit == 1) return field + ".gt";
                if (bit == 2) return field + ".eq";
                return field + ".so";
            };
            
            std::string op;
            if (xo == 257) op = get_cr_bit_str(crbA) + " & " + get_cr_bit_str(crbB); // crand
            else if (xo == 129) op = get_cr_bit_str(crbA) + " & !" + get_cr_bit_str(crbB); // crandc
            else if (xo == 289) op = get_cr_bit_str(crbA) + " == " + get_cr_bit_str(crbB); // creqv
            else if (xo == 225) op = "!(" + get_cr_bit_str(crbA) + " & " + get_cr_bit_str(crbB) + ")"; // crnand
            else if (xo == 33) op = "!(" + get_cr_bit_str(crbA) + " | " + get_cr_bit_str(crbB) + ")"; // crnor
            else if (xo == 449) op = get_cr_bit_str(crbA) + " | " + get_cr_bit_str(crbB); // cror
            else if (xo == 417) op = get_cr_bit_str(crbA) + " | !" + get_cr_bit_str(crbB); // crorc
            else if (xo == 193) op = get_cr_bit_str(crbA) + " ^ " + get_cr_bit_str(crbB); // crxor
            
            out << "    " << get_cr_bit_str(crbD) << " = " << op << ";\n";
        } else {
            out << "    std::cerr << \"UNIMPLEMENTED OPCODE 19 XO \" << " << xo << " << \" at 0x" << std::hex << inst.address << std::dec << "\\n\"; std::exit(1);\n";
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
        } else if (xo == 18) {
            out << "    ctx.fpr[" << fD << "] = (float)(ctx.fpr[" << fA << "] / ctx.fpr[" << fB << "]); // fdivs f" << fD << ", f" << fA << ", f" << fB << "\n";
        } else {
            out << "    // TODO: implement Opcode 59 XO " << xo << "\n";
        }
    } else if (ppc_inst.opcode() == 63) {
        uint32_t xo = (ppc_inst.value() >> 1) & 0x3FF;
        uint32_t fD = ppc_inst.rd();
        uint32_t fB = ppc_inst.rb();
        uint32_t fA = ppc_inst.ra();
        if (xo == 72) {
            out << "    ctx.fpr[" << fD << "] = ctx.fpr[" << fB << "]; // fmr f" << fD << ", f" << fB << "\n";
        } else if (xo == 0 || xo == 32) { // fcmpu or fcmpo
            uint32_t crfD = fD >> 2;
            out << "    ctx.cr[" << crfD << "].lt = (ctx.fpr[" << fA << "] < ctx.fpr[" << fB << "]);\n";
            out << "    ctx.cr[" << crfD << "].gt = (ctx.fpr[" << fA << "] > ctx.fpr[" << fB << "]);\n";
            out << "    ctx.cr[" << crfD << "].eq = (ctx.fpr[" << fA << "] == ctx.fpr[" << fB << "]);\n";
            out << "    ctx.cr[" << crfD << "].so = std::isnan(ctx.fpr[" << fA << "]) || std::isnan(ctx.fpr[" << fB << "]);\n";
        } else {
            out << "    // TODO: implement Opcode 63 XO " << xo << "\n";
        }
    } else if (ppc_inst.opcode() == 20) {
        // rlwimi
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
        
        out << "    ctx.gpr[" << rA << "] = (ctx.gpr[" << rA << "] & ~0x" << std::hex << std::uppercase << mask << std::dec << ") | (std::rotl(ctx.gpr[" << rS << "], " << sh << ") & 0x" << std::hex << std::uppercase << mask << std::dec << "); // rlwimi r" << rA << ", r" << rS << ", " << sh << ", " << mb << ", " << me << "\n";
        if (ppc_inst.value() & 1) {
            out << "    ctx.cr[0].lt = ((int32_t)ctx.gpr[" << rA << "] < 0);\n";
            out << "    ctx.cr[0].gt = ((int32_t)ctx.gpr[" << rA << "] > 0);\n";
            out << "    ctx.cr[0].eq = (ctx.gpr[" << rA << "] == 0);\n";
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
        if (ppc_inst.value() & 1) {
            out << "    ctx.cr[0].lt = ((int32_t)ctx.gpr[" << rA << "] < 0);\n";
            out << "    ctx.cr[0].gt = ((int32_t)ctx.gpr[" << rA << "] > 0);\n";
            out << "    ctx.cr[0].eq = (ctx.gpr[" << rA << "] == 0);\n";
        }
    } else if (ppc_inst.opcode() == 31) {
        uint32_t xo = ppc_inst.extended_opcode();
        uint32_t rD = ppc_inst.rd();
        uint32_t rA = ppc_inst.ra();
        uint32_t rB = ppc_inst.rb();
        uint32_t rS = ppc_inst.rs();
        
        if (xo == 598 || xo == 854 || xo == 982 || xo == 54 || xo == 86 || xo == 278 || xo == 246) {
            // sync, eieio, icbi, dcbst, dcbf, dcbt, dcbtst
            out << "    // sync/cache instruction\n";
        } else if (xo == 0) { // cmpw
            uint32_t crfD = rD >> 2;
            out << "    ctx.cr[" << crfD << "].lt = ((int32_t)ctx.gpr[" << rA << "] < (int32_t)ctx.gpr[" << rB << "]);\n";
            out << "    ctx.cr[" << crfD << "].gt = ((int32_t)ctx.gpr[" << rA << "] > (int32_t)ctx.gpr[" << rB << "]);\n";
            out << "    ctx.cr[" << crfD << "].eq = (ctx.gpr[" << rA << "] == ctx.gpr[" << rB << "]);\n";
            out << "    ctx.cr[" << crfD << "].so = false;\n"; // TODO: xer_so
        } else if (xo == 32) { // cmplw
            uint32_t crfD = rD >> 2;
            out << "    ctx.cr[" << crfD << "].lt = (ctx.gpr[" << rA << "] < ctx.gpr[" << rB << "]);\n";
            out << "    ctx.cr[" << crfD << "].gt = (ctx.gpr[" << rA << "] > ctx.gpr[" << rB << "]);\n";
            out << "    ctx.cr[" << crfD << "].eq = (ctx.gpr[" << rA << "] == ctx.gpr[" << rB << "]);\n";
            out << "    ctx.cr[" << crfD << "].so = false;\n"; // TODO: xer_so
        } else if (xo == 11) { // mulhwu
            out << "    ctx.gpr[" << rD << "] = (uint32_t)(((uint64_t)ctx.gpr[" << rA << "] * (uint64_t)ctx.gpr[" << rB << "]) >> 32); // mulhwu\n";
            if (ppc_inst.value() & 1) { // Rc
                out << "    ctx.cr[0].lt = ((int32_t)ctx.gpr[" << rD << "] < 0);\n";
                out << "    ctx.cr[0].gt = ((int32_t)ctx.gpr[" << rD << "] > 0);\n";
                out << "    ctx.cr[0].eq = (ctx.gpr[" << rD << "] == 0);\n";
            }
        } else if (xo == 75) { // mulhw
            out << "    ctx.gpr[" << rD << "] = (uint32_t)(((int64_t)(int32_t)ctx.gpr[" << rA << "] * (int64_t)(int32_t)ctx.gpr[" << rB << "]) >> 32); // mulhw\n";
            if (ppc_inst.value() & 1) { // Rc
                out << "    ctx.cr[0].lt = ((int32_t)ctx.gpr[" << rD << "] < 0);\n";
                out << "    ctx.cr[0].gt = ((int32_t)ctx.gpr[" << rD << "] > 0);\n";
                out << "    ctx.cr[0].eq = (ctx.gpr[" << rD << "] == 0);\n";
            }
        } else if (xo == 87) {
            if (rA == 0) out << "    ctx.gpr[" << rD << "] = ctx.mmu.read8(ctx.gpr[" << rB << "]); // lbzx\n";
            else out << "    ctx.gpr[" << rD << "] = ctx.mmu.read8(ctx.gpr[" << rA << "] + ctx.gpr[" << rB << "]); // lbzx\n";
        } else if (xo == 215) {
            if (rA == 0) out << "    ctx.mmu.write8(ctx.gpr[" << rB << "], (uint8_t)ctx.gpr[" << rS << "]); // stbx\n";
            else out << "    ctx.mmu.write8(ctx.gpr[" << rA << "] + ctx.gpr[" << rB << "], (uint8_t)ctx.gpr[" << rS << "]); // stbx\n";
        } else if (xo == 279) {
            if (rA == 0) out << "    ctx.gpr[" << rD << "] = ctx.mmu.read16(ctx.gpr[" << rB << "]); // lhzx\n";
            else out << "    ctx.gpr[" << rD << "] = ctx.mmu.read16(ctx.gpr[" << rA << "] + ctx.gpr[" << rB << "]); // lhzx\n";
        } else if (xo == 371) { // mftb
            out << "    ctx.gpr[" << rD << "] = (uint32_t)(ctx.inst_count & 0xFFFFFFFF); // mftb\n";
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
            if (ppc_inst.value() & 1) { out << "    ctx.cr[0].lt = ((int32_t)ctx.gpr[" << rD << "] < 0); ctx.cr[0].gt = ((int32_t)ctx.gpr[" << rD << "] > 0); ctx.cr[0].eq = (ctx.gpr[" << rD << "] == 0);\n"; }
        } else if (xo == 235) {
            out << "    ctx.gpr[" << rD << "] = ctx.gpr[" << rA << "] * ctx.gpr[" << rB << "]; // mullw\n";
            if (ppc_inst.value() & 1) { out << "    ctx.cr[0].lt = ((int32_t)ctx.gpr[" << rD << "] < 0); ctx.cr[0].gt = ((int32_t)ctx.gpr[" << rD << "] > 0); ctx.cr[0].eq = (ctx.gpr[" << rD << "] == 0);\n"; }
        } else if (xo == 491) {
            out << "    if (ctx.gpr[" << rB << "] != 0) ctx.gpr[" << rD << "] = (uint32_t)((int32_t)ctx.gpr[" << rA << "] / (int32_t)ctx.gpr[" << rB << "]); // divw\n";
            if (ppc_inst.value() & 1) { out << "    ctx.cr[0].lt = ((int32_t)ctx.gpr[" << rD << "] < 0); ctx.cr[0].gt = ((int32_t)ctx.gpr[" << rD << "] > 0); ctx.cr[0].eq = (ctx.gpr[" << rD << "] == 0);\n"; }
        } else if (xo == 459) {
            out << "    if (ctx.gpr[" << rB << "] != 0) ctx.gpr[" << rD << "] = ctx.gpr[" << rA << "] / ctx.gpr[" << rB << "]; // divwu\n";
            if (ppc_inst.value() & 1) { out << "    ctx.cr[0].lt = ((int32_t)ctx.gpr[" << rD << "] < 0); ctx.cr[0].gt = ((int32_t)ctx.gpr[" << rD << "] > 0); ctx.cr[0].eq = (ctx.gpr[" << rD << "] == 0);\n"; }
        } else if (xo == 954) {
            out << "    ctx.gpr[" << rA << "] = (uint32_t)(int32_t)(int8_t)ctx.gpr[" << rS << "]; // extsb\n";
            if (ppc_inst.value() & 1) { out << "    ctx.cr[0].lt = ((int32_t)ctx.gpr[" << rA << "] < 0); ctx.cr[0].gt = ((int32_t)ctx.gpr[" << rA << "] > 0); ctx.cr[0].eq = (ctx.gpr[" << rA << "] == 0);\n"; }
        } else if (xo == 922) {
            out << "    ctx.gpr[" << rA << "] = (uint32_t)(int32_t)(int16_t)ctx.gpr[" << rS << "]; // extsh\n";
            if (ppc_inst.value() & 1) { out << "    ctx.cr[0].lt = ((int32_t)ctx.gpr[" << rA << "] < 0); ctx.cr[0].gt = ((int32_t)ctx.gpr[" << rA << "] > 0); ctx.cr[0].eq = (ctx.gpr[" << rA << "] == 0);\n"; }
        } else if (xo == 28) {
            out << "    ctx.gpr[" << rA << "] = ctx.gpr[" << rS << "] & ctx.gpr[" << rB << "]; // and\n";
            if (ppc_inst.value() & 1) { out << "    ctx.cr[0].lt = ((int32_t)ctx.gpr[" << rA << "] < 0); ctx.cr[0].gt = ((int32_t)ctx.gpr[" << rA << "] > 0); ctx.cr[0].eq = (ctx.gpr[" << rA << "] == 0);\n"; }
        } else if (xo == 60) {
            out << "    ctx.gpr[" << rA << "] = ctx.gpr[" << rS << "] & ~ctx.gpr[" << rB << "]; // andc\n";
            if (ppc_inst.value() & 1) { out << "    ctx.cr[0].lt = ((int32_t)ctx.gpr[" << rA << "] < 0); ctx.cr[0].gt = ((int32_t)ctx.gpr[" << rA << "] > 0); ctx.cr[0].eq = (ctx.gpr[" << rA << "] == 0);\n"; }
        } else if (xo == 316) {
            out << "    ctx.gpr[" << rA << "] = ctx.gpr[" << rS << "] ^ ctx.gpr[" << rB << "]; // xor\n";
            if (ppc_inst.value() & 1) { out << "    ctx.cr[0].lt = ((int32_t)ctx.gpr[" << rA << "] < 0); ctx.cr[0].gt = ((int32_t)ctx.gpr[" << rA << "] > 0); ctx.cr[0].eq = (ctx.gpr[" << rA << "] == 0);\n"; }
        } else if (xo == 24) {
            out << "    ctx.gpr[" << rA << "] = ctx.gpr[" << rS << "] << (ctx.gpr[" << rB << "] & 0x1F); // slw\n";
            if (ppc_inst.value() & 1) { out << "    ctx.cr[0].lt = ((int32_t)ctx.gpr[" << rA << "] < 0); ctx.cr[0].gt = ((int32_t)ctx.gpr[" << rA << "] > 0); ctx.cr[0].eq = (ctx.gpr[" << rA << "] == 0);\n"; }
        } else if (xo == 536) {
            out << "    ctx.gpr[" << rA << "] = ctx.gpr[" << rS << "] >> (ctx.gpr[" << rB << "] & 0x1F); // srw\n";
            if (ppc_inst.value() & 1) { out << "    ctx.cr[0].lt = ((int32_t)ctx.gpr[" << rA << "] < 0); ctx.cr[0].gt = ((int32_t)ctx.gpr[" << rA << "] > 0); ctx.cr[0].eq = (ctx.gpr[" << rA << "] == 0);\n"; }
        } else if (xo == 792) {
            out << "    ctx.gpr[" << rA << "] = (uint32_t)((int32_t)ctx.gpr[" << rS << "] >> (ctx.gpr[" << rB << "] & 0x1F)); // sraw\n";
            if (ppc_inst.value() & 1) { out << "    ctx.cr[0].lt = ((int32_t)ctx.gpr[" << rA << "] < 0); ctx.cr[0].gt = ((int32_t)ctx.gpr[" << rA << "] > 0); ctx.cr[0].eq = (ctx.gpr[" << rA << "] == 0);\n"; }
        } else if (xo == 26) {
            out << "    ctx.gpr[" << rA << "] = ctx.gpr[" << rS << "] == 0 ? 32 : std::countl_zero(ctx.gpr[" << rS << "]); // cntlzw\n";
            if (ppc_inst.value() & 1) { // Rc bit
                out << "    ctx.cr[0].lt = ((int32_t)ctx.gpr[" << rA << "] < 0);\n";
                out << "    ctx.cr[0].gt = ((int32_t)ctx.gpr[" << rA << "] > 0);\n";
                out << "    ctx.cr[0].eq = (ctx.gpr[" << rA << "] == 0);\n";
            }
        } else {
            out << "    // TODO: implement opcode 31 xo " << xo << "\n";
        }
    } else if (ppc_inst.opcode() == 24) { // ori
        out << "    ctx.gpr[" << ppc_inst.ra() << "] = ctx.gpr[" << ppc_inst.rs() << "] | " << ppc_inst.uimm() << "; // ori\n";
    } else if (ppc_inst.opcode() == 25) { // oris
        out << "    ctx.gpr[" << ppc_inst.ra() << "] = ctx.gpr[" << ppc_inst.rs() << "] | " << (ppc_inst.uimm() << 16) << "; // oris\n";
    } else if (ppc_inst.opcode() == 26) { // xori
        out << "    ctx.gpr[" << ppc_inst.ra() << "] = ctx.gpr[" << ppc_inst.rs() << "] ^ " << ppc_inst.uimm() << "; // xori\n";
    } else if (ppc_inst.opcode() == 27) { // xoris
        out << "    ctx.gpr[" << ppc_inst.ra() << "] = ctx.gpr[" << ppc_inst.rs() << "] ^ " << (ppc_inst.uimm() << 16) << "; // xoris\n";
    } else if (ppc_inst.opcode() == 28) { // andi.
        uint32_t rA = ppc_inst.ra();
        out << "    ctx.gpr[" << rA << "] = ctx.gpr[" << ppc_inst.rs() << "] & " << ppc_inst.uimm() << "; // andi.\n";
        out << "    ctx.cr[0].lt = ((int32_t)ctx.gpr[" << rA << "] < 0);\n";
        out << "    ctx.cr[0].gt = ((int32_t)ctx.gpr[" << rA << "] > 0);\n";
        out << "    ctx.cr[0].eq = (ctx.gpr[" << rA << "] == 0);\n";
    } else if (ppc_inst.opcode() == 29) { // andis.
        out << "    ctx.gpr[" << ppc_inst.ra() << "] = ctx.gpr[" << ppc_inst.rs() << "] & " << (ppc_inst.uimm() << 16) << "; // andis.\n";
    } else if (ppc_inst.opcode() == 7) { // mulli
        out << "    ctx.gpr[" << ppc_inst.rd() << "] = (uint32_t)((int32_t)ctx.gpr[" << ppc_inst.ra() << "] * " << ppc_inst.simm() << "); // mulli\n";
    } else if (ppc_inst.opcode() == 8) { // subfic
        out << "    ctx.gpr[" << ppc_inst.rd() << "] = (uint32_t)(" << ppc_inst.simm() << " - (int32_t)ctx.gpr[" << ppc_inst.ra() << "]); // subfic\n";
    } else if (ppc_inst.opcode() == 12) { // addic
        out << "    ctx.gpr[" << ppc_inst.rd() << "] = ctx.gpr[" << ppc_inst.ra() << "] + " << ppc_inst.simm() << "; // addic\n";
    } else if (ppc_inst.opcode() == 13) { // addic.
        uint32_t rD = ppc_inst.rd();
        out << "    ctx.gpr[" << rD << "] = ctx.gpr[" << ppc_inst.ra() << "] + " << ppc_inst.simm() << "; // addic.\n";
        out << "    ctx.cr[0].lt = ((int32_t)ctx.gpr[" << rD << "] < 0);\n";
        out << "    ctx.cr[0].gt = ((int32_t)ctx.gpr[" << rD << "] > 0);\n";
        out << "    ctx.cr[0].eq = (ctx.gpr[" << rD << "] == 0);\n";
    } else if (ppc_inst.opcode() == 15) { // addis
        uint32_t rA = ppc_inst.ra();
        if (rA == 0) out << "    ctx.gpr[" << ppc_inst.rd() << "] = " << (ppc_inst.simm() << 16) << "; // lis\n";
        else out << "    ctx.gpr[" << ppc_inst.rd() << "] = ctx.gpr[" << rA << "] + " << (ppc_inst.simm() << 16) << "; // addis\n";
    } else if (ppc_inst.opcode() == 17) { // sc
        out << "    // sc (System Call) - NOOP for now\n";
    } else if (ppc_inst.opcode() == 4) { // ps_*
        uint32_t xo = (inst.opcode >> 1) & 0x1F;
        uint32_t xo_10 = (inst.opcode >> 1) & 0x3FF;
        uint32_t frD = ppc_inst.rd();
        uint32_t frA = ppc_inst.ra();
        uint32_t frB = ppc_inst.rb();
        uint32_t frC = ppc_inst.rc();
        
        if (xo == 21) {
            out << "    ctx.ps_add(" << frD << ", " << frA << ", " << frB << ");\n";
        } else if (xo == 20) {
            out << "    ctx.ps_sub(" << frD << ", " << frA << ", " << frB << ");\n";
        } else if (xo == 25) {
            out << "    ctx.ps_mul(" << frD << ", " << frA << ", " << frC << ");\n";
        } else if (xo == 29) {
            out << "    ctx.ps_madd(" << frD << ", " << frA << ", " << frC << ", " << frB << ");\n";
        } else if (xo == 28) {
            out << "    ctx.ps_msub(" << frD << ", " << frA << ", " << frC << ", " << frB << ");\n";
        } else if (xo == 31) {
            out << "    ctx.ps_nmadd(" << frD << ", " << frA << ", " << frC << ", " << frB << ");\n";
        } else if (xo == 30) {
            out << "    ctx.ps_nmsub(" << frD << ", " << frA << ", " << frC << ", " << frB << ");\n";
        } else if (xo == 18) {
            out << "    ctx.ps_div(" << frD << ", " << frA << ", " << frB << ");\n";
        } else if (xo == 10) {
            out << "    ctx.ps_sum0(" << frD << ", " << frA << ", " << frC << ", " << frB << ");\n";
        } else if (xo == 11) {
            out << "    ctx.ps_sum1(" << frD << ", " << frA << ", " << frC << ", " << frB << ");\n";
        } else if (xo == 12) {
            out << "    ctx.ps_muls0(" << frD << ", " << frA << ", " << frC << ");\n";
        } else if (xo == 13) {
            out << "    ctx.ps_muls1(" << frD << ", " << frA << ", " << frC << ");\n";
        } else if (xo == 14) {
            out << "    ctx.ps_madds0(" << frD << ", " << frA << ", " << frC << ", " << frB << ");\n";
        } else if (xo == 15) {
            out << "    ctx.ps_madds1(" << frD << ", " << frA << ", " << frC << ", " << frB << ");\n";
        } else if (xo == 23) {
            out << "    ctx.ps_sel(" << frD << ", " << frA << ", " << frC << ", " << frB << ");\n";
        } else if (xo_10 == 72) {
            out << "    ctx.ps_mr(" << frD << ", " << frB << ");\n";
        } else if (xo_10 == 40) {
            out << "    ctx.ps_neg(" << frD << ", " << frB << ");\n";
        } else if (xo_10 == 264) {
            out << "    ctx.ps_abs(" << frD << ", " << frB << ");\n";
        } else if (xo_10 == 136) {
            out << "    ctx.ps_nabs(" << frD << ", " << frB << ");\n";
        } else if (xo_10 == 528) {
            out << "    ctx.ps_merge00(" << frD << ", " << frA << ", " << frB << ");\n";
        } else if (xo_10 == 560) {
            out << "    ctx.ps_merge01(" << frD << ", " << frA << ", " << frB << ");\n";
        } else if (xo_10 == 592) {
            out << "    ctx.ps_merge10(" << frD << ", " << frA << ", " << frB << ");\n";
        } else if (xo_10 == 624) {
            out << "    ctx.ps_merge11(" << frD << ", " << frA << ", " << frB << ");\n";
        } else if (xo_10 == 0) {
            out << "    ctx.ps_cmpu0(" << (frD >> 2) << ", " << frA << ", " << frB << ");\n";
        } else if (xo_10 == 32) {
            out << "    ctx.ps_cmpo0(" << (frD >> 2) << ", " << frA << ", " << frB << ");\n";
        } else if (xo_10 == 64) {
            out << "    ctx.ps_cmpu1(" << (frD >> 2) << ", " << frA << ", " << frB << ");\n";
        } else if (xo_10 == 96) {
            out << "    ctx.ps_cmpo1(" << (frD >> 2) << ", " << frA << ", " << frB << ");\n";
        } else {
            out << "    // TODO: implement opcode 4 (ps_*) xo=" << xo << ", xo_10=" << xo_10 << "\n";
            out << "    ctx.fpr[" << frD << "] = 0.0;\n";
            out << "    ctx.ps1[" << frD << "] = 0.0;\n";
        }
    } else if (ppc_inst.opcode() == 10) { // cmpli
        uint32_t crD = ppc_inst.rd() >> 2;
        out << "    ctx.cr[" << crD << "].lt = (ctx.gpr[" << ppc_inst.ra() << "] < " << ppc_inst.uimm() << ");\n";
        out << "    ctx.cr[" << crD << "].gt = (ctx.gpr[" << ppc_inst.ra() << "] > " << ppc_inst.uimm() << ");\n";
        out << "    ctx.cr[" << crD << "].eq = (ctx.gpr[" << ppc_inst.ra() << "] == " << ppc_inst.uimm() << ");\n";
    } else if (ppc_inst.opcode() == 11) { // cmpi
        uint32_t crD = ppc_inst.rd() >> 2;
        out << "    ctx.cr[" << crD << "].lt = ((int32_t)ctx.gpr[" << ppc_inst.ra() << "] < " << ppc_inst.simm() << ");\n";
        out << "    ctx.cr[" << crD << "].gt = ((int32_t)ctx.gpr[" << ppc_inst.ra() << "] > " << ppc_inst.simm() << ");\n";
        out << "    ctx.cr[" << crD << "].eq = ((int32_t)ctx.gpr[" << ppc_inst.ra() << "] == " << ppc_inst.simm() << ");\n";
    } else if (ppc_inst.opcode() == 34) { // lbz
        uint32_t rA = ppc_inst.ra();
        if (rA == 0) out << "    ctx.gpr[" << ppc_inst.rd() << "] = ctx.mmu.read8(" << ppc_inst.simm() << "); // lbz\n";
        else out << "    ctx.gpr[" << ppc_inst.rd() << "] = ctx.mmu.read8(ctx.gpr[" << rA << "] + " << ppc_inst.simm() << "); // lbz\n";
    } else if (ppc_inst.opcode() == 38) { // stb
        uint32_t rA = ppc_inst.ra();
        if (rA == 0) out << "    ctx.mmu.write8(" << ppc_inst.simm() << ", (uint8_t)ctx.gpr[" << ppc_inst.rs() << "]); // stb\n";
        else out << "    ctx.mmu.write8(ctx.gpr[" << rA << "] + " << ppc_inst.simm() << ", (uint8_t)ctx.gpr[" << ppc_inst.rs() << "]); // stb\n";
    } else if (ppc_inst.opcode() == 40) { // lhz
        uint32_t rA = ppc_inst.ra();
        if (rA == 0) out << "    ctx.gpr[" << ppc_inst.rd() << "] = ctx.mmu.read16(" << ppc_inst.simm() << "); // lhz\n";
        else out << "    ctx.gpr[" << ppc_inst.rd() << "] = ctx.mmu.read16(ctx.gpr[" << rA << "] + " << ppc_inst.simm() << "); // lhz\n";
    } else if (ppc_inst.opcode() == 42) { // lha
        uint32_t rA = ppc_inst.ra();
        if (rA == 0) out << "    ctx.gpr[" << ppc_inst.rd() << "] = (uint32_t)(int32_t)(int16_t)ctx.mmu.read16(" << ppc_inst.simm() << "); // lha\n";
        else out << "    ctx.gpr[" << ppc_inst.rd() << "] = (uint32_t)(int32_t)(int16_t)ctx.mmu.read16(ctx.gpr[" << rA << "] + " << ppc_inst.simm() << "); // lha\n";
    } else if (ppc_inst.opcode() == 44) { // sth
        uint32_t rA = ppc_inst.ra();
        if (rA == 0) out << "    ctx.mmu.write16(" << ppc_inst.simm() << ", (uint16_t)ctx.gpr[" << ppc_inst.rs() << "]); // sth\n";
        else out << "    ctx.mmu.write16(ctx.gpr[" << rA << "] + " << ppc_inst.simm() << ", (uint16_t)ctx.gpr[" << ppc_inst.rs() << "]); // sth\n";
    } else if (ppc_inst.opcode() == 46) { // lmw
        uint32_t rD = ppc_inst.rd();
        uint32_t rA = ppc_inst.ra();
        int16_t simm = ppc_inst.simm();
        out << "    {\n";
        out << "        uint32_t addr = " << (rA == 0 ? std::string("0") : "ctx.gpr[" + std::to_string(rA) + "]") << " + " << simm << ";\n";
        out << "        for (int i = " << rD << "; i <= 31; ++i) {\n";
        out << "            ctx.gpr[i] = ctx.mmu.read32(addr);\n";
        out << "            addr += 4;\n";
        out << "        }\n";
        out << "    }\n";
    } else if (ppc_inst.opcode() == 47) { // stmw
        uint32_t rS = ppc_inst.rs();
        uint32_t rA = ppc_inst.ra();
        int16_t simm = ppc_inst.simm();
        out << "    {\n";
        out << "        uint32_t addr = " << (rA == 0 ? std::string("0") : "ctx.gpr[" + std::to_string(rA) + "]") << " + " << simm << ";\n";
        out << "        for (int i = " << rS << "; i <= 31; ++i) {\n";
        out << "            ctx.mmu.write32(addr, ctx.gpr[i]);\n";
        out << "            addr += 4;\n";
        out << "        }\n";
        out << "    }\n";
    } else if (ppc_inst.opcode() == 50) { // lfd
        uint32_t rA = ppc_inst.ra();
        if (rA == 0) out << "    ctx.fpr[" << ppc_inst.rd() << "] = ctx.mmu.read_f64(" << ppc_inst.simm() << "); // lfd\n";
        else out << "    ctx.fpr[" << ppc_inst.rd() << "] = ctx.mmu.read_f64(ctx.gpr[" << rA << "] + " << ppc_inst.simm() << "); // lfd\n";
    } else if (ppc_inst.opcode() == 54) { // stfd
        uint32_t rA = ppc_inst.ra();
        if (rA == 0) out << "    ctx.mmu.write_f64(" << ppc_inst.simm() << ", ctx.fpr[" << ppc_inst.rs() << "]); // stfd\n";
        else out << "    ctx.mmu.write_f64(ctx.gpr[" << rA << "] + " << ppc_inst.simm() << ", ctx.fpr[" << ppc_inst.rs() << "]); // stfd\n";
    } else if (ppc_inst.opcode() == 55) { // lfdu
        uint32_t rA = ppc_inst.ra();
        out << "    ctx.fpr[" << ppc_inst.rd() << "] = ctx.mmu.read_f64(ctx.gpr[" << rA << "] + " << ppc_inst.simm() << "); // lfdu\n";
        out << "    ctx.gpr[" << rA << "] = ctx.gpr[" << rA << "] + " << ppc_inst.simm() << ";\n";
    } else if (ppc_inst.opcode() == 56) { // psq_l
        uint32_t frD = (inst.opcode >> 21) & 0x1F;
        uint32_t rA = (inst.opcode >> 16) & 0x1F;
        uint32_t W = (inst.opcode >> 15) & 0x1;
        uint32_t I = (inst.opcode >> 12) & 0x7;
        int16_t d = inst.opcode & 0xFFF;
        if (d & 0x800) d |= 0xF000;
        if (rA == 0) out << "    ctx.psq_load(" << frD << ", " << d << ", " << W << ", " << I << "); // psq_l\n";
        else out << "    ctx.psq_load(" << frD << ", ctx.gpr[" << rA << "] + " << d << ", " << W << ", " << I << "); // psq_l\n";
    } else if (ppc_inst.opcode() == 57) { // psq_lu
        uint32_t frD = (inst.opcode >> 21) & 0x1F;
        uint32_t rA = (inst.opcode >> 16) & 0x1F;
        uint32_t W = (inst.opcode >> 15) & 0x1;
        uint32_t I = (inst.opcode >> 12) & 0x7;
        int16_t d = inst.opcode & 0xFFF;
        if (d & 0x800) d |= 0xF000;
        out << "    ctx.psq_load(" << frD << ", ctx.gpr[" << rA << "] + " << d << ", " << W << ", " << I << "); // psq_lu\n";
        out << "    ctx.gpr[" << rA << "] = ctx.gpr[" << rA << "] + " << d << ";\n";
    } else if (ppc_inst.opcode() == 60) { // psq_st
        uint32_t frS = (inst.opcode >> 21) & 0x1F;
        uint32_t rA = (inst.opcode >> 16) & 0x1F;
        uint32_t W = (inst.opcode >> 15) & 0x1;
        uint32_t I = (inst.opcode >> 12) & 0x7;
        int16_t d = inst.opcode & 0xFFF;
        if (d & 0x800) d |= 0xF000;
        if (rA == 0) out << "    ctx.psq_store(" << frS << ", " << d << ", " << W << ", " << I << "); // psq_st\n";
        else out << "    ctx.psq_store(" << frS << ", ctx.gpr[" << rA << "] + " << d << ", " << W << ", " << I << "); // psq_st\n";
    } else if (ppc_inst.opcode() == 61) { // psq_stu
        uint32_t frS = (inst.opcode >> 21) & 0x1F;
        uint32_t rA = (inst.opcode >> 16) & 0x1F;
        uint32_t W = (inst.opcode >> 15) & 0x1;
        uint32_t I = (inst.opcode >> 12) & 0x7;
        int16_t d = inst.opcode & 0xFFF;
        if (d & 0x800) d |= 0xF000;
        out << "    ctx.psq_store(" << frS << ", ctx.gpr[" << rA << "] + " << d << ", " << W << ", " << I << "); // psq_stu\n";
        out << "    ctx.gpr[" << rA << "] = ctx.gpr[" << rA << "] + " << d << ";\n";
    } else {
        out << "    std::cerr << \"UNIMPLEMENTED OPCODE " << ppc_inst.opcode() << " at 0x" << std::hex << inst.address << std::dec << "\\n\"; std::exit(1);\n";
    }
}

} // namespace recomp
} // namespace nwii
