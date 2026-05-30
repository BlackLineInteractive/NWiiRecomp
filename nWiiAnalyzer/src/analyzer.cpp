#include "analyzer/analyzer.h"
#include "ppc/instruction.h"
#include "common/endian.h"
#include <queue>
#include <iostream>

namespace nwii {
namespace analyzer {

Analyzer::Analyzer(const loader::Executable& executable) : executable_(executable) {}

bool Analyzer::read_instruction(uint32_t address, uint32_t& out_inst) const {
    for (const auto& section : executable_.sections) {
        if (section.is_text && address >= section.address && address < section.address + section.size) {
            uint32_t offset = address - section.address;
            if (offset + 4 <= section.data.size()) {
                // Read 4 bytes and swap endianness
                uint32_t raw;
                std::memcpy(&raw, section.data.data() + offset, sizeof(uint32_t));
                out_inst = swap_endian(raw);
                return true;
            }
        }
    }
    return false;
}

void Analyzer::analyze() {
    std::queue<uint32_t> function_queue;
    std::set<uint32_t> known_functions;

    if (executable_.entry_point != 0) {
        function_queue.push(executable_.entry_point);
        known_functions.insert(executable_.entry_point);
    }

    while (!function_queue.empty()) {
        uint32_t func_start = function_queue.front();
        function_queue.pop();

        if (functions_.find(func_start) != functions_.end()) {
            continue; // Already analyzed
        }

        uint32_t current_pc = func_start;
        uint32_t max_pc = func_start;
        bool func_ended = false;

        while (!func_ended) {
            if (analyzed_blocks_.count(current_pc)) {
                // Reached an already analyzed block, assume function continues or ends here
                break;
            }
            analyzed_blocks_.insert(current_pc);
            max_pc = std::max(max_pc, current_pc);

            uint32_t raw_inst;
            if (!read_instruction(current_pc, raw_inst)) {
                std::cerr << "Failed to read instruction at 0x" << std::hex << current_pc << std::dec << std::endl;
                break;
            }

            ppc::Instruction inst(raw_inst);

            if (inst.is_branch_link()) {
                // Call to another function
                uint32_t target = inst.branch_target(current_pc);
                if (target != 0 && known_functions.find(target) == known_functions.end()) {
                    known_functions.insert(target);
                    function_queue.push(target);
                }
            } else if (inst.is_branch_to_lr()) {
                // Return from function
                func_ended = true;
                break;
            } else if (inst.is_unconditional_branch()) {
                // Branch (not link). Could be a tail call or just a branch within the function.
                uint32_t target = inst.branch_target(current_pc);
                
                // Extremely simple heuristic: if it branches backwards a lot, or outside known bounds, it might be a tail call.
                // For now, we will just treat it as continuing the current function's block analysis.
                // In a real recompiler, we queue the target block. 
                // To avoid infinite loops in this basic version, we just follow the branch.
                if (target != 0 && target >= executable_.sections[0].address) {
                    current_pc = target;
                    continue; // Skip the += 4
                }
            }

            current_pc += 4;
        }

        Function func;
        func.start_address = func_start;
        func.end_address = max_pc + 4;
        
        for (uint32_t addr = func.start_address; addr < func.end_address; addr += 4) {
            uint32_t raw;
            if (read_instruction(addr, raw)) {
                func.instructions.push_back({addr, raw});
            }
        }
        
        functions_[func_start] = func;
    }
}

} // namespace analyzer
} // namespace nwii
