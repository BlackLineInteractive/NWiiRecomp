#include "loader/loader.h"
#include <fstream>
#include <iostream>
#include <filesystem>

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

    // Load text sections
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

    // Load data sections
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

    // Add BSS section
    if (header.bss_size > 0) {
        Section bss;
        bss.address = header.bss_address;
        bss.size = header.bss_size;
        bss.is_text = false;
        bss.is_bss = true;
        // BSS doesn't exist in the file, it's just initialized to zero at runtime.
        bss.data.clear(); 
        
        sections.push_back(std::move(bss));
    }

    return true;
}

bool Executable::load_unpacked_game(const std::string& path) {
    std::string dol_path = path;

    if (std::filesystem::is_directory(path)) {
        dol_path = path + "/sys/main.dol";
        if (!std::filesystem::exists(dol_path)) {
            dol_path = path + "/main.dol";
        }
        if (!std::filesystem::exists(dol_path)) {
            std::cerr << "Could not find main.dol in " << path << " or " << path << "/sys/" << std::endl;
            return false;
        }
    } else {
        // Assume the user directly provided the path to a .dol file
        if (!std::filesystem::exists(dol_path)) {
            std::cerr << "File does not exist: " << dol_path << std::endl;
            return false;
        }
    }

    return load_dol(dol_path);
}

} // namespace loader
} // namespace nwii
