#include <iostream>
#include <string>
#include "loader/loader.h"
#include "analyzer/analyzer.h"
#include "recompiler/recompiler.h"

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: nwiirecomp <unpacked_game_dir> [symbols.csv]\n";
        return 1;
    }

    std::string input_dir = argv[1];
    std::string symbols_path = (argc >= 3) ? argv[2] : "";
    std::cout << "NWiiRecomp: loading game from " << input_dir << "\n";

    nwii::loader::Executable exec;
    if (!exec.load_unpacked_game(input_dir)) {
        std::cerr << "Failed to load executable.\n";
        return 1;
    }

    std::cout << "Successfully loaded DOL.\n";
    std::cout << "Entry point: 0x" << std::hex << (uint32_t)exec.entry_point << std::dec << "\n";
    std::cout << "Sections loaded: " << exec.sections.size() << "\n";

    std::cout << "Running Code Analyzer...\n";
    nwii::analyzer::Analyzer analyzer(exec);
    analyzer.analyze();

    const auto& functions = analyzer.get_functions();
    std::cout << "Analysis complete. Discovered " << functions.size() << " functions.\n";

    // Uncomment to print all functions (can be very long)
    /*
    for (const auto& [start_addr, func] : functions) {
        std::cout << "Function at 0x" << std::hex << start_addr 
                  << " to 0x" << func.end_address << std::dec << "\n";
    }
    */

    nwii::recomp::SymbolTable symbols;
    bool has_symbols = false;
    if (!symbols_path.empty()) {
        std::cout << "Loading symbols from " << symbols_path << "...\n";
        if (symbols.load_csv(symbols_path)) {
            std::cout << "Symbols loaded successfully.\n";
            has_symbols = true;
        } else {
            std::cerr << "Failed to load symbols.\n";
        }
    }

    std::cout << "Generating CMake Project...\n";
    nwii::recomp::Recompiler recompiler(analyzer, has_symbols ? &symbols : nullptr);
    std::string runtime_path = "/Users/vovavovchok/NWiiRecomp/nWiiRuntime";
    if (recompiler.generate_cmake_project("export", runtime_path)) {
        std::cout << "Successfully exported standalone project to 'export' directory!\n";
    } else {
        std::cerr << "Failed to export project.\n";
    }

    return 0;
}

