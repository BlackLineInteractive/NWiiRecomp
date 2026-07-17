#include "recompiler/symbols.h"
#include <fstream>
#include <sstream>

namespace nwii {
namespace recomp {

bool SymbolTable::load_csv(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) return false;
    
    std::string line;
    
    std::getline(file, line);
    
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        
        std::stringstream ss(line);
        std::string name, loc_str, size_str;
        
        if (std::getline(ss, name, ',') && std::getline(ss, loc_str, ',')) {
            
            if (!name.empty() && name.front() == '"') name.erase(0, 1);
            if (!name.empty() && name.back() == '"') name.pop_back();
            if (!loc_str.empty() && loc_str.front() == '"') loc_str.erase(0, 1);
            if (!loc_str.empty() && loc_str.back() == '"') loc_str.pop_back();

            if (name == "default" || name == "new" || name == "delete" || name == "class" || name == "struct" || name == "auto") {
                name += "_sym";
            }
            for (char& c : name) {
                if (!std::isalnum(c) && c != '_') {
                    c = '_';
                }
            }
            if (!name.empty() && std::isdigit(name.front())) {
                name = "_" + name;
            }
            
            try {
                uint32_t addr = std::stoul(loc_str, nullptr, 16);
                symbols_[addr] = name;
            } catch(...) {}
        }
    }
    return true;
}

std::string SymbolTable::get_symbol(uint32_t address) const {
    auto it = symbols_.find(address);
    if (it != symbols_.end()) {
        return it->second;
    }
    return "";
}

bool SymbolTable::has_symbol(uint32_t address) const {
    return symbols_.find(address) != symbols_.end();
}

void SymbolTable::add_symbol(uint32_t address, const std::string& name) {
    symbols_[address] = name;
}

} 
} 
