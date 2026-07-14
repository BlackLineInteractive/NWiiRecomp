#include "input/input_manager.h"
#include "input/sources/gamepad_source.h"
#include "input/sources/mouse_keyboard_source.h"
#include "input/sources/phone_source.h"
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
#include <rlgl.h>
#include <thread>
#include <vector>

// Forward declarations of GX FIFO processing

#include "runtime/hw/hw.h"

namespace nwii::runtime {
    namespace hw { extern uint32_t g_vi_top_field_base; }
}

namespace nwii::runtime {
uint32_t g_debug_pc = 0;
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
  std::cout << "[Thread] CPU Core started. pc=" << std::hex << ctx->pc << " is_running=" << ctx->is_running << std::endl;
  std::flush(std::cout);
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

  // Register input sources per configured mode ([input] mode in TOML)
  {
    using namespace nwii::runtime::input;
    auto& im = InputManager::get();
    int mode = nwii::runtime::Config::get().input_mode;
    switch (mode) {
    case 1: // real Wiimote over Bluetooth: not implemented yet
      std::cout << "[Input] Mode 1 (Bluetooth Wiimote) not implemented, "
                   "falling back to keyboard+mouse" << std::endl;
      im.add_source(std::make_unique<MouseKeyboardSource>());
      break;
    case 2: // gamepad as Classic/GC controller
    case 3: // gamepad + pointer assist
    case 4: // gamepad full tilt
      im.add_source(std::make_unique<GamepadSource>());
      // Keyboard stays available for menus/debugging
      im.add_source(std::make_unique<MouseKeyboardSource>());
      break;
    case 5: // smartphone over UDP
      im.add_source(std::make_unique<PhoneSource>());
      im.add_source(std::make_unique<MouseKeyboardSource>());
      break;
    case 7: // touch: raylib maps primary touch to mouse on most hosts
      std::cout << "[Input] Mode 7 (touch): using pointer-as-touch mapping"
                << std::endl;
      im.add_source(std::make_unique<MouseKeyboardSource>());
      break;
    case 6:
    default:
      im.add_source(std::make_unique<MouseKeyboardSource>());
      im.add_source(std::make_unique<GamepadSource>());
      break;
    }
  }

  // NWII_HEADLESS=1 runs without a window (log-driven testing, CI)
  const char *headless_env = std::getenv("NWII_HEADLESS");
  bool headless = headless_env && headless_env[0] == '1';

  // Initialize graphics context FIRST in the main thread
  if (!headless) {
    InitWindow(nwii::runtime::Config::get().window_width,
               nwii::runtime::Config::get().window_height, "NWiiRecomp");
    SetTargetFPS(60);
    InitAudioDevice();
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
      std::cout << "[Loader] Loading section at 0x" << std::hex << sec.address << " size 0x" << sec.size << std::dec << std::endl;
      for (size_t i = 0; i < sec.size; ++i) {
        if ((sec.address + i) == 0x80003448) {
            std::cout << "[Loader] Found 0x80003448 inside section! val=" << std::hex << (uint32_t)sec.data[i] << std::dec << std::endl;
        }
        ctx->mmu.write8(sec.address + i, sec.data[i]);
      }
    }
    // Text sections have statically recompiled bodies; everything outside
    // them (low-mem helpers, streamed overlays) runs on the interpreter.
    // Keep the low-mem region (< 0x80004000) on the interpreter: parts of it
    // (memset/memcpy loop backedges) are reached only via mid-function jumps
    // the analyzer did not register as dispatcher cases. Marking them as
    // "recompiled" makes the interpreter yield to a dispatcher that has no
    // case, ping-ponging forever. Interpreting straight through is slower
    // but correct.
    if (sec.is_text) {
      uint32_t start = std::max<uint32_t>(sec.address, 0x80004000);
      uint32_t end = sec.address + sec.size;
      if (end > start)
        nwii::runtime::add_recompiled_range(start, end);
    }
  }

  arena_lo = (arena_lo + 31) & ~31;

  // The crt stack usually sits ABOVE the last section (linker __stack),
  // and OSClearArena wipes everything from ArenaLo up. Recover the stack
  // top from the entry code itself: __init_registers loads r1 with
  // "lis r1, X@ha" (3C20xxxx) followed by "ori/addi r1, r1, X@lo".
  {
    uint32_t stack_top = 0;
    uint32_t hi = 0;
    bool have_hi = false;
    for (uint32_t off = 0; off < 0x200; off += 4) {
      uint32_t insn = ctx->mmu.read32(exe.entry_point + off);
      if ((insn & 0xFFFF0000) == 0x3C200000) { // lis r1, hi
        hi = (insn & 0xFFFF) << 16;
        have_hi = true;
      } else if (have_hi && (insn & 0xFFFF0000) == 0x60210000) { // ori r1,r1,lo
        stack_top = hi | (insn & 0xFFFF);
        break;
      } else if (have_hi && (insn & 0xFFFF0000) == 0x38210000) { // addi r1,r1,lo
        stack_top = hi + (int16_t)(insn & 0xFFFF);
        break;
      }
    }
    if (stack_top >= arena_lo && stack_top < 0x81800000) {
      std::cout << "[Loader] crt stack top 0x" << std::hex << stack_top
                << " above section end 0x" << arena_lo
                << ", raising ArenaLo" << std::dec << std::endl;
      // +0x100 guard: __OSInitAudioSystem stashes DSP boot code in the
      // 128 bytes right below ArenaLo, which must not be live stack
      arena_lo = (stack_top + 0x100 + 31) & ~31u;
    }
  }

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
  // Arena bounds, apploader-style (matches Dolphin BS2 emulation):
  // GC: 0x30 (ArenaLo) stays 0 so OSInit uses the game's linker __ArenaLo
  // (a section-end guess would overlap the runtime stack and OSClearArena
  // would wipe it). 0x34 (ArenaHi) must be a real top-of-memory value or
  // the arena has zero size and every game heap allocation returns NULL.
  // The RVL SDK build of SHSM reads its MEM1 arena lo from 0x34 instead.
  if (!is_gc) {
    ctx->mmu.write32(0x80000030, arena_lo);
    ctx->mmu.write32(0x80000034, arena_hi);
  }
  if (!vdisc.valid()) {
    // No extracted disc: keep legacy behaviour of 0x38 = top of usable MEM1
    ctx->mmu.write32(0x80000038, arena_hi);
  }
  if (is_gc) {
    // BI2 pointer (0x800000F4): the real apploader keeps its BI2 copy in
    // the apploader scratch area, which the game recycles after OSInit
    // reads the debug flags. Placing it below the FST would shrink the
    // arena below what games budget for (NFS HP2 sizes its heap map to
    // the full ArenaLo..FST range). Same recycling behaviour here.
    uint32_t gc_arena_hi = arena_hi;
    uint32_t fst_base = ctx->mmu.read32(0x80000038);
    if (fst_base >= 0x80000000 && fst_base < 0x81800000)
      gc_arena_hi = fst_base & ~31u;
    std::filesystem::path bi2_path =
        std::filesystem::path(argv[1]) / "sys" / "bi2.bin";
    if (std::filesystem::exists(bi2_path)) {
      std::ifstream bi2(bi2_path, std::ios::binary);
      std::vector<char> bi2_data((std::istreambuf_iterator<char>(bi2)),
                                 std::istreambuf_iterator<char>());
      uint32_t bi2_addr = 0x81200000; // apploader scratch region
      for (size_t i = 0; i < bi2_data.size(); ++i)
        ctx->mmu.write8(bi2_addr + (uint32_t)i, (uint8_t)bi2_data[i]);
      ctx->mmu.write32(0x800000F4, bi2_addr);
      std::cout << "[Loader] BI2 loaded at 0x" << std::hex << bi2_addr
                << std::dec << std::endl;
    }
    // Classic YAGCD layout, confirmed by OSClearArena's actual arguments
    // (start=*(0x30), size=*(0x34)-*(0x30)): ArenaLo at 0x30, ArenaHi at
    // 0x34. ArenaLo = end of sections raised above the crt stack plus the
    // DSP-boot scratch guard.
    ctx->mmu.write32(0x80000030, arena_lo);
    ctx->mmu.write32(0x80000034, gc_arena_hi);
    std::cout << "[Loader] GC Arena = 0x" << std::hex << arena_lo << " - 0x"
              << gc_arena_hi << " (" << std::dec
              << (gc_arena_hi - arena_lo) / 1024 << " KiB)" << std::endl;
  }
  // 0xF0: simulated memory size (read by OSGetConsoleSimulatedMemSize)
  ctx->mmu.write32(0x800000F0, mem1_size);

  // The 0x8000311x-0x8000315x block below is OS low-memory (MEM2 bounds,
  // Hollywood revision) on real hardware, but some games load a code/data
  // section over that range (NFS HP2's CodeWarrior runtime memset/memcpy
  // helpers sit at 0x80003100). Writing synthesized globals there would
  // corrupt executable code. Only poke an address the game itself does not
  // occupy — the loaded section is always authoritative for its own bytes.
  auto poke_global = [&](uint32_t addr, uint32_t val) {
    for (const auto &sec : exe.sections)
      if (!sec.is_bss && addr >= sec.address && addr < sec.address + sec.size)
        return; // game section owns this address
    ctx->mmu.write32(addr, val);
  };

  if (!is_gc) {
    // Wii-only low-mem fields
    // 0x3118: Simulated MEM2 Size, 0x311C: Physical MEM2 Size
    poke_global(0x80003118, mem2_size);
    poke_global(0x8000311C, mem2_size);
    // 0x3124 - 0x3134: usable MEM2 bounds + IOS-reserved region
    poke_global(0x80003124, 0x90000800);
    poke_global(0x80003128, 0x93e00000);
    poke_global(0x80003130, 0x93e00000);
    poke_global(0x80003134, 0x94000000);
    // 0x3158: Hollywood hardware revision (retail = 0x00000023)
    poke_global(0x80003158, 0x00000023);
  }

  // VI current TV mode (__VIDTVStatus / OS low-mem 0x800000CC): the bootrom
  // leaves this set to the console's video system. A PAL title configures a
  // PAL render mode and the SDK VIConfigure asserts+halts if it sees this as
  // an NTSC->PAL change. Derive NTSC vs PAL from the disc region letter (4th
  // char of the game id) so any title gets a consistent starting mode — the
  // game's own VIConfigure still overwrites it (and only matters here for the
  // very first configure). VITVMode: NTSC=0, PAL=1.
  {
    const std::string &gid = nwii::runtime::Config::get().game_id;
    char region = gid.size() >= 4 ? gid[3] : 'E';
    // PAL/50Hz regions per the Nintendo disc region letters.
    bool pal = (region == 'P' || region == 'D' || region == 'F' ||
                region == 'I' || region == 'S' || region == 'H' ||
                region == 'U' || region == 'X' || region == 'Y' ||
                region == 'Z');
    poke_global(0x800000CC, pal ? 1u : 0u);
  }

  // Initial SP
  ctx->gpr[1] = 0x816FFFF0;

  if (is_gc) {
    ctx->mmu.write32(0x800000F8, 40500000);  // OS_TIMER_CLOCK
    ctx->mmu.write32(0x800000FC, 162000000); // OS_BUS_CLOCK
    ctx->tb_freq = 40500000;                 // TB = bus/4
  } else {
    ctx->mmu.write32(0x800000F8, 60750000);  // OS_TIMER_CLOCK
    ctx->mmu.write32(0x800000FC, 243000000); // OS_BUS_CLOCK
    ctx->tb_freq = 60750000;                 // TB = bus/4
  }
  // Reset the wall-clock origin now that the platform TB rate is known,
  // so the guest's first OSGetTime reads a small value.
  ctx->tb_start = std::chrono::steady_clock::now();

  nwii::runtime::init_ipc_client(*ctx);

  // Launch CPU emulation in a background thread
  std::thread cpu_thread(cpu_thread_func, ctx.get());

  if (headless) {
    // No window: only pace VBlank so the OS thread queue keeps moving
    uint64_t tick = 0;
    while (ctx->is_running) {
      std::this_thread::sleep_for(std::chrono::milliseconds(16));

      // Window-less input still works for network sources (PhoneSource
      // UDP); keyboard/gamepad sources read empty raylib state safely.
      nwii::runtime::input::InputManager::get().update();

      extern void ProcessGXFifo();
      ProcessGXFifo();
      ctx->vblank_pending = true;
      if (std::getenv("NWII_SAMPLE") && (++tick % 60) == 0) {
        std::cout << "[Sample] pc=0x" << std::hex << ctx->pc << " lr=0x"
                  << ctx->lr << " msr=0x" << ctx->msr << " r3=0x" << ctx->gpr[3]
                  << " cb=" << (ctx->in_callback ? 1 : 0) << " inst=" << std::dec
                  << ctx->inst_count << "\n";
        // NWII_PEEK=hexaddr[,words]: dump guest memory once per second.
        if (const char *env = std::getenv("NWII_PEEK")) {
          uint32_t pa = 0, pw = 8;
          if (std::sscanf(env, "%x,%u", &pa, &pw) >= 1 && pa) {
            std::cout << "[Peek] " << std::hex << pa << ":";
            for (uint32_t i = 0; i < pw && i < 32; i++)
              std::cout << " " << ctx->mmu.read32(pa + i * 4);
            std::cout << std::dec << "\n";
          }
        }
        // Run-queue bitmap + reschedule flag (SDA offsets from the SDK
        // scheduler): shows whether the OS ever reaches its EE=1 idle spin
        // (bitmap==0) or keeps churning ready threads.
        if ((tick % 300) == 0 && ctx->gpr[13] >= 0x80000000u) {
          uint32_t r13 = ctx->gpr[13];
          std::cout << "  [RunQ] bitmap=0x" << std::hex
                    << ctx->mmu.read32(r13 - 0x21c8) << " resched=0x"
                    << ctx->mmu.read32(r13 - 0x21c4) << " cur=0x"
                    << ctx->mmu.read32(0x800000E4)
                    // VI retrace state (SDA offsets from the VI handler):
                    // count, pre-retrace CB, post-CB/flip flag slots.
                    << " retrace=" << std::dec << ctx->mmu.read32(r13 - 0x2358)
                    << std::hex << " postCB=0x" << ctx->mmu.read32(r13 - 0x2344)
                    << std::dec << "\n";
          // Gates of the game's post-retrace callback (0x80137584): it only
          // flips + sends the frame message when [vidstate+0xa2]==0 and the
          // flip-pending byte [r13-0x29b6] is set.
          uint32_t vs = ctx->mmu.read32(r13 - 0x29c0);
          // EndFrame (0x801376dc) operates on the object at [r13-0x2AF0]:
          // +0xB0 frame ptr, +0xB4 float gate, +0xB8/BA flags, +0xBE wait
          // mode (0=msg queue, else token spin), +0x9C countdown.
          uint32_t ef = ctx->mmu.read32(r13 - 0x2AF0);
          std::cout << "  [FlipG] vs=0x" << std::hex << vs
                    << " vsA2=" << std::dec << (int)ctx->mmu.read8(vs + 0xa2)
                    << " pend29b6=" << (int)ctx->mmu.read8(r13 - 0x29b6)
                    << " b29b5=" << (int)ctx->mmu.read8(r13 - 0x29b5)
                    << " b4808=" << (int)ctx->mmu.read8(r13 - 0x4808)
                    << " ef=0x" << std::hex << ef
                    << " efB0=0x" << (ef ? ctx->mmu.read32(ef + 0xb0) : 0)
                    << " efB4=0x" << (ef ? ctx->mmu.read32(ef + 0xb4) : 0)
                    << " efB8=" << std::dec << (ef ? (int)ctx->mmu.read8(ef + 0xb8) : -1)
                    << " efBE=" << (ef ? (int)ctx->mmu.read8(ef + 0xbe) : -1)
                    << " ef9C=" << (ef ? (int)ctx->mmu.read32(ef + 0x9c) : -1)
                    << " ddflag=" << (int)ctx->mmu.read8(r13 - 0x2368)
                    << std::hex << " tok=0x" << ctx->mmu.read16(r13 - 0x29b8)
                    << " pe_sr=0x" << nwii::runtime::hw::g_pe_sr
                    << std::dec << "\n";
          // Which HW block the game's own retrace-ish handler polls: hwp is
          // the register base it reads +8/+0xC from, shp a shadow struct,
          // sdkPre/wrapPre the SDK and wrapper pre-retrace callback slots.
          std::cout << "  [VIwrap] hwp=0x" << std::hex
                    << ctx->mmu.read32(r13 - 0x4118) << " shp=0x"
                    << ctx->mmu.read32(r13 - 0x239c) << " sdkPre=0x"
                    << ctx->mmu.read32(r13 - 0x2348) << " wrapPre=0x"
                    << ctx->mmu.read32(r13 - 0x237c) << std::dec << "\n";
        }
        // Wall-clock thread dump: walk the OS active-thread list and peek at
        // each sleeper's wait object so a starved producer/consumer chain is
        // visible even when DEC (the other dump site) barely fires. Racy
        // reads against the CPU thread are fine for diagnostics.
        if ((tick % 300) == 0) {
          uint32_t cur = ctx->mmu.read32(0x800000E4);
          uint32_t th = ctx->mmu.read32(0x800000DC);
          int guard = 0;
          while (th >= 0x80000000u && th < 0x81800000u && guard++ < 16) {
          uint32_t q = 0x803540e0;
          std::cout << "Queue " << std::hex << q << ": msgArray=" << ctx->mmu.read32(q+16) << " count=" << ctx->mmu.read32(q+20) << " first=" << ctx->mmu.read32(q+24) << " used=" << ctx->mmu.read32(q+28) << "\n";

            uint16_t state = ctx->mmu.read16(th + 0x2C8);
            uint32_t prio = ctx->mmu.read32(th + 0x2D0);
            uint32_t wq = ctx->mmu.read32(th + 0x2DC);
            uint32_t srr0 = ctx->mmu.read32(th + 0x198);
            std::cout << "  [Thr] 0x" << std::hex << th << (th == cur ? "*" : " ")
                      << " st=" << std::dec << state << " pr=" << prio
                      << " wq=0x" << std::hex << wq << " srr0=0x" << srr0;
            if (wq >= 0x80000000u && wq < 0x81800000u) {
              std::cout << " wq[-2..5]:";
              for (int i = -2; i < 6; ++i)
                std::cout << " " << ctx->mmu.read32(wq + i * 4);
            }
            std::cout << std::dec << "\n";
            th = ctx->mmu.read32(th + 0x2FC);
          }
        }
      }
    }
  } else {
    // Main Raylib/GPU Thread Loop. The guest frame renders into an
    // EFB-sized offscreen target which is then scaled onto the window.
    RenderTexture2D target = LoadRenderTexture(640, 480);
    Texture2D xfb_tex = {0};
    std::vector<unsigned char> xfb_px;

    // Console audio output: the Audio DMA (hw_dsp) decodes the game's
    // mixed 32kHz stereo buffers into a ring; feed it to a raylib stream.
    AudioStream gc_audio = {0};
    if (IsAudioDeviceReady()) {
      gc_audio = LoadAudioStream(32000, 16, 2);
      PlayAudioStream(gc_audio);
    }

    while (!WindowShouldClose() && ctx->is_running) {
      // Poll Inputs exactly once per frame
      nwii::runtime::input::InputManager::get().update();

      BeginTextureMode(target);
      // We no longer manually clear the target here every frame.
      // EFB clear is handled by PE_COPY_EXECUTE.

      // Present the XFB the game composed in RAM (GXCopyDisp, BP 0x52). It
      // is the finished frame — YUYV 4:2:2, big-endian, `stride` bytes/row.
      // Drawing it as the background shows real video/UI/2D output even when
      // the GX rasterizer can't reproduce the 3D scene. NWII_NOXFB disables.
      extern void GX_GetXfb(uint32_t*, unsigned*, unsigned*, unsigned*);
      uint32_t xfb_addr; unsigned xw, xh, xstride;
      GX_GetXfb(&xfb_addr, &xw, &xh, &xstride);
      
      // Prioritize the actual Video Interface (VI) frame buffer base if set.
      // Games playing FMVs via CPU will update this register without touching GX.
      if (nwii::runtime::hw::g_vi_top_field_base != 0) {
          xfb_addr = nwii::runtime::hw::g_vi_top_field_base;
      }

      static int xc = 0;
      if (xc++ < 60) {
          printf("[GXTRACE] XFB addr=0x%08X (VI=0x%08X) w=%u h=%u stride=%u\n", xfb_addr, nwii::runtime::hw::g_vi_top_field_base, xw, xh, xstride);
      }

      if (xfb_addr && xw && xh && xw <= 720 && xh <= 576 &&
          !std::getenv("NWII_NOXFB")) {
        xfb_px.resize((size_t)xw * xh * 4);
        auto clamp8 = [](float v) -> unsigned char {
          return (unsigned char)(v < 0 ? 0 : v > 255 ? 255 : v);
        };
        for (unsigned y = 0; y < xh; ++y) {
          uint32_t row = (xfb_addr | 0x80000000u) + (uint32_t)y * xstride;
          for (unsigned x = 0; x < xw; x += 2) {
            uint8_t y0 = ctx->mmu.read8(row + x * 2 + 0);
            uint8_t cb = ctx->mmu.read8(row + x * 2 + 1);
            uint8_t y1 = ctx->mmu.read8(row + x * 2 + 2);
            uint8_t cr = ctx->mmu.read8(row + x * 2 + 3);
            float fcb = (float)cb - 128.0f, fcr = (float)cr - 128.0f;
            for (int k = 0; k < 2; ++k) {
              float yy = (float)(k ? y1 : y0);
              size_t p = ((size_t)y * xw + x + k) * 4;
              if (x + (unsigned)k >= xw) break;
              xfb_px[p + 0] = clamp8(yy + 1.371f * fcr);
              xfb_px[p + 1] = clamp8(yy - 0.336f * fcb - 0.698f * fcr);
              xfb_px[p + 2] = clamp8(yy + 1.732f * fcb);
              xfb_px[p + 3] = 255;
            }
          }
        }
        if (xfb_tex.id == 0 || (unsigned)xfb_tex.width != xw ||
            (unsigned)xfb_tex.height != xh) {
          if (xfb_tex.id) UnloadTexture(xfb_tex);
          Image im = {xfb_px.data(), (int)xw, (int)xh, 1,
                      PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
          xfb_tex = LoadTextureFromImage(im);
        } else {
          UpdateTexture(xfb_tex, xfb_px.data());
        }
      }
      extern void ProcessGXFifo();
      ProcessGXFifo();
      EndTextureMode();

      BeginDrawing();
      ClearBackground(BLACK);
      
      if (xfb_addr && xw && xh && xw <= 720 && xh <= 576 &&
          !std::getenv("NWII_NOXFB")) {
        DrawTexturePro(xfb_tex, {0, 0, (float)xw, (float)xh},
                       {0, 0, (float)GetScreenWidth(), (float)GetScreenHeight()}, {0, 0}, 0.0f, WHITE);
      }
      
      rlEnableColorBlend();
      // Draw EFB on top of XFB. Assuming the clear color has alpha=0 when transparent,
      // it will overlay correctly.
      DrawTexturePro(target.texture,
                     { 0, 0, (float)target.texture.width, -(float)target.texture.height },
                     { 0, 0, (float)GetScreenWidth(), (float)GetScreenHeight() },
                     { 0, 0 }, 0.0f, WHITE);
      EndDrawing();

      // NWII_SCREENSHOT=prefix: dump the framebuffer to <prefix>_<frame>.png
      // every ~5s so a windowed run can be inspected without screen access.
      if (const char *shot_pfx = std::getenv("NWII_SCREENSHOT")) {
        static int shot_frame = 0;
        if ((++shot_frame % 300) == 0) {
          std::string p = std::string(shot_pfx) + "_" +
                          std::to_string(shot_frame) + ".png";
          TakeScreenshot(p.c_str());
        }
      }

      // Feed the host audio stream from the Audio-DMA ring.
      if (gc_audio.buffer && IsAudioStreamProcessed(gc_audio)) {
        static int16_t abuf[1024 * 2];
        nwii::runtime::hw::dsp_audio_pull(abuf, 1024);
        UpdateAudioStream(gc_audio, abuf, 1024);
      }

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