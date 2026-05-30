#include "runtime/cpu_context.h"
#include <iostream>

using namespace nwii::runtime;

// Basic linear allocator for the Wii OS Heap
// In a real OS, there are multiple heaps (MEM1, MEM2).
// For now, we will use a single bump allocator in MEM1 starting at 0x80800000.
static uint32_t current_heap_ptr = 0x80800000;

extern "C" {

void OSInitAlloc(CPUContext& ctx) {
    uint32_t arena_start = ctx.gpr[3];
    uint32_t arena_end = ctx.gpr[4];
    int max_heaps = ctx.gpr[5];
    
    std::cout << "[HLE OS] OSInitAlloc: arena_start=" << std::hex << arena_start 
              << ", arena_end=" << arena_end 
              << ", max_heaps=" << std::dec << max_heaps << std::endl;
}

void OSCreateHeap(CPUContext& ctx) {
    uint32_t start_addr = ctx.gpr[3];
    uint32_t size = ctx.gpr[4];
    
    std::cout << "[HLE OS] OSCreateHeap: start=" << std::hex << start_addr 
              << ", size=" << size << std::endl;
              
    // Return a dummy heap handle
    ctx.gpr[3] = 1; 
}

void OSAllocFromHeap(CPUContext& ctx) {
    uint32_t heap = ctx.gpr[3];
    uint32_t size = ctx.gpr[4];
    
    // Simple bump allocation
    uint32_t allocated = current_heap_ptr;
    // Align to 32 bytes
    size = (size + 31) & ~31;
    current_heap_ptr += size;
    
    std::cout << "[HLE OS] OSAllocFromHeap: heap=" << heap << ", size=" << size 
              << " -> returning 0x" << std::hex << allocated << std::endl;
              
    ctx.gpr[3] = allocated;
}

void OSFreeToHeap(CPUContext& ctx) {
    uint32_t heap = ctx.gpr[3];
    uint32_t ptr = ctx.gpr[4];
    
    // Bump allocator cannot free individual blocks, so this is a no-op for now.
    std::cout << "[HLE OS] OSFreeToHeap: heap=" << heap << ", ptr=" << std::hex << ptr << std::endl;
}

} // extern "C"
