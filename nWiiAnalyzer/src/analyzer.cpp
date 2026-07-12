#include "analyzer/analyzer.h"
#include "analyzer/dolphin_sigdb.h"
#include "common/endian.h"
#include "ppc/instruction.h"
#include "analyzer/signature_scanner.h"
#include <iostream>
#include <map>
#include <queue>
#include <algorithm>
#include <cstring>

namespace nwii {
namespace analyzer {

Analyzer::Analyzer(const loader::Executable &executable)
    : executable_(executable) {}

bool Analyzer::read_instruction(uint32_t address, uint32_t &out_inst) const {
  for (const auto &section : executable_.sections) {
    if (section.is_text && address >= section.address &&
        address < section.address + section.size) {
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

bool Analyzer::is_text_address(uint32_t address) const {
  if ((address & 3) != 0)
    return false;
  for (const auto &section : executable_.sections) {
    if (section.is_text && address >= section.address &&
        address < section.address + section.size) {
      return true;
    }
  }
  return false;
}

uint32_t Analyzer::read_data32(uint32_t address) const {
  for (const auto &section : executable_.sections) {
    if (address >= section.address &&
        address < section.address + section.size) {
      uint32_t offset = address - section.address;
      if (offset + 4 <= section.data.size()) {
        uint32_t raw;
        std::memcpy(&raw, section.data.data() + offset, sizeof(uint32_t));
        return swap_endian(raw);
      }
    }
  }
  return 0;
}

void Analyzer::analyze_jump_table(uint32_t bctr_pc,
                                  const std::map<uint32_t, uint32_t> &insts,
                                  std::queue<uint32_t> &block_queue,
                                  std::set<uint32_t> &jump_targets,
                                  const std::set<uint32_t> &known_functions) {
  // Scan backwards from bctr_pc to find mtctr
  int32_t mtctr_reg = -1;
  uint32_t curr_pc = bctr_pc - 4;

  // Look back up to 30 instructions
  for (int i = 0; i < 30 && insts.count(curr_pc); ++i, curr_pc -= 4) {
    ppc::Instruction inst(insts.at(curr_pc));

    // mtctr rX (opcode 31, extended 467, SPR=9)
    if (inst.opcode() == 31 && inst.extended_opcode() == 467) {
      uint32_t spr = (inst.value() >> 11) & 0x3FF;
      if (spr == 9) {                            // CTR
        mtctr_reg = (inst.value() >> 21) & 0x1F; // rS
        break;
      }
    }
  }

  if (mtctr_reg == -1)
    return;

  // Now find what sets mtctr_reg. Typically lwzx or addi
  uint32_t table_addr_hi = 0;

  curr_pc = bctr_pc - 4;
  for (int i = 0; i < 30 && insts.count(curr_pc); ++i, curr_pc -= 4) {
    ppc::Instruction inst(insts.at(curr_pc));

    // lis rD, IMM (opcode 15)
    if (inst.opcode() == 15) {
      uint32_t rD = (inst.value() >> 21) & 0x1F;
      uint32_t imm = inst.value() & 0xFFFF;
      // Assume this is the high half of the table address
      // We just grab the first lis we see backwards that might be relevant
      table_addr_hi = imm << 16;
      // Let's break, assuming this is it
      break;
    }
  }

  if (table_addr_hi != 0) {
    // Now let's try to scan the table!
    // We don't know the exact base if there was an addi, but often
    // table_addr_hi is close enough, or we just scan a region starting from
    // table_addr_hi. Actually, we can just look at table_addr_hi + lo from an
    // addi if it exists. Or we can just scan the data section at table_addr_hi
    // for valid text pointers!

    // Let's refine: find an addi (opcode 14) that uses the lis register
    uint32_t table_base = table_addr_hi;
    curr_pc = bctr_pc - 4;
    for (int i = 0; i < 30 && insts.count(curr_pc); ++i, curr_pc -= 4) {
      ppc::Instruction inst(insts.at(curr_pc));
      if (inst.opcode() == 14) { // addi rD, rA, SIMM
        int32_t simm = (int16_t)(inst.value() & 0xFFFF);
        table_base += simm;
        break; // Just guess this is the table base
      }
      if (inst.opcode() == 55) { // lwzux
        break;
      }
    }

    // Validate table_base
    uint32_t target = read_data32(table_base);
    int valid_targets = 0;

    // A jump table typically has multiple entries pointing to the same
    // function's text region. We'll read consecutive 32-bit values.
    uint32_t scan_addr = table_base;
    while (true) {
      uint32_t ptr = read_data32(scan_addr);
      if (!is_text_address(ptr))
        break;

      // Typical switch case is local, within +/- 32KB of bctr_pc
      int32_t diff = (int32_t)ptr - (int32_t)bctr_pc;
      if (std::abs(diff) > 0x8000)
        break;

      // Also if it points to a known function, it's a VTable or callback array, not a local switch!
      if (known_functions.find(ptr) != known_functions.end()) {
          // It's a pointer to another function, ignore for jump table
          scan_addr += 4;
          continue;
      }

      block_queue.push(ptr);
      jump_targets.insert(ptr);
      valid_targets++;
      scan_addr += 4;

      if (valid_targets > 1000)
        break; // Sanity limit
    }

    if (valid_targets > 0) {
      std::cout << "[Analyzer] Found jump table at 0x" << std::hex << table_base
                << " with " << std::dec << valid_targets
                << " targets from bctr at 0x" << std::hex << bctr_pc << std::dec
                << "\n";
    }
  }
}

void Analyzer::analyze(const std::vector<uint32_t>& additional_roots) {
  std::queue<uint32_t> function_queue;
  std::set<uint32_t> known_functions;

  if (executable_.entry_point != 0) {
    function_queue.push(executable_.entry_point);
    known_functions.insert(executable_.entry_point);
  }

  // Seed with additional roots (e.g. from symbols.csv)
  for (uint32_t root : additional_roots) {
    if (is_text_address(root) &&
        known_functions.find(root) == known_functions.end()) {
      known_functions.insert(root);
      function_queue.push(root);
    }
  }

  // Scan data sections for potential function pointers
  for (const auto &section : executable_.sections) {
    if (!section.is_text) {
      for (size_t i = 0; i + 4 <= section.data.size(); i += 4) {
        uint32_t ptr = read_data32(section.address + i);
        if (is_text_address(ptr) &&
            known_functions.find(ptr) == known_functions.end()) {
          known_functions.insert(ptr);
          function_queue.push(ptr);
        }
      }
    } else {
      // Universal lis/addi heuristic scanner for OS callbacks in .text
      // This eliminates the need for hardcoded OS callback addresses!
      uint32_t lis_regs[32] = {0};
      for (size_t i = 0; i + 4 <= section.data.size(); i += 4) {
        uint32_t inst = read_data32(section.address + i);
        uint32_t opcode = inst >> 26;
        uint32_t rD = (inst >> 21) & 0x1F;
        uint32_t rA = (inst >> 16) & 0x1F;
        int16_t simm = inst & 0xFFFF;

        if (opcode == 15 && rA == 0) { // lis rD, simm
          lis_regs[rD] = (uint32_t)simm << 16;
        } else if (opcode == 14) { // addi rD, rA, simm
          if (lis_regs[rA] != 0) {
            uint32_t target = lis_regs[rA] + (int32_t)simm;
            if (is_text_address(target) && known_functions.find(target) == known_functions.end()) {
              known_functions.insert(target);
              function_queue.push(target);
            }
          }
          lis_regs[rD] = 0; // Clear it so we don't carry false positives
        } else if (opcode == 18 || opcode == 19 || opcode == 16) {
           // Clear all on branch to avoid cross-block false positives
           for (int r = 0; r < 32; r++) lis_regs[r] = 0;
        } else {
           // If a register is overwritten by something else, clear its lis value
           lis_regs[rD] = 0;
        }
      }
    }
  }

  std::cout << "[Analyzer] Starting analysis. Entry point: 0x" << std::hex
            << executable_.entry_point << std::dec << "\n";
  int analyzed_count = 0;

  while (!function_queue.empty()) {
    uint32_t func_start = function_queue.front();
    function_queue.pop();

    if (functions_.find(func_start) != functions_.end())
      continue;

    uint32_t min_pc = func_start;
    uint32_t max_pc = func_start;

    std::map<uint32_t, uint32_t> insts;
    std::queue<uint32_t> block_queue;
    std::set<uint32_t> visited_blocks;
    std::set<uint32_t> local_jump_targets;

    block_queue.push(func_start);

    while (!block_queue.empty()) {
      uint32_t current_pc = block_queue.front();
      block_queue.pop();

      while (true) {
        if (visited_blocks.count(current_pc))
          break;
        visited_blocks.insert(current_pc);
        analyzed_blocks_.insert(current_pc);

        uint32_t raw_inst;
        if (!read_instruction(current_pc, raw_inst))
          break;

        insts[current_pc] = raw_inst;
        min_pc = std::min(min_pc, current_pc);
        max_pc = std::max(max_pc, current_pc);

        ppc::Instruction inst(raw_inst);

        if (inst.is_branch_link()) {
          uint32_t target = inst.branch_target(current_pc);
          if (target != 0 &&
              known_functions.find(target) == known_functions.end()) {
            known_functions.insert(target);
            function_queue.push(target);
          }
        } else if (inst.is_unconditional_indirect_branch()) {
          // It's a bctr or bclr
          if (inst.opcode() == 19 && (inst.value() & 0x3FF) == 528) {
            // bctr
            analyze_jump_table(current_pc, insts, block_queue,
                               local_jump_targets, known_functions);
          }
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

    for (const auto &[addr, op] : insts) {
      func.instructions.push_back({addr, op});
    }
    func.jump_table_targets = local_jump_targets;

    functions_[func_start] = func;

    analyzed_count++;
    if (analyzed_count % 1000 == 0) {
      std::cout << "[Analyzer] Analyzed " << analyzed_count
                << " functions. Queue size: " << function_queue.size() << "\n";
    }
  }
  SignatureScanner scanner;
  for (auto &pair : functions_) {
    Function &func = pair.second;
    std::vector<uint32_t> opcodes;
    for (const auto &inst : func.instructions) {
      opcodes.push_back(inst.opcode);
    }
    func.hle_hook_name = scanner.match(opcodes);
    if (!func.hle_hook_name.empty()) {
      std::cout << "[Analyzer] Signature matched at 0x" << std::hex << func.start_address 
                << " -> " << func.hle_hook_name << std::dec << "\n";
    }
  }

  std::cout << "[Analyzer] Analysis complete. Total functions found: "
            << functions_.size() << "\n";
}

int Analyzer::apply_signature_db(const std::string& dsy_path) {
  DolphinSigDB db;
  if (!db.load_dsy(dsy_path)) {
    std::cout << "[Analyzer] No signature DB at " << dsy_path << "\n";
    return 0;
  }

  int matched = 0;
  for (auto &pair : functions_) {
    Function &func = pair.second;
    if (func.instructions.empty())
      continue;
    std::vector<uint32_t> opcodes;
    opcodes.reserve(func.instructions.size());
    for (const auto &inst : func.instructions)
      opcodes.push_back(inst.opcode);

    uint32_t sum = DolphinSigDB::checksum(opcodes);
    if (const DolphinSigDB::Entry *e = db.match(sum)) {
      // Guard against hash collisions on tiny functions: require the
      // database size to agree with what we disassembled.
      uint32_t our_size = (uint32_t)opcodes.size() * 4;
      if (e->size == our_size) {
        func.sdk_name = e->name;
        matched++;
      }
    }
  }
  std::cout << "[Analyzer] Signature DB matched " << matched << " of "
            << functions_.size() << " functions\n";
  return matched;
}

} // namespace analyzer
} // namespace nwii
