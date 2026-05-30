#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace nwii {
namespace recompiler {

struct Symbol {
    std::string name;
    uint32_t address;
    uint32_t size;
    std::string type;
};

class SymbolMap {
public:
    bool load_ghidra_csv(const std::string& filepath);
    
    const Symbol* get_symbol_by_address(uint32_t address) const;
    const std::vector<Symbol>& get_all_symbols() const { return symbols; }

private:
    std::vector<Symbol> symbols;
};

} // namespace recompiler
} // namespace nwii
