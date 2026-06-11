#include "analyzer/analyzer.h"
#include "common/endian.h"
#include "ppc/instruction.h"
#include <iostream>
#include <map>
#include <queue>

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
                                  std::set<uint32_t> &jump_targets) {
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

      // Typical switch case is local, within +/- 1MB of bctr_pc
      int32_t diff = (int32_t)ptr - (int32_t)bctr_pc;
      if (std::abs(diff) > 0x100000)
        break;

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

void Analyzer::analyze() {
  std::queue<uint32_t> function_queue;
  std::set<uint32_t> known_functions;

  if (executable_.entry_point != 0) {
    function_queue.push(executable_.entry_point);
    known_functions.insert(executable_.entry_point);
  }

  // Hardcoded dynamic dispatch entry points (OS functions computed via
  // lis/addi)
  std::vector<uint32_t> hints = {
      0x8010beb4,
      0x80218070,
      0x802180F0,
      // STM/IPC thread callbacks (IOS_IoctlAsync)
      0x8021cc70,
      0x8021c000,
      0x8021d000,
      // FS/ISFS async callbacks (IOS_OpenAsync / iosQueueMessage)
      0x80240680,
      0x80244e30,
      0x802464b0,
      0x80247730,
      0x8019BA08,
      0x8019ba60,
      0x8018fea8,
      0x801a98fc,
      0x801aa550,
      // IOS/DVD callbacks discovered at runtime
      0x80222f80, // missing DVD callback (fallback dispatch crash point)
      0x80222f90,
      0x80223000,
      0x80223100,
      0x80223200,
      0x80223300,
      0x80223400,
      0x80223500,
      0x80223600,
      0x80223700,
      0x80223800,
      0x80223900,
      0x80225190,
      0x80225230,
      0x802252c0,
      0x802258a0,
      0x80225920,
      0x802259a0,
      0x80225a20,
      0x80225aa0,
      0x80225b20,
      0x8027c310,
      0x80225ba0,
      0x80225c20,
      0x80211060, // <-- DVD Callback
      0x80009ccc, // <-- OS Error Handler
          // Additional callback ranges
      0x80220000,
      0x80221000,
      0x80222000,
      0x80223000,
      0x80224000,
      0x80225000,
      0x80226000,
      0x80227000,
      0x80228000,
      0x80229000,
      0x8022a000,
      0x8022b000,
  };
  // RVL_SDK IPC/FS module: callbacks are often registered at runtime only.
  for (uint32_t addr = 0x80210000; addr < 0x80260000; addr += 0x10) {
    hints.push_back(addr);
  }
  for (uint32_t hint : hints) {
    if (is_text_address(hint) &&
        known_functions.find(hint) == known_functions.end()) {
      known_functions.insert(hint);
      function_queue.push(hint);
    }
  }

  // Scan data sections for potential function pointers
  for (const auto &section : executable_.sections) {
    if (!section.is_text) {
      for (size_t i = 0; i + 4 <= section.data.size(); i += 4) {
        uint32_t ptr;
        std::memcpy(&ptr, section.data.data() + i, 4);
        ptr = swap_endian(ptr);
        if (is_text_address(ptr) &&
            known_functions.find(ptr) == known_functions.end()) {
          known_functions.insert(ptr);
          function_queue.push(ptr);
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
          if (inst.opcode() == 19 && inst.extended_opcode() == 528) { // bctr
            analyze_jump_table(current_pc, insts, block_queue,
                               local_jump_targets);
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
  std::cout << "[Analyzer] Analysis complete. Total functions found: "
            << functions_.size() << "\n";
}

} // namespace analyzer
} // namespace nwii
