#include "input/input_manager.h"
#include "input/sources/gamepad_source.h"
#include "input/sources/mouse_keyboard_source.h"
#include "loader/loader.h"
#include "runtime/config.h"
#include "runtime/cpu_context.h"
#include <atomic>
#include <iostream>
#include <raylib.h>
#include <thread>

// Forward declarations of GX FIFO processing
extern void ProcessGXFifo();

#include "runtime/hw/hw.h"

namespace nwii::runtime {
MMU *g_mmu = nullptr;
CPUContext *g_ctx_ptr = nullptr;

bool init() {
  Config::get().load("config.toml");
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
    std::cerr << "Usage: " << argv[0] << " <path_to_unpacked_game_dir>\n";
    return 1;
  }

  if (!nwii::runtime::init())
    return 1;
  nwii::runtime::Config::get().game_dir = argv[1];

  // Register input sources
  nwii::runtime::input::InputManager::get().add_source(
      std::make_unique<nwii::runtime::input::MouseKeyboardSource>());
  nwii::runtime::input::InputManager::get().add_source(
      std::make_unique<nwii::runtime::input::GamepadSource>());

  // Initialize graphics context FIRST in the main thread
  InitWindow(nwii::runtime::Config::get().window_width,
             nwii::runtime::Config::get().window_height, "NWiiRecomp");
  SetTargetFPS(60);

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

  std::cout << "DOL LOADED! Value at 0x80528A30: 0x" << std::hex
            << ctx->mmu.read32(0x80528A30) << std::endl;
  std::cout << "Value at 0x8052A588 (r13 - 16792): 0x" << std::hex
            << ctx->mmu.read32(0x8052A588) << std::endl;
  std::cout << "Console Type at 0x80000024 (read32): 0x" << std::hex
            << ctx->mmu.read32(0x80000024) << std::endl;
  std::cout << "Console Type at 0x80000024 (mem1): 0x" << std::hex
            << (int)ctx->mmu.mem1[0x24] << (int)ctx->mmu.mem1[0x25]
            << (int)ctx->mmu.mem1[0x26] << (int)ctx->mmu.mem1[0x27]
            << std::endl;

  std::cout << "[DEBUG] Hash Table at 0x8048E088:" << std::endl;
  for (int i = 0; i < 256; i++) {
    uint32_t ptr = ctx->mmu.read32(0x8048E088 + i * 4);
    if (ptr != 0) {
      std::cout << "  Index " << std::dec << i << " -> 0x" << std::hex << ptr
                << std::endl;
    }
  }
  arena_lo = (arena_lo + 31) & ~31;

  uint32_t console_type = 0x10000010;    // Wii Retail 16
  uint32_t mem1_size = 24 * 1024 * 1024; // Wii has 24MB MEM1
  uint32_t mem2_size = 64 * 1024 * 1024;

  // 0x24: Simulated MEM1 Size
  ctx->mmu.write32(0x80000024, mem1_size);

  // 0x28: Physical MEM1 Size
  ctx->mmu.write32(0x80000028, mem1_size);

  // 0x2C: Console Type
  ctx->mmu.write32(0x8000002C, console_type);

  // 0x3118: Simulated MEM1 Size
  ctx->mmu.write32(0x80003118, mem1_size);

  // 0x311C: Physical MEM2 Size
  ctx->mmu.write32(0x8000311C, mem2_size);

  // 0x3124 - 0x3134: MEM2 bounds
  ctx->mmu.write32(0x80003124, 0x90000000);
  ctx->mmu.write32(0x80003128, 0x93e00000);
  ctx->mmu.write32(0x80003130, 0x93e00000);
  ctx->mmu.write32(0x80003134, 0x94000000);

  // 0x3158: Hardware Revision (Wii Hollywood)
  ctx->mmu.write32(0x80003158, 0x11110000);

  ctx->mmu.write32(0x80000030, arena_lo);
  ctx->mmu.write32(0x80000034, 0x81700000); // Set OS_MEM1_ARENA_HI

  // Initial SP
  ctx->gpr[1] = 0x816FFFF0;

  if (nwii::runtime::Config::get().platform ==
      nwii::runtime::Platform::GameCube) {
    ctx->mmu.write32(0x800000F8, 40500000);  // OS_TIMER_CLOCK
    ctx->mmu.write32(0x800000FC, 162000000); // OS_BUS_CLOCK
  } else {
    ctx->mmu.write32(0x800000F8, 60750000);  // OS_TIMER_CLOCK
    ctx->mmu.write32(0x800000FC, 243000000); // OS_BUS_CLOCK
  }

  nwii::runtime::init_ipc_client(*ctx);

  // Launch CPU emulation in a background thread
  std::thread cpu_thread(cpu_thread_func, ctx.get());

  // Main Raylib/GPU Thread Loop
  while (!WindowShouldClose() && ctx->is_running) {
    // Poll Inputs exactly once per frame
    nwii::runtime::input::InputManager::get().update();

    BeginDrawing();
    ClearBackground(BLACK);

    // Drain GX FIFO and issue OpenGL/rlgl calls safely in the main thread
    ProcessGXFifo();

    EndDrawing();
  }

  // Teardown
  ctx->is_running = false;
  ctx->pc = 0; // Trigger run_game to exit
  if (cpu_thread.joinable())
    cpu_thread.join();

  CloseWindow();
  nwii::runtime::shutdown();
  return 0;
}
