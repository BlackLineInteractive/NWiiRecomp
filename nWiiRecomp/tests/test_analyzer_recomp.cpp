#include "loader/loader.h"
#include "analyzer/analyzer.h"
#include "recompiler/recompiler.h"
#include "recompiler/symbols.h"
#include <iostream>
#include <vector>
#include <cstdint>
#include <cassert>
#include <string>

// Helper to swap endianness for the raw instructions (since Wii is Big Endian)
uint32_t bswap(uint32_t val) {
    return ((val & 0xFF000000) >> 24) |
           ((val & 0x00FF0000) >> 8) |
           ((val & 0x0000FF00) << 8) |
           ((val & 0x000000FF) << 24);
}

int main() {
    std::cout << "--- nWiiRecomp: Analyzer & Recompiler Test ---\n\n";

    // 1. Manually construct a valid Executable structure
    nwii::loader::Executable exec;
    exec.entry_point = 0x80001000;
    
    // Create a dummy .text section
    nwii::loader::Section text_section;
    text_section.address = 0x80001000;
    text_section.is_text = true;
    
    // Write some PowerPC instructions (Big Endian)
    // 0x80001000: stwu r1, -16(r1)   (94 21 FF F0)
    // 0x80001004: subf r3, r4, r5    (7C 64 28 50)
    // 0x80001008: rlwinm r6, r7, 4, 0, 31 (54 E6 20 3E)
    // 0x8000100C: lwzx r8, r9, r10   (7D 09 50 2E)
    // 0x80001010: bl 0x80001018      (48 00 00 09)
    // 0x80001014: blr                (4E 80 00 20)
    // 0x80001018: blr                (4E 80 00 20)  (Target of bl)
    
    std::vector<uint32_t> instructions = {
        0x9421FFF0,
        0x7C642850,
        0x54E6203E,
        0x7D09502E,
        0x48000009,
        0x4E800020,
        0x4E800020
    };
    
    // Copy into section data, swapping to Big Endian (as if read from disk)
    text_section.data.resize(instructions.size() * 4);
    for (size_t i = 0; i < instructions.size(); ++i) {
        uint32_t swapped = bswap(instructions[i]);
        std::memcpy(text_section.data.data() + i * 4, &swapped, 4);
    }
    
    text_section.size = text_section.data.size();
    exec.sections.push_back(text_section);
    
    // 2. Setup Symbols to test HLE detection
    nwii::recomp::SymbolTable symbols;
    symbols.add_symbol(0x80001000, "GameMain");
    symbols.add_symbol(0x80001018, "OSReport"); // HLE Hook!
    
    // 3. Run Analyzer
    std::cout << "[1] Running Analyzer...\n";
    nwii::analyzer::Analyzer analyzer(exec);
    analyzer.analyze();
    
    const auto& functions = analyzer.get_functions();
    std::cout << "Analyzer found " << functions.size() << " functions.\n\n";
    assert(functions.size() == 2 && "Analyzer should find GameMain and OSReport");
    
    // 4. Run Recompiler
    std::cout << "[2] Running Recompiler...\n";
    nwii::recomp::Recompiler recompiler(analyzer, &symbols);
    
    std::string test_output_dir = "test_export";
    if (recompiler.generate_cmake_project(test_output_dir, "/Users/vovavovchok/NWiiRecomp/nWiiRuntime")) {
        std::cout << "Recompiler generated C++ successfully.\n\n";
    } else {
        std::cerr << "Recompiler failed!\n";
        return 1;
    }
    
    // 5. Let's print the generated C++ code directly for the user to see!
    std::cout << "[3] Generated Output:\n";
    std::cout << "========================================================\n";
    std::string cpp_source = recompiler.generate_function_cpp(functions.at(0x80001000));
    std::cout << cpp_source;
    std::cout << "========================================================\n\n";
    std::cout << "Test passed!\n";
    
    return 0;
}
