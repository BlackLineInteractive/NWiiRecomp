#include "runtime/cpu_context.h"
#include <iostream>

using namespace nwii::runtime;

// Basic Memory Manager for the Wii OS Heap
// We will allocate linearly for now but with detailed logging to show progress
static uint32_t current_heap_ptr = 0x80800000;

extern "C" {

void OSInitAlloc(CPUContext& ctx) {
    uint32_t arena_start = ctx.gpr[3];
    uint32_t arena_end = ctx.gpr[4];
    int max_heaps = ctx.gpr[5];
    
    std::cout << "[HLE Memory] Initializing Memory Allocator..." << std::endl;
    std::cout << "  Arena Start: 0x" << std::hex << arena_start << std::endl;
    std::cout << "  Arena End:   0x" << std::hex << arena_end << std::endl;
    std::cout << "  Max Heaps:   " << std::dec << max_heaps << std::endl;
}

void OSCreateHeap(CPUContext& ctx) {
    uint32_t start_addr = ctx.gpr[3];
    uint32_t size = ctx.gpr[4];
    
    std::cout << "[HLE Memory] OSCreateHeap -> Start: 0x" << std::hex << start_addr 
              << ", Size: " << std::dec << size << " bytes" << std::endl;
              
    // Return a dummy heap handle
    static uint32_t next_heap_handle = 1;
    ctx.gpr[3] = next_heap_handle++; 
}

void OSAllocFromHeap(CPUContext& ctx) {
    uint32_t heap = ctx.gpr[3];
    uint32_t size = ctx.gpr[4];
    
    // Align to 32 bytes
    size = (size + 31) & ~31;
    uint32_t allocated = current_heap_ptr;
    current_heap_ptr += size;
    
    std::cout << "[HLE Memory] OSAllocFromHeap -> Heap: " << std::dec << heap 
              << ", Size: " << size << " bytes. Allocated at: 0x" << std::hex << allocated << std::endl;
              
    ctx.gpr[3] = allocated;
}

void OSFreeToHeap(CPUContext& ctx) {
    uint32_t heap = ctx.gpr[3];
    uint32_t ptr = ctx.gpr[4];
    
    std::cout << "[HLE Memory] OSFreeToHeap -> Heap: " << std::dec << heap 
              << ", Pointer: 0x" << std::hex << ptr << std::endl;
}

} // extern "C"
