#include <iostream>
#include "runtime/cpu_context.h"
#include "runtime/config.h"

namespace nwii::runtime {

bool init() {
    std::cout << "init runtime" << std::endl;
    Config::get().load("config.toml");
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
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <path_to_unpacked_game_dir>\n";
        return 1;
    }

    if (!nwii::runtime::init()) return 1;
    
    nwii::runtime::Config::get().game_dir = argv[1];
    
    std::cout << "nWiiRecomp: Standalone app started." << std::endl;
    
    nwii::runtime::CPUContext ctx;
    
    // Load DOL
    nwii::loader::Executable exe;
    if (!exe.load_unpacked_game(argv[1])) {
        std::cerr << "Failed to load game DOL from " << argv[1] << "\n";
        return 1;
    }
    
    uint32_t arena_lo = 0x80000000;
    for (const auto& sec : exe.sections) {
        uint32_t end_addr = sec.address + sec.size;
        if (end_addr > arena_lo) {
            arena_lo = end_addr;
        }
        if (sec.is_bss) continue; // BSS is already zeroed in MMU
        for (size_t i = 0; i < sec.size; ++i) {
            ctx.mmu.write8(sec.address + i, sec.data[i]);
        }
    }
    
    // Align ArenaLo to 32 bytes
    arena_lo = (arena_lo + 31) & ~31;
    
    std::cout << "DOL loaded into memory. ArenaLo: 0x" << std::hex << arena_lo << std::dec << std::endl;
    
    // Initialize OS Globals (MEM1)
    // 0x80000028 = Physical Memory Size (24MB)
    ctx.mmu.write32(0x80000028, 24 * 1024 * 1024);
    // 0x80000030 = ArenaLo
    ctx.mmu.write32(0x80000030, arena_lo);
    // 0x80000034 = ArenaHi (End of 24MB MEM1)
    ctx.mmu.write32(0x80000034, 0x81800000);
    
    // Initialize OS Globals (MEM2) for Wii
    // 0x80000310 = Physical MEM2 Size (64MB)
    ctx.mmu.write32(0x80000310, 64 * 1024 * 1024);
    // 0x80000314 = MEM2 ArenaLo
    ctx.mmu.write32(0x80000314, 0x90000000);
    // 0x80000318 = MEM2 ArenaHi (reduced to leave 16MB for IPC)
    ctx.mmu.write32(0x80000318, 0x93000000);
    
    // Initialize OS Globals (IPC)
    // 0x80003130 = IPC ArenaLo
    ctx.mmu.write32(0x80003130, 0x93000000);
    // 0x80003134 = IPC ArenaHi
    ctx.mmu.write32(0x80003134, 0x94000000);
    // Bus Frequency = 243 MHz
    ctx.mmu.write32(0x800000F8, 243000000);
    // CPU Frequency = 729 MHz
    ctx.mmu.write32(0x800000FC, 729000000);
    
    run_game(ctx);
    
    nwii::runtime::shutdown();
    return 0;
}
