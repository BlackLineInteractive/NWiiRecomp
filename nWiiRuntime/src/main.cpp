#include <iostream>
#include "runtime/cpu_context.h"

namespace nwii::runtime {

bool init() {
    std::cout << "init runtime" << std::endl;
    // TODO: setup hle, gfx, input
    return true;
}

void shutdown() {
    std::cout << "shutdown runtime\n";
}

} // namespace nwii::runtime

extern "C" void run_game(nwii::runtime::CPUContext& ctx);

#include "loader/loader.h"

// Default entry point for the standalone Mac application
int main(int argc, char** argv) {
    if (!nwii::runtime::init()) return 1;
    
    std::cout << "nWiiRecomp: Standalone app started." << std::endl;
    
    nwii::runtime::CPUContext ctx;
    
    // Load DOL
    nwii::loader::Executable exe;
    if (!exe.load_unpacked_game("../NO_GitHub/Recomp_game(NO_PUBLIK)/SHSM-Extract")) {
        std::cerr << "Failed to load game DOL\n";
        return 1;
    }
    
    for (const auto& sec : exe.sections) {
        if (sec.is_bss) continue; // BSS is already zeroed in MMU
        for (size_t i = 0; i < sec.size; ++i) {
            ctx.mmu.write8(sec.address + i, sec.data[i]);
        }
    }
    std::cout << "DOL loaded into memory." << std::endl;
    
    // Initialize OS Globals (Core / Bus Frequency)
    // Bus Frequency = 243 MHz
    ctx.mmu.write32(0x800000F8, 243000000);
    // CPU Frequency = 729 MHz
    ctx.mmu.write32(0x800000FC, 729000000);
    
    run_game(ctx);
    
    nwii::runtime::shutdown();
    return 0;
}
