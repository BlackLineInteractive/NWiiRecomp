#include "runtime/cafe_os.h"
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

namespace nwii::runtime::cafe {

// RPL Module Registry
// Tracks loaded RPL modules for dynamic linking

static std::unordered_map<std::string, RPLModule> g_rpl_modules;

void register_rpl_module(const RPLModule &module) {
  std::cout << "[Cafe OS] Registered RPL module: " << module.name << " at 0x"
            << std::hex << module.base_address << std::dec
            << " (imports=" << module.imports.size()
            << ", exports=" << module.exports.size() << ")" << std::endl;
  g_rpl_modules[module.name] = module;
}

RPLModule *find_rpl_module(const std::string &name) {
  auto it = g_rpl_modules.find(name);
  if (it != g_rpl_modules.end()) {
    return &it->second;
  }
  return nullptr;
}

// RPX/RPL Loader
// Parses the RPX header to identify sections, imports, and exports.
// Real implementation will decompress zlib sections and resolve symbols.

bool load_rpx(CPUContext &ctx, const std::string &path) {
  std::cout << "[Cafe OS] Loading RPX: " << path << std::endl;

  // TODO: Implement full RPX parsing:
  // 1. Read ELF32 header, verify e_type == 0xFE01 (Cafe RPL)
  // 2. Parse section headers
  // 3. Decompress zlib-compressed sections (SHF_RPL_ZLIB)
  // 4. Load .text, .data, .rodata into the expanded MMU (2GB address space)
  // 5. Parse .rplimports sections to build the import table
  // 6. Resolve imports by matching against registered HLE module exports

  std::cout << "[Cafe OS] RPX loader not yet implemented" << std::endl;
  return false;
}

// Cafe OS Memory Allocation
// Uses the Page Table Walker in the expanded MMU for dynamic allocation.

// Simple bump allocator for Cafe OS system memory
// Real Wii U uses 0x01000000 - 0x50000000 virtual address space
static uint32_t cafe_heap_ptr =
    0x10000000; // Start of Cafe OS heap in physical memory

uint32_t OSAllocFromSystem(CPUContext &ctx, uint32_t size, int align) {
  // Align the pointer
  if (align > 0) {
    cafe_heap_ptr = (cafe_heap_ptr + align - 1) & ~(align - 1);
  }

  uint32_t result = cafe_heap_ptr;
  cafe_heap_ptr += size;

  std::cout << "[Cafe OS] OSAllocFromSystem: size=" << size
            << " align=" << align << " -> ptr=0x" << std::hex << result
            << std::dec << std::endl;

  return result;
}

void OSFreeToSystem(CPUContext &ctx, uint32_t ptr) {
  // Bump allocator doesn't support free; log and ignore
  std::cout << "[Cafe OS] OSFreeToSystem: ptr=0x" << std::hex << ptr << std::dec
            << " (ignored, bump allocator)" << std::endl;
}

} // namespace nwii::runtime::cafe
