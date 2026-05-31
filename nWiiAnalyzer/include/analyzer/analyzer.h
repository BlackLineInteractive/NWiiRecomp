#pragma once

#include "loader/loader.h"
#include <vector>
#include <set>
#include <map>
#include <cstdint>

namespace nwii {
namespace analyzer {

struct Instruction {
    uint32_t address;
    uint32_t opcode;
};

struct Function {
    uint32_t start_address;
    uint32_t end_address; // Address of the return instruction (blr) or similar
    std::vector<Instruction> instructions;
};

class Analyzer {
public:
    Analyzer(const loader::Executable& executable);

    // Run the recursive descent analysis starting from the entry point
    void analyze();

    const std::map<uint32_t, Function>& get_functions() const { return functions_; }

private:
    bool read_instruction(uint32_t address, uint32_t& out_inst) const;
    bool is_text_address(uint32_t address) const;

    const loader::Executable& executable_;
    
    std::map<uint32_t, Function> functions_;
    std::set<uint32_t> analyzed_blocks_;
};

} // namespace analyzer
} // namespace nwii
