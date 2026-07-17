#include "loader/loader.h"
#include <fstream>
#include <iostream>
#include <filesystem>
#include "runtime/config.h"

namespace nwii {
namespace loader {

bool Executable::load_dol(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Failed to open " << path << std::endl;
        return false;
    }

    DolHeader header;
    if (!file.read(reinterpret_cast<char*>(&header), sizeof(DolHeader))) {
        std::cerr << "Failed to read DOL header" << std::endl;
        return false;
    }

    entry_point = header.entry_point;

    for (int i = 0; i < 7; ++i) {
        if (header.text_offsets[i] == 0 || header.text_sizes[i] == 0) continue;

        Section sec;
        sec.address = header.text_addresses[i];
        sec.size = header.text_sizes[i];
        sec.is_text = true;
        sec.is_bss = false;
        sec.data.resize(sec.size);

        file.seekg(header.text_offsets[i], std::ios::beg);
        file.read(reinterpret_cast<char*>(sec.data.data()), sec.size);

        sections.push_back(std::move(sec));
    }

    for (int i = 0; i < 11; ++i) {
        if (header.data_offsets[i] == 0 || header.data_sizes[i] == 0) continue;

        Section sec;
        sec.address = header.data_addresses[i];
        sec.size = header.data_sizes[i];
        sec.is_text = false;
        sec.is_bss = false;
        sec.data.resize(sec.size);

        file.seekg(header.data_offsets[i], std::ios::beg);
        file.read(reinterpret_cast<char*>(sec.data.data()), sec.size);

        sections.push_back(std::move(sec));
    }

    if (header.bss_size > 0) {
        Section bss;
        bss.address = header.bss_address;
        bss.size = header.bss_size;
        bss.is_text = false;
        bss.is_bss = true;
        
        bss.data.clear(); 
        
        sections.push_back(std::move(bss));
    }

    return true;
}

bool Executable::load_rel(const std::string& path, uint32_t module_base,
                          uint32_t bss_base) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        std::cerr << "[REL] cannot open " << path << std::endl;
        return false;
    }
    size_t fsize = (size_t)file.tellg();
    file.seekg(0);
    std::vector<uint8_t> img(fsize);
    file.read(reinterpret_cast<char*>(img.data()), fsize);

    auto be32 = [&](size_t o) -> uint32_t {
        return ((uint32_t)img[o] << 24) | ((uint32_t)img[o + 1] << 16) |
               ((uint32_t)img[o + 2] << 8) | img[o + 3];
    };
    auto be16 = [&](size_t o) -> uint16_t {
        return (uint16_t)(((uint32_t)img[o] << 8) | img[o + 1]);
    };
    auto wr32 = [&](size_t o, uint32_t v) {
        img[o] = v >> 24; img[o + 1] = v >> 16; img[o + 2] = v >> 8; img[o + 3] = v;
    };
    auto wr16 = [&](size_t o, uint16_t v) { img[o] = v >> 8; img[o + 1] = v & 0xFF; };

    uint32_t num_sec = be32(0x0C), sec_off = be32(0x10);
    uint32_t imp_off = be32(0x28), imp_size = be32(0x2C);

    
    std::vector<uint32_t> sec_fileoff(num_sec), sec_size(num_sec), sec_addr(num_sec);
    for (uint32_t i = 0; i < num_sec; ++i) {
        uint32_t raw = be32(sec_off + i * 8);
        sec_fileoff[i] = raw & ~3u;
        sec_size[i] = be32(sec_off + i * 8 + 4);
        sec_addr[i] = sec_fileoff[i] ? (module_base + sec_fileoff[i]) : 0;
    }
    
    for (uint32_t i = 0; i < num_sec; ++i)
        if (sec_fileoff[i] == 0 && sec_size[i] > 0) sec_addr[i] = bss_base;

    

    
    
    enum { R_ADDR32 = 1, R_ADDR24 = 2, R_ADDR16 = 4, R_ADDR16_LO = 5,
           R_ADDR16_HI = 6, R_ADDR16_HA = 7, R_REL24 = 10, R_REL14 = 11,
           R_NOP = 201, R_SECTION = 202, R_END = 203, R_MRKREF = 204 };
    for (uint32_t e = 0; e < imp_size / 8; ++e) {
        uint32_t mod = be32(imp_off + e * 8);
        uint32_t rel = be32(imp_off + e * 8 + 4);
        uint32_t tgt_sec = 0, run = 0;
        size_t o = rel;
        while (o + 8 <= img.size()) {
            uint16_t roff = be16(o);
            uint8_t rtype = img[o + 2];
            uint8_t rsec = img[o + 3];
            uint32_t addend = be32(o + 4);
            o += 8;
            if (rtype == R_END) break;
            if (rtype == R_SECTION) { tgt_sec = rsec; run = 0; continue; }
            run += roff;
            if (rtype == R_NOP || rtype == R_MRKREF) continue;
            
            uint32_t patch_fo = sec_fileoff[tgt_sec] + run;
            uint32_t patch_addr = sec_addr[tgt_sec] + run;
            uint32_t value = (mod == 0) ? addend
                                        : (sec_addr[rsec] + addend);
            switch (rtype) {
            case R_ADDR32: wr32(patch_fo, value); break;
            case R_ADDR16:
            case R_ADDR16_LO: wr16(patch_fo, value & 0xFFFF); break;
            case R_ADDR16_HI: wr16(patch_fo, (value >> 16) & 0xFFFF); break;
            case R_ADDR16_HA:
                wr16(patch_fo, ((value >> 16) + ((value & 0x8000) ? 1 : 0)) & 0xFFFF);
                break;
            case R_ADDR24: {
                uint32_t insn = be32(patch_fo);
                wr32(patch_fo, (insn & ~0x03FFFFFCu) | (value & 0x03FFFFFC));
                break;
            }
            case R_REL24: {
                uint32_t insn = be32(patch_fo);
                uint32_t delta = value - patch_addr;
                wr32(patch_fo, (insn & ~0x03FFFFFCu) | (delta & 0x03FFFFFC));
                break;
            }
            case R_REL14: {
                uint32_t insn = be32(patch_fo);
                uint32_t delta = value - patch_addr;
                wr32(patch_fo, (insn & ~0x0000FFFCu) | (delta & 0x0000FFFC));
                break;
            }
            default: break;
            }
        }
    }

    
    
    int added = 0;
    for (uint32_t i = 0; i < num_sec; ++i) {
        bool is_exec = (be32(sec_off + i * 8) & 1) != 0;
        if (!is_exec || sec_size[i] == 0 || sec_fileoff[i] == 0) continue;
        Section sec;
        sec.address = sec_addr[i];
        sec.size = sec_size[i];
        sec.is_text = true;
        sec.is_bss = false;
        sec.data.assign(img.begin() + sec_fileoff[i],
                        img.begin() + sec_fileoff[i] + sec_size[i]);
        sections.push_back(std::move(sec));
        ++added;
        std::cout << "[REL] " << path << " text sec" << i << " @0x" << std::hex
                  << sec_addr[i] << " size 0x" << sec_size[i] << std::dec << "\n";
    }
    return added > 0;
}

bool Executable::load_unpacked_game(const std::string& path) {
    std::string dol_path = path;
    std::string boot_bin_path = "";

    if (std::filesystem::is_directory(path)) {
        dol_path = path + "/sys/main.dol";
        boot_bin_path = path + "/sys/boot.bin";
        if (!std::filesystem::exists(dol_path)) {
            dol_path = path + "/main.dol";
            boot_bin_path = path + "/boot.bin";
        }
        if (!std::filesystem::exists(dol_path)) {
            std::cerr << "Could not find main.dol in " << path << " or " << path << "/sys/" << std::endl;
            return false;
        }
    } else {
        
        if (!std::filesystem::exists(dol_path)) {
            std::cerr << "File does not exist: " << dol_path << std::endl;
            return false;
        }
    }

    if (!boot_bin_path.empty() && std::filesystem::exists(boot_bin_path)) {
        std::ifstream boot(boot_bin_path, std::ios::binary);
        if (boot.is_open()) {
            char game_id_buf[7] = {0};
            if (boot.read(game_id_buf, 6)) {
                nwii::runtime::Config::get().game_id = std::string(game_id_buf);
                std::cout << "[Loader] Parsed Game ID from boot.bin: " << nwii::runtime::Config::get().game_id << std::endl;
            }
        }
    }

    return load_dol(dol_path);
}

} 
} 
