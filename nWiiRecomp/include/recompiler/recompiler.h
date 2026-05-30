#pragma once
#include "analyzer/analyzer.h"
#include "recompiler/symbols.h"
#include <string>
#include <vector>

namespace nwii {
namespace recomp {

class Recompiler {
public:
    Recompiler(const analyzer::Analyzer& analyzer, const SymbolTable* symbols = nullptr);

    // Generates the C++ output and writes to the given path
    bool generate_cpp(const std::string& output_path, uint32_t entry_point);
    
    // Generates a standalone CMake project containing the C++ output and the runtime
    bool generate_cmake_project(const std::string& output_dir, const std::string& runtime_source_path, uint32_t entry_point);
    
    // Generates C++ output for a single function
    std::string generate_function_cpp(const analyzer::Function& func);

private:
    void emit_function(std::ostream& out, const analyzer::Function& func, const std::string& func_name);
    void emit_instruction(std::ostream& out, const analyzer::Instruction& inst, const analyzer::Function& func);

    const analyzer::Analyzer& analyzer_;
    const SymbolTable* symbols_;
};

} // namespace recomp
} // namespace nwii
