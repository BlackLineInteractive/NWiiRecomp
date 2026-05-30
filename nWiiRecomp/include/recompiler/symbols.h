#pragma once

#include <string>
#include <map>
#include <cstdint>

namespace nwii {
namespace recomp {

class SymbolTable {
public:
    bool load_csv(const std::string& filepath);
    
    std::string get_symbol(uint32_t address) const;
    bool has_symbol(uint32_t address) const;
    void add_symbol(uint32_t address, const std::string& name);

private:
    std::map<uint32_t, std::string> symbols_;
};

} // namespace recomp
} // namespace nwii
