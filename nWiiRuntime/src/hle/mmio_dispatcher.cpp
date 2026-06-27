#include "runtime/mmio_dispatcher.h"
#include <iostream>
#include <algorithm>

namespace nwii::runtime {

void MMIODispatcher::register_region(uint32_t start, uint32_t end, MMIORegionReadCallback read_cb, MMIORegionWriteCallback write_cb) {
    m_regions.push_back({start, end, std::move(read_cb), std::move(write_cb)});
    // Optional: Sort regions for faster lookup (binary search)
    // std::sort(m_regions.begin(), m_regions.end(), [](const MMIORegion& a, const MMIORegion& b) {
    //     return a.start_addr < b.start_addr;
    // });
}

uint32_t MMIODispatcher::read32(uint32_t addr) {
    for (const auto& region : m_regions) {
        if (addr >= region.start_addr && addr <= region.end_addr) {
            if (region.read_cb) return region.read_cb(addr);
            return 0;
        }
    }
    std::cout << "[MMIO] Unhandled read32 at 0x" << std::hex << addr << std::dec << std::endl;
    return 0;
}

void MMIODispatcher::write32(uint32_t addr, uint32_t val) {
    for (const auto& region : m_regions) {
        if (addr >= region.start_addr && addr <= region.end_addr) {
            if (region.write_cb) {
                region.write_cb(addr, val);
            }
            return;
        }
    }
    std::cout << "[MMIO] Unhandled write32 at 0x" << std::hex << addr << " val=0x" << val << std::dec << std::endl;
}

void MMIODispatcher::clear() {
    m_regions.clear();
}

} // namespace nwii::runtime
