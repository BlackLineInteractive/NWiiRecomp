#include "runtime/cpu_context.h"
#include <iostream>
#include <map>
#include <list>

using namespace nwii::runtime;

struct MemoryBlock {
    uint32_t address;
    uint32_t size;
    bool is_free;
};

class HeapManager {
public:
    uint32_t start_addr;
    uint32_t total_size;
    std::list<MemoryBlock> blocks;
    
    HeapManager() : start_addr(0), total_size(0) {}
    
    HeapManager(uint32_t start, uint32_t size) {
        start_addr = start;
        total_size = size;
        blocks.push_back({start, size, true});
    }
    
    uint32_t allocate(uint32_t size, uint32_t alignment = 32) {
        // Ensure minimum block size
        if (size == 0) size = alignment;
        
        for (auto it = blocks.begin(); it != blocks.end(); ++it) {
            if (!it->is_free) continue;
            
            uint32_t align_offset = (alignment - (it->address % alignment)) % alignment;
            if (it->size >= size + align_offset) {
                // Split for alignment padding
                if (align_offset > 0) {
                    blocks.insert(it, {it->address, align_offset, true});
                    it->address += align_offset;
                    it->size -= align_offset;
                }
                
                // Split remaining free space
                if (it->size > size) {
                    blocks.insert(std::next(it), {it->address + size, it->size - size, true});
                    it->size = size;
                }
                
                it->is_free = false;
                return it->address;
            }
        }
        return 0; // OOM
    }
    
    void free(uint32_t ptr) {
        for (auto it = blocks.begin(); it != blocks.end(); ++it) {
            if (it->address == ptr && !it->is_free) {
                it->is_free = true;
                
                // Merge with next if free
                auto next = std::next(it);
                if (next != blocks.end() && next->is_free) {
                    it->size += next->size;
                    blocks.erase(next);
                }
                
                // Merge with prev if free
                if (it != blocks.begin()) {
                    auto prev = std::prev(it);
                    if (prev->is_free) {
                        prev->size += it->size;
                        blocks.erase(it);
                    }
                }
                return;
            }
        }
        std::cerr << "[HLE Memory] WARNING: Attempted to free invalid or already freed pointer 0x" << std::hex << ptr << std::endl;
    }
};

static std::map<uint32_t, HeapManager> active_heaps;
static uint32_t next_heap_handle = 1;

extern "C" {

void OSInitAlloc(CPUContext& ctx) {
    uint32_t arena_start = ctx.gpr[3];
    uint32_t arena_end = ctx.gpr[4];
    int max_heaps = ctx.gpr[5];
    
    std::cout << "\n============================================\n";
    std::cout << "[HLE Memory] Initializing Memory Allocator...\n";
    std::cout << "  Arena Start: 0x" << std::hex << arena_start << "\n";
    std::cout << "  Arena End:   0x" << std::hex << arena_end << "\n";
    std::cout << "  Max Heaps:   " << std::dec << max_heaps << "\n";
    std::cout << "============================================\n\n";
    
    active_heaps.clear();
    next_heap_handle = 1;
}

void OSCreateHeap(CPUContext& ctx) {
    uint32_t start_addr = ctx.gpr[3];
    uint32_t size = ctx.gpr[4];
    
    uint32_t handle = next_heap_handle++;
    active_heaps[handle] = HeapManager(start_addr, size);
    
    std::cout << "[HLE Memory] Created Heap " << std::dec << handle 
              << " -> Start: 0x" << std::hex << start_addr 
              << ", Size: " << std::dec << size << " bytes" << std::endl;
              
    ctx.gpr[3] = handle; 
}

void OSAllocFromHeap(CPUContext& ctx) {
    uint32_t heap = ctx.gpr[3];
    uint32_t size = ctx.gpr[4];
    
    if (active_heaps.find(heap) == active_heaps.end()) {
        std::cerr << "[HLE Memory] ERROR: OSAllocFromHeap on invalid heap " << std::dec << heap << std::endl;
        ctx.gpr[3] = 0;
        return;
    }
    
    uint32_t allocated = active_heaps[heap].allocate(size, 32);
    
    std::cout << "[HLE Memory] Allocated from Heap " << std::dec << heap 
              << " -> Size: " << size << " bytes. Ptr: 0x" << std::hex << allocated << std::endl;
              
    ctx.gpr[3] = allocated;
}

void OSFreeToHeap(CPUContext& ctx) {
    uint32_t heap = ctx.gpr[3];
    uint32_t ptr = ctx.gpr[4];
    
    if (active_heaps.find(heap) == active_heaps.end()) {
        std::cerr << "[HLE Memory] ERROR: OSFreeToHeap on invalid heap " << std::dec << heap << std::endl;
        return;
    }
    
    active_heaps[heap].free(ptr);
    
    std::cout << "[HLE Memory] Freed from Heap " << std::dec << heap 
              << " -> Ptr: 0x" << std::hex << ptr << std::endl;
}

} // extern "C"
