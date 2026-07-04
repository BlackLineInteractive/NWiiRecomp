#include "runtime/cpu_context.h"
#include <iostream>

using namespace nwii::runtime;

// RVL_SDK OSHeap uses a strict 32-byte header.
// Offset 0x18: Magic/Flags (0x7373 = used, 0x4652 = free)
// Offset 0x1C: Payload Size

extern "C" {

void OSInitAlloc(CPUContext& ctx) {
    // Usually initializes the global arena limits. We let it pass.
}

void OSCreateHeap(CPUContext& ctx) {
    // SDK signature: OSCreateHeap(void* lo, void* hi)
    uint32_t start_addr = ctx.gpr[3];
    uint32_t end_addr = ctx.gpr[4];
    uint32_t size = end_addr - start_addr;

    // We align the start address to 32 bytes
    uint32_t aligned_start = (start_addr + 31) & ~31;
    
    // Write initial FREE block header into guest memory
    ctx.mmu.write32(aligned_start + 0x18, 0x46520000); // Magic 'FR'
    ctx.mmu.write32(aligned_start + 0x1C, size - 32);  // Size
    
    std::cout << "[HLE Memory] OSCreateHeap at 0x" << std::hex << aligned_start << " (Size: " << std::dec << size << ")\n";
    ctx.gpr[3] = aligned_start; 
}

void OSAllocFromHeap(CPUContext& ctx) {
    uint32_t heap = ctx.gpr[3];
    uint32_t req_size = ctx.gpr[4];
    
    if (heap == 0) { ctx.gpr[3] = 0; return; }
    
    uint32_t alloc_size = (req_size + 31) & ~31; // Align allocation
    uint32_t current_block = heap;
    
    // Simple Guest-Memory Linked List Traversal
    while (true) {
        uint32_t magic = ctx.mmu.read32(current_block + 0x18);
        uint32_t size  = ctx.mmu.read32(current_block + 0x1C);
        
        if (magic == 0) break; // Reached end of uninitialized heap or bounds
        
        if ((magic >> 16) == 0x4652 && size >= alloc_size) { // 'FR' (Free)
            // Found suitable block. Split it if there's enough room for another header
            if (size > alloc_size + 32) {
                uint32_t next_block = current_block + 32 + alloc_size;
                ctx.mmu.write32(next_block + 0x18, 0x46520000); // Next is FREE
                ctx.mmu.write32(next_block + 0x1C, size - alloc_size - 32);
            } else {
                alloc_size = size; // Take the whole block
            }
            
            // Mark current block as USED
            ctx.mmu.write32(current_block + 0x18, 0x73730000); // Magic 'ss'
            ctx.mmu.write32(current_block + 0x1C, alloc_size);
            
            uint32_t payload_ptr = current_block + 32;
            std::cout << "[HLE Memory] Allocated " << std::dec << req_size << " bytes at 0x" << std::hex << payload_ptr << "\n";
            ctx.gpr[3] = payload_ptr;
            return;
        }
        
        current_block += 32 + size; // Move to next block
    }
    
    std::cerr << "[HLE Memory] ERROR: OSAllocFromHeap OOM or corrupted heap at 0x" << std::hex << heap << "\n";
    ctx.gpr[3] = 0;
}

void OSFreeToHeap(CPUContext& ctx) {
    uint32_t ptr = ctx.gpr[4];
    if (ptr == 0) return;
    
    uint32_t block_addr = ptr - 32;
    uint32_t magic = ctx.mmu.read32(block_addr + 0x18);
    
    if ((magic >> 16) == 0x7373) {
        // Mark as free. A real OS would coalesce adjacent free blocks here.
        ctx.mmu.write32(block_addr + 0x18, 0x46520000);
        std::cout << "[HLE Memory] Freed block at 0x" << std::hex << ptr << "\n";
    } else {
        std::cerr << "[HLE Memory] WARNING: Attempted to free invalid block at 0x" << std::hex << ptr << "\n";
    }
}

} // extern "C"

