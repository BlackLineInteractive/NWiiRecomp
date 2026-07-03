#include "input/input_manager.h"
#include "input/sources/gamepad_source.h"
#include "input/sources/mouse_keyboard_source.h"
#include "loader/loader.h"
#include "runtime/config.h"
#include "runtime/cpu_context.h"
#include "runtime/virtual_disc.h"
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <raylib.h>
#include <thread>
#include <vector>

// Forward declarations of GX FIFO processing
extern void ProcessGXFifo();

#include "runtime/hw/hw.h"

namespace nwii::runtime {
MMU *g_mmu = nullptr;
CPUContext *g_ctx_ptr = nullptr;

bool init(const char *config_path = "config.toml") {
  Config::get().load(config_path);
  hw::register_all_hw(MMIODispatcher::get());
  return true;
}
void shutdown() {}
} // namespace nwii::runtime

extern "C" void run_game(nwii::runtime::CPUContext &ctx);

// CPU Execution Thread
void cpu_thread_func(nwii::runtime::CPUContext *ctx) {
  nwii::runtime::g_ctx_ptr = ctx;
  std::cout << "[Thread] CPU Core started." << std::endl;
  run_game(*ctx);
  std::cout << "[Thread] CPU Core exited. Final PC: 0x" << std::hex << ctx->pc
            << ", LR: 0x" << ctx->lr << std::dec << std::endl;
}

int main(int argc, char **argv) {
  if (argc < 2) {
    std::cerr << "Usage: " << argv[0]
              << " <path_to_unpacked_game_dir> [config_path]\n";
    return 1;
  }

  const char *config_path = (argc >= 3) ? argv[2] : "config.toml";
  if (!nwii::runtime::init(config_path))
    return 1;
  nwii::runtime::Config::get().game_dir = argv[1];

  // Register input sources
  nwii::runtime::input::InputManager::get().add_source(
      std::make_unique<nwii::runtime::input::MouseKeyboardSource>());
  nwii::runtime::input::InputManager::get().add_source(
      std::make_unique<nwii::runtime::input::GamepadSource>());

  // NWII_HEADLESS=1 runs without a window (log-driven testing, CI)
  const char *headless_env = std::getenv("NWII_HEADLESS");
  bool headless = headless_env && headless_env[0] == '1';

  // Initialize graphics context FIRST in the main thread
  if (!headless) {
    InitWindow(nwii::runtime::Config::get().window_width,
               nwii::runtime::Config::get().window_height, "NWiiRecomp");
    SetTargetFPS(60);
  }

  // Context allocation
  auto ctx = std::make_unique<nwii::runtime::CPUContext>();
  nwii::runtime::g_mmu = &ctx->mmu;

  // Load DOL
  nwii::loader::Executable exe;
  if (!exe.load_unpacked_game(argv[1]))
    return 1;

  uint32_t arena_lo = 0x80000000;
  // First pass: Clear BSS sections
  for (const auto &sec : exe.sections) {
    uint32_t end_addr = sec.address + sec.size;
    if (end_addr > arena_lo)
      arena_lo = end_addr;

    if (sec.is_bss) {
      for (size_t i = 0; i < sec.size; ++i)
        ctx->mmu.write8(sec.address + i, 0);
    }
  }

  // Second pass: Load Data and Text sections (overwriting BSS if overlapping)
  for (const auto &sec : exe.sections) {
    if (!sec.is_bss) {
      for (size_t i = 0; i < sec.size; ++i)
        ctx->mmu.write8(sec.address + i, sec.data[i]);
    }
  }

  arena_lo = (arena_lo + 31) & ~31;

  bool is_gc = nwii::runtime::Config::get().platform ==
               nwii::runtime::Platform::GameCube;

  // Extracted-disc support: place the FST at the top of MEM1 and publish
  // disc header + FST location in low memory, like the apploader does.
  uint32_t arena_hi = 0x81700000;
  auto &vdisc = nwii::runtime::VirtualDisc::get();
  if (vdisc.init(argv[1])) {
    const auto &boot = vdisc.boot_data();
    // Disc header (game ID, magics) at 0x80000000
    for (size_t i = 0; i < 0x20 && i < boot.size(); ++i)
      ctx->mmu.write8(0x80000000 + (uint32_t)i, boot[i]);

    const auto &fst = vdisc.fst_data();
    uint32_t fst_size = (uint32_t)fst.size();
    uint32_t fst_addr = (0x81800000 - fst_size) & ~63u;
    for (uint32_t i = 0; i < fst_size; ++i)
      ctx->mmu.write8(fst_addr + i, fst[i]);

    ctx->mmu.write32(0x80000038, fst_addr); // FST base
    ctx->mmu.write32(0x8000003C, fst_size); // FST max size
    if (fst_addr < 0x81700000)
      arena_hi = fst_addr & ~31u;
    std::cout << "[Loader] FST loaded at 0x" << std::hex << fst_addr
              << " size 0x" << fst_size << std::dec << std::endl;
  }

  // Console type (Dolphin BootManager): Wii retail Hollywood = 0x10000006,
  // GameCube retail (latest production board) = 0x00000003.
  uint32_t console_type = is_gc ? 0x00000003 : 0x10000006;
  uint32_t mem1_size = 24 * 1024 * 1024; // 24MB MEM1
  uint32_t mem2_size = 64 * 1024 * 1024; // 64MB MEM2

  ctx->mmu.write32(0x80000020, is_gc ? 0x0D15EA5E : console_type); // GC boot magic
  // 0x24: __OSSimulatedMemSize (MEM1)
  ctx->mmu.write32(0x80000024, mem1_size);
  // 0x28: __OSPhysMemSize (MEM1)
  ctx->mmu.write32(0x80000028, mem1_size);
  ctx->mmu.write32(0x8000002C, console_type);
  // Arena bounds. GC (YAGCD): 0x30 = ArenaLo, 0x34 = ArenaHi.
  // The RVL SDK build of SHSM reads its MEM1 arena lo from 0x34 instead.
  ctx->mmu.write32(0x80000030, arena_lo);
  ctx->mmu.write32(0x80000034, is_gc ? arena_hi : arena_lo);
  if (!vdisc.valid()) {
    // No extracted disc: keep legacy behaviour of 0x38 = top of usable MEM1
    ctx->mmu.write32(0x80000038, arena_hi);
  }
  if (is_gc) {
    // BI2 pointer (0x800000F4): copy bi2.bin below the FST if present
    std::filesystem::path bi2_path =
        std::filesystem::path(argv[1]) / "sys" / "bi2.bin";
    if (std::filesystem::exists(bi2_path)) {
      std::ifstream bi2(bi2_path, std::ios::binary);
      std::vector<char> bi2_data((std::istreambuf_iterator<char>(bi2)),
                                 std::istreambuf_iterator<char>());
      uint32_t bi2_addr = (arena_hi - (uint32_t)bi2_data.size()) & ~31u;
      for (size_t i = 0; i < bi2_data.size(); ++i)
        ctx->mmu.write8(bi2_addr + (uint32_t)i, (uint8_t)bi2_data[i]);
      ctx->mmu.write32(0x800000F4, bi2_addr);
      arena_hi = bi2_addr;
      ctx->mmu.write32(0x80000034, arena_hi);
      std::cout << "[Loader] BI2 loaded at 0x" << std::hex << bi2_addr
                << std::dec << std::endl;
    }
  }
  // 0xF0: simulated memory size (read by OSGetConsoleSimulatedMemSize)
  ctx->mmu.write32(0x800000F0, mem1_size);

  if (!is_gc) {
    // Wii-only low-mem fields
    // 0x3118: Simulated MEM2 Size, 0x311C: Physical MEM2 Size
    ctx->mmu.write32(0x80003118, mem2_size);
    ctx->mmu.write32(0x8000311C, mem2_size);
    // 0x3124 - 0x3134: usable MEM2 bounds + IOS-reserved region
    ctx->mmu.write32(0x80003124, 0x90000800);
    ctx->mmu.write32(0x80003128, 0x93e00000);
    ctx->mmu.write32(0x80003130, 0x93e00000);
    ctx->mmu.write32(0x80003134, 0x94000000);
    // 0x3158: Hollywood hardware revision (retail = 0x00000023)
    ctx->mmu.write32(0x80003158, 0x00000023);
  }

  // Initial SP
  ctx->gpr[1] = 0x816FFFF0;

  if (is_gc) {
    ctx->mmu.write32(0x800000F8, 40500000);  // OS_TIMER_CLOCK
    ctx->mmu.write32(0x800000FC, 162000000); // OS_BUS_CLOCK
  } else {
    ctx->mmu.write32(0x800000F8, 60750000);  // OS_TIMER_CLOCK
    ctx->mmu.write32(0x800000FC, 243000000); // OS_BUS_CLOCK
  }

  nwii::runtime::init_ipc_client(*ctx);

  // Launch CPU emulation in a background thread
  std::thread cpu_thread(cpu_thread_func, ctx.get());

  if (headless) {
    // No window: only pace VBlank so the OS thread queue keeps moving
    while (ctx->is_running) {
      std::this_thread::sleep_for(std::chrono::milliseconds(16));
      ctx->vblank_pending = true;
    }
  } else {
    // Main Raylib/GPU Thread Loop
    while (!WindowShouldClose() && ctx->is_running) {
      // Poll Inputs exactly once per frame
      nwii::runtime::input::InputManager::get().update();

      BeginDrawing();
      ClearBackground(BLACK);

      // Drain GX FIFO and issue OpenGL/rlgl calls safely in the main thread
      ProcessGXFifo();

      EndDrawing();

      // Trigger VBlank interrupt to drive the OS thread queue
      ctx->vblank_pending = true;
    }
  }

  // Teardown
  ctx->is_running = false;
  ctx->pc = 0; // Trigger run_game to exit
  if (cpu_thread.joinable())
    cpu_thread.join();

  if (!headless)
    CloseWindow();
  nwii::runtime::shutdown();
  return 0;
}