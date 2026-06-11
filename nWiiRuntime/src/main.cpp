#include <iostream>
#include "runtime/cpu_context.h"
#include "runtime/config.h"

namespace nwii::runtime {

MMU* g_mmu = nullptr;

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
    ctx.mmu.mem1[0x24 + 0] = 0x00;
    ctx.mmu.mem1[0x24 + 1] = 0x00;
    ctx.mmu.mem1[0x24 + 2] = 0x00;
    ctx.mmu.mem1[0x24 + 3] = 0x02; // Console Type: Wii Retail

    ctx.mmu.mem1[0x28 + 0] = (24u * 1024u * 1024u) >> 24;
    ctx.mmu.mem1[0x28 + 1] = ((24u * 1024u * 1024u) >> 16) & 0xFF;
    ctx.mmu.mem1[0x28 + 2] = ((24u * 1024u * 1024u) >> 8) & 0xFF;
    ctx.mmu.mem1[0x28 + 3] = (24u * 1024u * 1024u) & 0xFF;
    
    ctx.mmu.mem1[0x30 + 0] = arena_lo >> 24;
    ctx.mmu.mem1[0x30 + 1] = (arena_lo >> 16) & 0xFF;
    ctx.mmu.mem1[0x30 + 2] = (arena_lo >> 8) & 0xFF;
    ctx.mmu.mem1[0x30 + 3] = arena_lo & 0xFF;

    ctx.mmu.mem1[0x34 + 0] = 0x81;
    ctx.mmu.mem1[0x34 + 1] = 0x80;
    ctx.mmu.mem1[0x34 + 2] = 0x00;
    ctx.mmu.mem1[0x34 + 3] = 0x00;
    
    // Initialize OS Globals (MEM2/Wii)
    uint32_t mem2_size = __builtin_bswap32(64 * 1024 * 1024);
    std::memcpy(ctx.mmu.mem1.data() + 0x3118, &mem2_size, 4);
    std::memcpy(ctx.mmu.mem1.data() + 0x311C, &mem2_size, 4);

    // Simulated MEM2 Arena (0x90000000 to 0x93E00000)
    uint32_t arena2_lo = __builtin_bswap32(0x90000000);
    uint32_t arena2_hi = __builtin_bswap32(0x93E00000);
    std::memcpy(ctx.mmu.mem1.data() + 0x3124, &arena2_lo, 4);
    std::memcpy(ctx.mmu.mem1.data() + 0x3128, &arena2_hi, 4);
    
    // IOS IPC Arena
    ctx.mmu.mem1[0x3130 + 0] = 0x93;
    ctx.mmu.mem1[0x3130 + 1] = 0xE0;
    ctx.mmu.mem1[0x3130 + 2] = 0x00;
    ctx.mmu.mem1[0x3130 + 3] = 0x00;

    ctx.mmu.mem1[0x3134 + 0] = 0x94;
    ctx.mmu.mem1[0x3134 + 1] = 0x00;
    ctx.mmu.mem1[0x3134 + 2] = 0x00;
    ctx.mmu.mem1[0x3134 + 3] = 0x00;

    // Memory Sizes
    uint32_t mem1_size = __builtin_bswap32(24 * 1024 * 1024); // 24MB
    std::memcpy(ctx.mmu.mem1.data() + 0x28, &mem1_size, 4);
    std::memcpy(ctx.mmu.mem1.data() + 0x3118, &mem2_size, 4);
    std::memcpy(ctx.mmu.mem1.data() + 0x311C, &mem2_size, 4);
    
    // Bus/CPU Frequency
    ctx.mmu.write32(0x800000F8u, 243'000'000u); // Bus Frequency = 243 MHz
    ctx.mmu.write32(0x800000FCu, 729'000'000u); // CPU Frequency = 729 MHz
    
    std::cout << "[DEBUG] Before DOL load, mem1[0x24] = " << std::hex << *(uint32_t*)&ctx.mmu.mem1[0x24] << "\n";
    std::cout << "[DEBUG] Before DOL load, mem1[0x3118] = " << std::hex << *(uint32_t*)&ctx.mmu.mem1[0x3118] << "\n";

    // ConsoleType was already set to 0x00000002 (Wii Retail) above - do NOT overwrite it here!

    std::cout << "[DEBUG] Before Game Run, mem1[0x24] = " << std::hex << *(uint32_t*)&ctx.mmu.mem1[0x24] << "\n";
    std::cout << "[DEBUG] Before Game Run, mem1[0x3118] = " << std::hex << *(uint32_t*)&ctx.mmu.mem1[0x3118] << "\n";
    
    nwii::runtime::g_mmu = &ctx.mmu;
    nwii::runtime::init_ipc_client(ctx);
    run_game(ctx);
    
    nwii::runtime::shutdown();
    return 0;
}
