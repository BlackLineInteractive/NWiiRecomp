#include "runtime/hw/mmio_dispatcher.h"
#include <iostream>
#include <algorithm>

namespace nwii::runtime {

void MMIODispatcher::register_region(uint32_t start, uint32_t end, MMIORegionReadCallback read_cb, MMIORegionWriteCallback write_cb) {
    m_regions.push_back({start, end, std::move(read_cb), std::move(write_cb), nullptr, nullptr});
    // Optional: Sort regions for faster lookup (binary search)
    // std::sort(m_regions.begin(), m_regions.end(), [](const MMIORegion& a, const MMIORegion& b) {
    //     return a.start_addr < b.start_addr;
    // });
}

void MMIODispatcher::register_region(uint32_t start, uint32_t end, MMIORegionReadCallback read_cb, MMIORegionWriteCallback write_cb,
                                     MMIORegionRead16Callback read16_cb, MMIORegionWrite16Callback write16_cb) {
    m_regions.push_back({start, end, std::move(read_cb), std::move(write_cb),
                         std::move(read16_cb), std::move(write16_cb)});
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

uint16_t MMIODispatcher::read16(uint32_t addr) {
    for (const auto& region : m_regions) {
        if (addr >= region.start_addr && addr <= region.end_addr) {
            if (region.read16_cb) return region.read16_cb(addr);
            if (region.read_cb) return (uint16_t)region.read_cb(addr);
            return 0;
        }
    }
    std::cout << "[MMIO] Unhandled read16 at 0x" << std::hex << addr << std::dec << std::endl;
    return 0;
}

void MMIODispatcher::write16(uint32_t addr, uint16_t val) {
    for (const auto& region : m_regions) {
        if (addr >= region.start_addr && addr <= region.end_addr) {
            if (region.write16_cb) region.write16_cb(addr, val);
            else if (region.write_cb) region.write_cb(addr, val);
            return;
        }
    }
    std::cout << "[MMIO] Unhandled write16 at 0x" << std::hex << addr << " val=0x" << val << std::dec << std::endl;
}

void MMIODispatcher::clear() {
    m_regions.clear();
}

} // namespace nwii::runtime
