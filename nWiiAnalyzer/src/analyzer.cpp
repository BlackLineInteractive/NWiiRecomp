#include "analyzer/analyzer.h"
#include "ppc/instruction.h"
#include "common/endian.h"
#include <queue>
#include <map>
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

        if (functions_.find(func_start) != functions_.end()) continue;

        uint32_t min_pc = func_start;
        uint32_t max_pc = func_start;
        
        std::map<uint32_t, uint32_t> insts;
        std::queue<uint32_t> block_queue;
        std::set<uint32_t> visited_blocks;
        
        block_queue.push(func_start);

        while (!block_queue.empty()) {
            uint32_t current_pc = block_queue.front();
            block_queue.pop();

            while (true) {
                if (visited_blocks.count(current_pc)) break;
                visited_blocks.insert(current_pc);
                analyzed_blocks_.insert(current_pc);

                uint32_t raw_inst;
                if (!read_instruction(current_pc, raw_inst)) break;

                insts[current_pc] = raw_inst;
                min_pc = std::min(min_pc, current_pc);
                max_pc = std::max(max_pc, current_pc);

                ppc::Instruction inst(raw_inst);

                if (inst.is_branch_link()) {
                    uint32_t target = inst.branch_target(current_pc);
                    if (target != 0 && known_functions.find(target) == known_functions.end()) {
                        known_functions.insert(target);
                        function_queue.push(target);
                    }
                } else if (inst.is_unconditional_indirect_branch()) {
                    break;
                } else if (inst.is_unconditional_branch()) {
                    uint32_t target = inst.branch_target(current_pc);
                    if (target != 0) {
                        int32_t diff = (int32_t)target - (int32_t)current_pc;
                        if (std::abs(diff) < 0x80000) { // Local branch (< 512KB)
                            block_queue.push(target);
                        } else { // Tail call
                            if (known_functions.find(target) == known_functions.end()) {
                                known_functions.insert(target);
                                function_queue.push(target);
                            }
                        }
                    }
                    break; 
                }
                
                // Conditional branch (opcode 16: bc)
                if (inst.opcode() == 16) {
                    uint32_t target = inst.branch_target(current_pc);
                    if (target != 0) {
                        int32_t diff = (int32_t)target - (int32_t)current_pc;
                        if (std::abs(diff) < 0x80000) { // Local conditional branch
                            block_queue.push(target);
                        }
                    }
                }

                current_pc += 4;
            }
        }

        Function func;
        func.start_address = min_pc;
        func.end_address = max_pc + 4;
        
        for (auto const& [addr, raw] : insts) {
            func.instructions.push_back({addr, raw});
        }
        
        functions_[func_start] = func;
    }
}

} // namespace analyzer
} // namespace nwii
