#include <iostream>
#include <string>
#include "loader/loader.h"

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: nwiirecomp <input.dol> [options]\n";
        return 1;
    }

    std::string input_file = argv[1];
    std::cout << "NWiiRecomp: loading " << input_file << "\n";

    nwii::loader::Executable exec;
    if (!exec.load_dol(input_file)) {
        std::cerr << "Failed to load executable.\n";
        return 1;
    }

    std::cout << "Successfully loaded DOL.\n";
    std::cout << "Entry point: 0x" << std::hex << (uint32_t)exec.entry_point << std::dec << "\n";
    std::cout << "Sections loaded: " << exec.sections.size() << "\n";

    // TODO: parse binary, disassemble PPC, generate C++
    return 0;
}

