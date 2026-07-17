#pragma once

#include <cstdint>
#include <map>
#include <vector>
#include <set>
#include <queue>

#include "loader/loader.h"

namespace nwii {
namespace analyzer {

struct Instruction {
    uint32_t address;
    uint32_t opcode;
};

struct Function {
    uint32_t start_address;
    uint32_t end_address; 
    std::string hle_hook_name; 
    // SDK name recovered from a Dolphin signature database (totaldb.dsy),

    std::string sdk_name;
    std::vector<Instruction> instructions;
    std::set<uint32_t> jump_table_targets;
};

class Analyzer {
public:
    Analyzer(const loader::Executable& executable);

    void analyze(const std::vector<uint32_t>& additional_roots = {});

    // Match discovered functions against a Dolphin signature database
    
    int apply_signature_db(const std::string& dsy_path);

    const std::map<uint32_t, Function>& get_functions() const { return functions_; }

private:
    bool read_instruction(uint32_t address, uint32_t& out_inst) const;
    bool is_text_address(uint32_t address) const;
    uint32_t read_data32(uint32_t address) const;
    void analyze_jump_table(uint32_t bctr_pc, const std::map<uint32_t, uint32_t>& insts, std::queue<uint32_t>& block_queue, std::set<uint32_t>& jump_targets, const std::set<uint32_t>& known_functions);

    const loader::Executable& executable_;
    
    std::map<uint32_t, Function> functions_;
    std::set<uint32_t> analyzed_blocks_;
};

} 
} 
