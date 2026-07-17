#pragma once
#include <cstdint>
#include <functional>
#include <vector>

namespace nwii::runtime {

using MMIORegionReadCallback = std::function<uint32_t(uint32_t)>;
using MMIORegionWriteCallback = std::function<void(uint32_t, uint32_t)>;
using MMIORegionRead16Callback = std::function<uint16_t(uint32_t)>;
using MMIORegionWrite16Callback = std::function<void(uint32_t, uint16_t)>;

struct MMIORegion {
    uint32_t start_addr;
    uint32_t end_addr;
    MMIORegionReadCallback read_cb;
    MMIORegionWriteCallback write_cb;

    
    
    MMIORegionRead16Callback read16_cb;
    MMIORegionWrite16Callback write16_cb;
};

class MMIODispatcher {
public:
    static MMIODispatcher& get() {
        static MMIODispatcher instance;
        return instance;
    }

    void register_region(uint32_t start, uint32_t end, MMIORegionReadCallback read_cb, MMIORegionWriteCallback write_cb);
    void register_region(uint32_t start, uint32_t end, MMIORegionReadCallback read_cb, MMIORegionWriteCallback write_cb,
                         MMIORegionRead16Callback read16_cb, MMIORegionWrite16Callback write16_cb);
    uint32_t read32(uint32_t addr);
    void write32(uint32_t addr, uint32_t val);
    uint16_t read16(uint32_t addr);
    void write16(uint32_t addr, uint16_t val);
    void clear();

private:
    MMIODispatcher() = default;
    std::vector<MMIORegion> m_regions;
};

} 
