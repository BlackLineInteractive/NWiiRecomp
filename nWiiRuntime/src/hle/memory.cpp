#include "runtime/cpu_context.h"
#include <iostream>

using namespace nwii::runtime;



#define MAX_HEAPS 32

struct HeapInfo {
    bool active;
    uint32_t start_addr;
    uint32_t end_addr;
};

static HeapInfo g_heaps[MAX_HEAPS] = {0};
static int g_current_heap = -1;

extern "C" {

void OSInitAlloc(CPUContext& ctx) {
    uint32_t arena_lo = ctx.gpr[3];
    uint32_t arena_hi = ctx.gpr[4];
    uint32_t max_heaps = ctx.gpr[5];
    
    for (int i = 0; i < MAX_HEAPS; i++) {
        g_heaps[i].active = false;
    }

    
    
    uint32_t array_size = max_heaps * 0x20;
    
    std::cout << "[HLE Memory] OSInitAlloc arena_lo: 0x" << std::hex << arena_lo << " -> 0x" << (arena_lo + array_size) << " max_heaps: " << std::dec << max_heaps << "\n";
    ctx.gpr[3] = arena_lo + array_size;
}

void OSCreateHeap(CPUContext& ctx) {
    uint32_t start_addr = ctx.gpr[3];
    uint32_t end_addr = ctx.gpr[4];
    uint32_t size = end_addr - start_addr;

    uint32_t aligned_start = (start_addr + 31) & ~31;
    
    ctx.mmu.write32(aligned_start + 0x18, 0x46520000); 
    ctx.mmu.write32(aligned_start + 0x1C, size - 32);
    
    int handle = -1;
    for (int i = 0; i < MAX_HEAPS; i++) {
        if (!g_heaps[i].active) {
            g_heaps[i].active = true;
            g_heaps[i].start_addr = aligned_start;
            g_heaps[i].end_addr = end_addr;
            handle = i;
            break;
        }
    }

    std::cout << "[HLE Memory] OSCreateHeap at 0x" << std::hex << aligned_start << " (Size: " << std::dec << size << ") -> Handle: " << handle << "\n";
    
    ctx.gpr[3] = handle; 
}

void OSDestroyHeap(CPUContext& ctx) {
    int handle = ctx.gpr[3];
    if (handle >= 0 && handle < MAX_HEAPS && g_heaps[handle].active) {
        g_heaps[handle].active = false;
        std::cout << "[HLE Memory] OSDestroyHeap handle: " << handle << "\n";
    }
}

void OSSetCurrentHeap(CPUContext& ctx) {
    int handle = ctx.gpr[3];
    int old_heap = g_current_heap;
    if (handle >= 0 && handle < MAX_HEAPS && g_heaps[handle].active) {
        g_current_heap = handle;
    }
    ctx.gpr[3] = old_heap;
}

void OSGetCurrentHeap(CPUContext& ctx) {
    ctx.gpr[3] = g_current_heap;
}

void OSAllocFromHeap(CPUContext& ctx) {
    int handle = ctx.gpr[3];
    uint32_t req_size = ctx.gpr[4];
    
    if (handle < 0 || handle >= MAX_HEAPS || !g_heaps[handle].active) {
        std::cerr << "[HLE Memory] ERROR: OSAllocFromHeap invalid handle " << handle << "\n";
        ctx.gpr[3] = 0; 
        return; 
    }
    
    uint32_t heap_start = g_heaps[handle].start_addr;
    uint32_t heap_end = g_heaps[handle].end_addr;
    uint32_t alloc_size = (req_size + 31) & ~31;
    uint32_t current_block = heap_start;
    
    while (current_block < heap_end) {
        uint32_t magic = ctx.mmu.read32(current_block + 0x18);
        uint32_t size  = ctx.mmu.read32(current_block + 0x1C);
        
        if (magic == 0) break;

        if ((magic >> 16) == 0x4652) {
            uint32_t next_block = current_block + 32 + size;
            while (next_block < heap_end) {
                uint32_t next_magic = ctx.mmu.read32(next_block + 0x18);
                uint32_t next_size  = ctx.mmu.read32(next_block + 0x1C);
                if ((next_magic >> 16) == 0x4652) {
                    size += 32 + next_size;
                    ctx.mmu.write32(current_block + 0x1C, size);
                    next_block += 32 + next_size;
                } else {
                    break;
                }
            }
        }
        
        if ((magic >> 16) == 0x4652 && size >= alloc_size) {
            if (size > alloc_size + 32) {
                uint32_t next_block = current_block + 32 + alloc_size;
                ctx.mmu.write32(next_block + 0x18, 0x46520000);
                ctx.mmu.write32(next_block + 0x1C, size - alloc_size - 32);
            } else {
                alloc_size = size;
            }
            
            ctx.mmu.write32(current_block + 0x18, 0x73730000); 
            ctx.mmu.write32(current_block + 0x1C, alloc_size);
            
            uint32_t payload_ptr = current_block + 32;
            std::cout << "[HLE Memory] Allocated " << std::dec << req_size << " bytes at 0x" << std::hex << payload_ptr << " from handle " << std::dec << handle << "\n";
            ctx.gpr[3] = payload_ptr;
            return;
        }
        
        current_block += 32 + size;
    }
    
    std::cerr << "[HLE Memory] ERROR: OSAllocFromHeap OOM handle " << std::dec << handle << "\n";
    ctx.gpr[3] = 0;
}

void OSFreeToHeap(CPUContext& ctx) {
    int handle = ctx.gpr[3];
    uint32_t ptr = ctx.gpr[4];
    if (ptr == 0) return;
    if (handle < 0 || handle >= MAX_HEAPS || !g_heaps[handle].active) return;
    
    uint32_t block_addr = ptr - 32;
    if (block_addr < g_heaps[handle].start_addr || block_addr >= g_heaps[handle].end_addr) {
        std::cerr << "[HLE Memory] WARNING: free out of bounds block at 0x" << std::hex << ptr << "\n";
        return;
    }
    
    uint32_t magic = ctx.mmu.read32(block_addr + 0x18);
    
    if ((magic >> 16) == 0x7373) {
        ctx.mmu.write32(block_addr + 0x18, 0x46520000);
        std::cout << "[HLE Memory] Freed block at 0x" << std::hex << ptr << " to handle " << std::dec << handle << "\n";
    } else {
        std::cerr << "[HLE Memory] WARNING: free invalid block at 0x" << std::hex << ptr << "\n";
    }
}

void OSCheckHeap(CPUContext& ctx) {
    int handle = ctx.gpr[3];
    if (handle < 0 || handle >= MAX_HEAPS || !g_heaps[handle].active) {
        ctx.gpr[3] = 0xFFFFFFFF;
        return;
    }
    ctx.gpr[3] = 0;
}

} 

