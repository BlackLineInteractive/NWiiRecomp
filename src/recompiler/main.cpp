#include <iostream>
#include <string>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: nwiirecomp <input.dol/.elf/.rel> [options]\n";
        return 1;
    }

    std::string input_file = argv[1];
    std::cout << "NWiiRecomp: loading " << input_file << "\n";

    // TODO: parse binary, disassemble PPC, generate C++
    return 0;
}
