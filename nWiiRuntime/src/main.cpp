#include <iostream>
#include "runtime/cpu_context.h"

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

extern "C" void run_game(nwii::runtime::CPUContext& ctx);

// Default entry point for the standalone Mac application
int main(int argc, char** argv) {
    if (!nwii::runtime::init()) return 1;
    
    std::cout << "nWiiRecomp: Standalone app started.\n";
    
    nwii::runtime::CPUContext ctx;
    run_game(ctx);
    
    nwii::runtime::shutdown();
    return 0;
}
