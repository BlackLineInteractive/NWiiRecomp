#pragma once
#include "analyzer/analyzer.h"
#include "recompiler/symbols.h"
#include <map>
#include <string>
#include <vector>

namespace nwii {
namespace recomp {

struct RecompilerConfig {
    std::string project_name = "RecompiledGame";
    std::string input_game_dir = "";
    std::string output_dir = "export";
    std::string runtime_source_dir = "../nWiiRuntime";
    std::string symbols_csv = "";

    bool split_output = false;
    int instructions_per_file = 20000;

    

    std::map<uint32_t, std::string> hle_hooks;

    

    struct RelAot { std::string path; uint32_t module_base; uint32_t bss_base; };
    std::vector<RelAot> rel_aot;
};

class Recompiler {
public:
    Recompiler(const analyzer::Analyzer& analyzer, const SymbolTable* symbols = nullptr, const RecompilerConfig& config = RecompilerConfig());

    std::vector<std::string> generate_cpp(uint32_t entry_point);

    bool generate_cmake_project(uint32_t entry_point);

    std::string generate_function_cpp(const analyzer::Function& func);

private:
    void emit_function(std::ostream& out, const analyzer::Function& func, const std::string& func_name);
    void emit_instruction(std::ostream& out, const analyzer::Instruction& inst, const analyzer::Function& func);

    const analyzer::Analyzer& analyzer_;
    const SymbolTable* symbols_;
    RecompilerConfig config_;
};

} 
} 
