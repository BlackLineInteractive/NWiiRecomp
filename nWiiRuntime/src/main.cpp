#include <iostream>

namespace nwii::runtime {

bool init() {
    std::cout << "init runtime\n";
    // TODO: setup hle, gfx, input
    return true;
}

void shutdown() {
    std::cout << "shutdown runtime\n";
}

} // namespace nwii::runtime

// Default entry point for the standalone Mac application
int main(int argc, char** argv) {
    if (!nwii::runtime::init()) return 1;
    
    std::cout << "nWiiRecomp: Standalone app started.\n";
    // TODO: Initialize CPUContext and call the game's entry point here.
    
    nwii::runtime::shutdown();
    return 0;
}
