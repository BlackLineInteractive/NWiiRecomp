#include "runtime/virtual_disc.h"
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;

namespace nwii::runtime {

VirtualDisc& VirtualDisc::get() {
    static VirtualDisc instance;
    return instance;
}

static std::vector<uint8_t> read_file(const fs::path& p) {
    std::vector<uint8_t> data;
    std::ifstream f(p, std::ios::binary | std::ios::ate);
    if (!f.is_open())
        return data;
    data.resize((size_t)f.tellg());
    f.seekg(0);
    f.read(reinterpret_cast<char*>(data.data()), data.size());
    return data;
}

static uint32_t be32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

void VirtualDisc::add_region(uint64_t offset, uint64_t size,
                             const std::string& path) {
    if (size == 0)
        return;
    m_regions.push_back({offset, size, path});
}

bool VirtualDisc::init(const std::string& game_dir) {
    if (m_init_done)
        return m_valid;
    m_init_done = true;
    m_dir = game_dir;

    fs::path root(game_dir);
    fs::path sys = root / "sys";
    if (!fs::exists(sys / "fst.bin") || !fs::exists(sys / "boot.bin")) {
        // Also accept a flat layout (boot.bin next to main.dol)
        if (fs::exists(root / "fst.bin") && fs::exists(root / "boot.bin")) {
            sys = root;
        } else {
            return false;
        }
    }

    m_boot_data = read_file(sys / "boot.bin");
    m_fst_data = read_file(sys / "fst.bin");
    if (m_boot_data.size() < 0x440 || m_fst_data.empty()) {
        std::cout << "[VDisc] boot.bin/fst.bin too small, ignoring" << std::endl;
        return false;
    }

    // Disc magics: Wii @0x18 = 0x5D1C9EA3, GC @0x1C = 0xC2339F3D
    m_is_wii = be32(&m_boot_data[0x18]) == 0x5D1C9EA3;
    int shift = m_is_wii ? 2 : 0;

    uint64_t dol_off = (uint64_t)be32(&m_boot_data[0x420]) << shift;
    uint64_t fst_off = (uint64_t)be32(&m_boot_data[0x424]) << shift;
    uint64_t fst_size = (uint64_t)be32(&m_boot_data[0x428]) << shift;

    add_region(0, 0x440, (sys / "boot.bin").string());
    if (fs::exists(sys / "bi2.bin"))
        add_region(0x440, fs::file_size(sys / "bi2.bin"),
                   (sys / "bi2.bin").string());
    if (fs::exists(sys / "apploader.img"))
        add_region(0x2440, fs::file_size(sys / "apploader.img"),
                   (sys / "apploader.img").string());
    if (dol_off && fs::exists(sys / "main.dol"))
        add_region(dol_off, fs::file_size(sys / "main.dol"),
                   (sys / "main.dol").string());
    if (fst_off)
        add_region(fst_off, fst_size ? fst_size : m_fst_data.size(),
                   (sys / "fst.bin").string());

    parse_fst();

    std::sort(m_regions.begin(), m_regions.end(),
              [](const Region& a, const Region& b) { return a.offset < b.offset; });

    m_valid = true;
    std::cout << "[VDisc] Extracted disc: " << (m_is_wii ? "Wii" : "GC")
              << ", " << m_regions.size() << " regions, FST "
              << m_fst_data.size() << " bytes" << std::endl;
    return true;
}

void VirtualDisc::parse_fst() {
    const uint8_t* fst = m_fst_data.data();
    size_t fst_len = m_fst_data.size();
    if (fst_len < 12)
        return;

    uint32_t num_entries = be32(fst + 8);
    if ((uint64_t)num_entries * 12 > fst_len)
        return;
    const char* strings = reinterpret_cast<const char*>(fst + num_entries * 12);
    size_t strings_len = fst_len - num_entries * 12;
    int shift = m_is_wii ? 2 : 0;

    fs::path files_root = fs::path(m_dir) / "files";

    // Directory stack: (last_entry_index, path)
    std::vector<std::pair<uint32_t, fs::path>> dirs;
    dirs.push_back({num_entries, files_root});

    for (uint32_t i = 1; i < num_entries; ++i) {
        while (dirs.size() > 1 && i >= dirs.back().first)
            dirs.pop_back();

        const uint8_t* e = fst + i * 12;
        bool is_dir = e[0] != 0;
        uint32_t name_off = ((uint32_t)e[1] << 16) | ((uint32_t)e[2] << 8) | e[3];
        uint32_t off_raw = be32(e + 4);
        uint32_t len = be32(e + 8);

        std::string name;
        if (name_off < strings_len)
            name = std::string(strings + name_off);

        if (is_dir) {
            dirs.push_back({len, dirs.back().second / name});
        } else {
            uint64_t off = (uint64_t)off_raw << shift;
            fs::path host = dirs.back().second / name;
            if (fs::exists(host)) {
                add_region(off, len, host.string());
            } else {
                std::cout << "[VDisc] Missing file: " << host << std::endl;
            }
        }
    }
}

bool VirtualDisc::read(uint64_t offset, uint32_t len, uint8_t* dst) {
    if (!m_valid)
        return false;
    std::memset(dst, 0, len);

    uint64_t end = offset + len;
    uint64_t covered = 0;
    for (const auto& r : m_regions) {
        if (r.offset >= end || r.offset + r.size <= offset)
            continue;
        uint64_t lo = std::max(offset, r.offset);
        uint64_t hi = std::min(end, r.offset + r.size);

        std::ifstream f(r.host_path, std::ios::binary);
        if (!f.is_open())
            continue;
        f.seekg((std::streamoff)(lo - r.offset));
        f.read(reinterpret_cast<char*>(dst + (lo - offset)), (std::streamsize)(hi - lo));
        covered += hi - lo;
    }
    // A read that matches no extracted file returns zeros; the game then
    // parses garbage. Surface it so disc-layout gaps are visible.
    if (covered < len) {
        static int miss_budget = 40;
        if (miss_budget > 0) {
            miss_budget--;
            std::cout << "[VDisc] MISS: read off=0x" << std::hex << offset
                      << " len=0x" << len << " covered=0x" << covered
                      << std::dec << " (no file region)\n";
        }
    }
    return true;
}

} // namespace nwii::runtime
