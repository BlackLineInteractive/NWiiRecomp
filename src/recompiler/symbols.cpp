#include "nwii/recompiler/symbols.h"
#include <fstream>
#include <sstream>
#include <iostream>

namespace nwii {
namespace recompiler {

bool SymbolMap::load_ghidra_csv(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "Failed to open symbol file: " << filepath << std::endl;
        return false;
    }

    std::string line;
    // Skip header
    std::getline(file, line);

    while (std::getline(file, line)) {
        if (line.empty()) continue;

        std::stringstream ss(line);
        std::string name, location_str, size_str, type, rest;

        // Basic CSV parsing (Ghidra export usually: Name,Location,Size,Type,...)
        // Note: Real Ghidra CSVs might have quotes, this is a basic split implementation.
        std::getline(ss, name, ',');
        std::getline(ss, location_str, ',');
        std::getline(ss, size_str, ',');
        std::getline(ss, type, ',');

        // Remove quotes if present
        if (!name.empty() && name.front() == '"') {
            name = name.substr(1, name.size() - 2);
        }

        Symbol sym;
        sym.name = name;
        sym.type = type;

        try {
            // Location might have "80" prefix in RAM for Wii (e.g., 0x80001234)
            sym.address = std::stoul(location_str, nullptr, 16);
            sym.size = size_str.empty() ? 0 : std::stoul(size_str, nullptr, 10);
            symbols.push_back(sym);
        } catch (const std::exception& e) {
            // Skip invalid lines
        }
    }

    return true;
}

const Symbol* SymbolMap::get_symbol_by_address(uint32_t address) const {
    for (const auto& sym : symbols) {
        if (sym.address == address) {
            return &sym;
        }
    }
    return nullptr;
}

} // namespace recompiler
} // namespace nwii
