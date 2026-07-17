#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace nwii::runtime {

// Serves disc reads from an extracted game directory (wit/Dolphin layout):



class VirtualDisc {
public:
    static VirtualDisc& get();

    bool init(const std::string& game_dir);
    bool valid() const { return m_valid; }

    size_t read(uint64_t offset, uint32_t len, uint8_t* dst);

    uint32_t fst_size() const { return (uint32_t)m_fst_data.size(); }
    const std::vector<uint8_t>& fst_data() const { return m_fst_data; }
    const std::vector<uint8_t>& boot_data() const { return m_boot_data; }
    bool is_wii() const { return m_is_wii; }

private:
    VirtualDisc() = default;

    struct Region {
        uint64_t offset;
        uint64_t size;
        std::string host_path;
    };

    void add_region(uint64_t offset, uint64_t size, const std::string& path);
    void parse_fst();

    bool m_valid = false;
    bool m_init_done = false;
    bool m_is_wii = false;
    std::string m_dir;
    std::vector<uint8_t> m_boot_data;
    std::vector<uint8_t> m_fst_data;
    std::vector<Region> m_regions;

    std::string m_cached_path;
    FILE* m_cached_file = nullptr;
};

} 
