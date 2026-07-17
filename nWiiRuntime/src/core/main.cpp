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
#include <SDL.h>
#include <glad/glad.h>
#include <thread>
#include <vector>

#include "runtime/gx_state.h"
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
} 

extern "C" void run_game(nwii::runtime::CPUContext &ctx);

void cpu_thread_func(nwii::runtime::CPUContext *ctx) {
  nwii::runtime::g_ctx_ptr = ctx;
  std::cout << "[Thread] CPU Core started. pc=" << std::hex << ctx->pc << " is_running=" << ctx->is_running << std::endl;
  std::flush(std::cout);
  run_game(*ctx);

  
  
  if (!ctx->is_running) {
    std::cout << "[Thread] CPU Core exited: shutdown requested (window closed "
                 "or quit). NOT a crash." << std::endl;
  } else {
    std::cout << "[Thread] CPU Core CRASHED (jumped to 0). Final PC: 0x"
              << std::hex << ctx->pc << ", LR: 0x" << ctx->lr << std::dec
              << std::endl;
  }
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

  {
    using namespace nwii::runtime::input;
    auto& im = InputManager::get();
    int mode = nwii::runtime::Config::get().input_mode;
    switch (mode) {
    case 1: 
      std::cout << "[Input] Mode 1 (Bluetooth Wiimote) not implemented, "
                   "falling back to keyboard+mouse" << std::endl;
      im.add_source(std::make_unique<MouseKeyboardSource>());
      break;
    case 2: 
    case 3: 
    case 4: 
      im.add_source(std::make_unique<GamepadSource>());
      
      im.add_source(std::make_unique<MouseKeyboardSource>());
      break;
    case 5: 
      im.add_source(std::make_unique<PhoneSource>());
      im.add_source(std::make_unique<MouseKeyboardSource>());
      break;
    case 7: 
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

  const char *headless_env = std::getenv("NWII_HEADLESS");
  bool headless = headless_env && headless_env[0] == '1';

  SDL_Window* window = nullptr;
  SDL_GLContext gl_ctx = nullptr;
  SDL_AudioDeviceID audio_dev = 0;
  GLuint efb_fbo = 0, efb_tex = 0;
  GLuint xfb_tex = 0;

  if (!headless) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) < 0) {
      std::cerr << "SDL_Init failed: " << SDL_GetError() << "\n";
      return 1;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);

    
    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    {
      SDL_DisplayMode dm;
      if (SDL_GetCurrentDisplayMode(0, &dm) == 0)
        std::cout << "[GL] display 0: " << dm.w << "x" << dm.h << " fmt="
                  << SDL_GetPixelFormatName(dm.format) << std::endl;
      else
        std::cerr << "[GL] no display mode: " << SDL_GetError()
                  << " (screen locked or no display attached?)" << std::endl;
    }
#ifdef __APPLE__

    
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS,
                        SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
#endif
    bool use_gl = (nwii::runtime::Config::get().backend == nwii::runtime::Backend::OpenGL);
    uint32_t window_flags = SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
    if (use_gl) {
        window_flags |= SDL_WINDOW_OPENGL;
    } else {
#ifdef SDL_WINDOW_METAL
        window_flags |= SDL_WINDOW_METAL;
#endif
    }
    window = SDL_CreateWindow("NWiiRecomp", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                              nwii::runtime::Config::get().window_width,
                              nwii::runtime::Config::get().window_height,
                              window_flags);
    if (!window) {
      std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << "\n";
      return 1;
    }

    
    extern void GX_SetWindow(void *);
    GX_SetWindow(window);
    if (use_gl) {
      gl_ctx = SDL_GL_CreateContext(window);
      if (!gl_ctx) {
        std::cerr << "SDL_GL_CreateContext failed: " << SDL_GetError() << "\n";
        return 1;
      }
      if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) {
        std::cerr << "glad: failed to load GL functions\n";
        return 1;
      }
      std::cout << "[GL] " << (const char *)glGetString(GL_RENDERER) << " | "
                << (const char *)glGetString(GL_VERSION) << std::endl;
      SDL_GL_SetSwapInterval(1);
    }
    
    glGenFramebuffers(1, &efb_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, efb_fbo);
    glGenTextures(1, &efb_tex);
    glBindTexture(GL_TEXTURE_2D, efb_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 640, 480, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, efb_tex, 0);
    {

      
      GLuint efb_depth_rbo = 0;
      glGenRenderbuffers(1, &efb_depth_rbo);
      glBindRenderbuffer(GL_RENDERBUFFER, efb_depth_rbo);
      glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, 640, 480);
      glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                GL_RENDERBUFFER, efb_depth_rbo);
    }
    {
      
      GLenum st = glCheckFramebufferStatus(GL_FRAMEBUFFER);
      std::cout << "[GL] EFB FBO status: 0x" << std::hex << st << std::dec
                << (st == GL_FRAMEBUFFER_COMPLETE ? " (complete)" : " (INCOMPLETE!)")
                << std::endl;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    glGenTextures(1, &xfb_tex);
    glBindTexture(GL_TEXTURE_2D, xfb_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    SDL_AudioSpec want = {0}, have;
    want.freq = 32000;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 1024;
    audio_dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (audio_dev > 0) SDL_PauseAudioDevice(audio_dev, 0);
  }

  auto ctx = std::make_unique<nwii::runtime::CPUContext>();
  nwii::runtime::g_mmu = &ctx->mmu;

  nwii::loader::Executable exe;
  if (!exe.load_unpacked_game(argv[1]))
    return 1;

  uint32_t arena_lo = 0x80000000;
  
  for (const auto &sec : exe.sections) {
    uint32_t end_addr = sec.address + sec.size;
    if (end_addr > arena_lo)
      arena_lo = end_addr;

    if (sec.is_bss) {
      for (size_t i = 0; i < sec.size; ++i)
        ctx->mmu.write8(sec.address + i, 0);
    }
  }

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

    

    
    
    if (sec.is_text) {
      uint32_t start = sec.address;
      if (std::getenv("NWII_INTERP_LOW"))
        start = std::max<uint32_t>(sec.address, 0x80004000);
      uint32_t end = sec.address + sec.size;
      if (end > start)
        nwii::runtime::add_recompiled_range(start, end);
    }
  }

  arena_lo = (arena_lo + 31) & ~31;

  

  {
    uint32_t stack_top = 0;
    uint32_t hi = 0;
    bool have_hi = false;
    for (uint32_t off = 0; off < 0x200; off += 4) {
      uint32_t insn = ctx->mmu.read32(exe.entry_point + off);
      if ((insn & 0xFFFF0000) == 0x3C200000) { 
        hi = (insn & 0xFFFF) << 16;
        have_hi = true;
      } else if (have_hi && (insn & 0xFFFF0000) == 0x60210000) { 
        stack_top = hi | (insn & 0xFFFF);
        break;
      } else if (have_hi && (insn & 0xFFFF0000) == 0x38210000) { 
        stack_top = hi + (int16_t)(insn & 0xFFFF);
        break;
      }
    }
    if (stack_top >= arena_lo && stack_top < 0x81800000) {
      std::cout << "[Loader] crt stack top 0x" << std::hex << stack_top
                << " above section end 0x" << arena_lo
                << ", raising ArenaLo" << std::dec << std::endl;

      arena_lo = (stack_top + 0x100 + 31) & ~31u;
    }
  }

  bool is_gc = nwii::runtime::Config::get().platform ==
               nwii::runtime::Platform::GameCube;

  
  uint32_t arena_hi = 0x81700000;
  auto &vdisc = nwii::runtime::VirtualDisc::get();
  if (vdisc.init(argv[1])) {
    const auto &boot = vdisc.boot_data();
    
    for (size_t i = 0; i < 0x20 && i < boot.size(); ++i)
      ctx->mmu.write8(0x80000000 + (uint32_t)i, boot[i]);

    const auto &fst = vdisc.fst_data();
    uint32_t fst_size = (uint32_t)fst.size();
    uint32_t fst_addr = (0x81800000 - fst_size) & ~63u;
    for (uint32_t i = 0; i < fst_size; ++i)
      ctx->mmu.write8(fst_addr + i, fst[i]);

    ctx->mmu.write32(0x80000038, fst_addr); 
    ctx->mmu.write32(0x8000003C, fst_size); 
    if (fst_addr < 0x81700000)
      arena_hi = fst_addr & ~31u;
    std::cout << "[Loader] FST loaded at 0x" << std::hex << fst_addr
              << " size 0x" << fst_size << std::dec << std::endl;
  }

  // Console type (Dolphin BootManager): Wii retail Hollywood = 0x10000006,
  
  uint32_t console_type = is_gc ? 0x00000003 : 0x10000006;
  uint32_t mem1_size = 24 * 1024 * 1024; 
  uint32_t mem2_size = 64 * 1024 * 1024; 

  ctx->mmu.write32(0x80000020, is_gc ? 0x0D15EA5E : console_type); 
  
  ctx->mmu.write32(0x80000024, mem1_size);
  
  ctx->mmu.write32(0x80000028, mem1_size);
  ctx->mmu.write32(0x8000002C, console_type);
  // Arena bounds, apploader-style (matches Dolphin BS2 emulation):

  

  if (!is_gc) {
    ctx->mmu.write32(0x80000030, arena_lo);
    ctx->mmu.write32(0x80000034, arena_hi);
  }
  if (!vdisc.valid()) {
    
    ctx->mmu.write32(0x80000038, arena_hi);
  }
  if (is_gc) {

    

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
      uint32_t bi2_addr = 0x81200000; 
      for (size_t i = 0; i < bi2_data.size(); ++i)
        ctx->mmu.write8(bi2_addr + (uint32_t)i, (uint8_t)bi2_data[i]);
      ctx->mmu.write32(0x800000F4, bi2_addr);
      std::cout << "[Loader] BI2 loaded at 0x" << std::hex << bi2_addr
                << std::dec << std::endl;
    }

    
    
    ctx->mmu.write32(0x80000030, arena_lo);
    ctx->mmu.write32(0x80000034, gc_arena_hi);
    std::cout << "[Loader] GC Arena = 0x" << std::hex << arena_lo << " - 0x"
              << gc_arena_hi << " (" << std::dec
              << (gc_arena_hi - arena_lo) / 1024 << " KiB)" << std::endl;
  }
  
  ctx->mmu.write32(0x800000F0, mem1_size);

  

  
  
  auto poke_global = [&](uint32_t addr, uint32_t val) {
    for (const auto &sec : exe.sections)
      if (!sec.is_bss && addr >= sec.address && addr < sec.address + sec.size)
        return; 
    ctx->mmu.write32(addr, val);
  };

  if (!is_gc) {

    poke_global(0x80003118, mem2_size);
    poke_global(0x8000311C, mem2_size);
    
    poke_global(0x80003124, 0x90000800);
    poke_global(0x80003128, 0x93e00000);
    poke_global(0x80003130, 0x93e00000);
    poke_global(0x80003134, 0x94000000);
    
    poke_global(0x80003158, 0x00000023);
  }

  

  

  {
    const std::string &gid = nwii::runtime::Config::get().game_id;
    char region = gid.size() >= 4 ? gid[3] : 'E';
    
    bool pal = (region == 'P' || region == 'D' || region == 'F' ||
                region == 'I' || region == 'S' || region == 'H' ||
                region == 'U' || region == 'X' || region == 'Y' ||
                region == 'Z');
    poke_global(0x800000CC, pal ? 1u : 0u);
  }

  ctx->gpr[1] = 0x816FFFF0;

  // Low memory 0xF8/0xFC hold the BUS and CPU clocks (Dolphin writes

  
  
  if (is_gc) {
    ctx->mmu.write32(0x800000F8, 162000000); 
    ctx->mmu.write32(0x800000FC, 486000000); 
    ctx->tb_freq = 40500000;                 
  } else {
    ctx->mmu.write32(0x800000F8, 243000000); 
    ctx->mmu.write32(0x800000FC, 729000000); 
    ctx->tb_freq = 60750000;                 
  }

  ctx->tb_start = std::chrono::steady_clock::now();

  nwii::runtime::init_ipc_client(*ctx);

  std::thread cpu_thread(cpu_thread_func, ctx.get());

  if (headless) {
    
    uint64_t tick = 0;
    while (ctx->is_running) {
      std::this_thread::sleep_for(std::chrono::milliseconds(16));

      
      nwii::runtime::input::InputManager::get().update();

      extern void ProcessGXFifo();
      ProcessGXFifo();
      ctx->vblank_pending = true;
      if (std::getenv("NWII_SAMPLE") && (++tick % 60) == 0) {
        std::cout << "[Sample] pc=0x" << std::hex << ctx->pc << " lr=0x"
                  << ctx->lr << " msr=0x" << ctx->msr << " r3=0x" << ctx->gpr[3]
                  << " cb=" << (ctx->in_callback ? 1 : 0) << " inst=" << std::dec
                  << ctx->inst_count << "\n";
        
        if (const char *env = std::getenv("NWII_PEEK")) {
          uint32_t pa = 0, pw = 8;
          if (std::sscanf(env, "%x,%u", &pa, &pw) >= 1 && pa) {
            std::cout << "[Peek] " << std::hex << pa << ":";
            for (uint32_t i = 0; i < pw && i < 32; i++)
              std::cout << " " << ctx->mmu.read32(pa + i * 4);
            std::cout << std::dec << "\n";
          }
        }

        
        if ((tick % 300) == 0 && ctx->gpr[13] >= 0x80000000u) {
          uint32_t r13 = ctx->gpr[13];
          std::cout << "  [RunQ] bitmap=0x" << std::hex
                    << ctx->mmu.read32(r13 - 0x21c8) << " resched=0x"
                    << ctx->mmu.read32(r13 - 0x21c4) << " cur=0x"
                    << ctx->mmu.read32(0x800000E4)

                    << " retrace=" << std::dec << ctx->mmu.read32(r13 - 0x2358)
                    << std::hex << " postCB=0x" << ctx->mmu.read32(r13 - 0x2344)
                    << std::dec << "\n";

          
          uint32_t vs = ctx->mmu.read32(r13 - 0x29c0);

          
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

          
          std::cout << "  [VIwrap] hwp=0x" << std::hex
                    << ctx->mmu.read32(r13 - 0x4118) << " shp=0x"
                    << ctx->mmu.read32(r13 - 0x239c) << " sdkPre=0x"
                    << ctx->mmu.read32(r13 - 0x2348) << " wrapPre=0x"
                    << ctx->mmu.read32(r13 - 0x237c) << std::dec << "\n";
        }

        
        
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
    
    std::vector<unsigned char> xfb_px;
    unsigned xfb_w = 0, xfb_h = 0;
    bool quit = false;

    while (!quit && ctx->is_running) {
      if (!headless) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
          if (e.type == SDL_QUIT) quit = true;
        }
      }

      nwii::runtime::input::InputManager::get().update();

      if (!headless) {
        glBindFramebuffer(GL_FRAMEBUFFER, efb_fbo);
        glViewport(0, 0, 640, 480);
      }

      extern void GX_GetXfb(uint32_t*, unsigned*, unsigned*, unsigned*);
      uint32_t xfb_addr; unsigned xw, xh, xstride;
      GX_GetXfb(&xfb_addr, &xw, &xh, &xstride);
      if (nwii::runtime::hw::g_vi_top_field_base != 0) {
        xfb_addr = nwii::runtime::hw::g_vi_top_field_base;
      }

      if (xfb_addr && xw && xh && xw <= 720 && xh <= 576 && std::getenv("NWII_XFB") && !headless) {
        xfb_px.resize((size_t)xw * xh * 4);
        auto clamp8 = [](float v) -> unsigned char { return (unsigned char)(v < 0 ? 0 : v > 255 ? 255 : v); };
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
        glBindTexture(GL_TEXTURE_2D, xfb_tex);
        if (xw != xfb_w || xh != xfb_h) {
          glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, xw, xh, 0, GL_RGBA, GL_UNSIGNED_BYTE, xfb_px.data());
          xfb_w = xw; xfb_h = xh;
        } else {
          glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, xw, xh, GL_RGBA, GL_UNSIGNED_BYTE, xfb_px.data());
        }
      }

      extern void ProcessGXFifo();
      ProcessGXFifo();

      

      if (!headless) {
        static auto t0 = std::chrono::steady_clock::now();
        static int stat_n = 0;
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now - t0).count() >=
            5) {
          t0 = now;
          extern uint64_t g_stat_frames, g_stat_draws;
          extern uint64_t g_stat_parse_us, g_stat_render_us;
          extern uint64_t nwii_stat_hash0(), nwii_stat_shaders();
          std::vector<unsigned char> p(640 * 480 * 4);
          glBindFramebuffer(GL_READ_FRAMEBUFFER, efb_fbo);
          glReadPixels(0, 0, 640, 480, GL_RGBA, GL_UNSIGNED_BYTE, p.data());
          glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
          size_t nonblack = 0;
          for (size_t i = 0; i < p.size(); i += 4)
            if (p[i] | p[i + 1] | p[i + 2])
              ++nonblack;
          std::cout << "[STAT] " << ++stat_n * 5 << "s frames=" << std::dec
                    << g_stat_frames << " draws=" << g_stat_draws
                    << " parse_ms=" << g_stat_parse_us / 1000
                    << " render_ms=" << g_stat_render_us / 1000
                    << " efb_nonblack=" << nonblack << "/307200"
                    << " shaders=" << nwii_stat_shaders() << " hash0=0x"
                    << std::hex << nwii_stat_hash0();
          g_stat_parse_us = 0;
          g_stat_render_us = 0;

          std::cout << " E0..E7=";
          for (int i = 0xE0; i <= 0xE7; ++i)
            std::cout << nwii::runtime::gx::g_state.bp[i] << ",";

          
          std::cout << std::dec << " projType="
                    << nwii::runtime::gx::g_state.projType
                    << " projSet=" << (int)nwii::runtime::gx::g_state.projSet
                    << " proj=";
          for (int i = 0; i < 6; ++i)
            std::cout << nwii::runtime::gx::g_state.projection[i] << ",";

          
          uint32_t base = nwii::runtime::gx::g_state.texStages[0].base_addr;
          uint32_t toff = nwii::runtime::gx::g_state.texStages[0].tlut_offset;
          uint32_t tex_nz = 0, tlut_nz = 0;
          if (nwii::runtime::g_ctx_ptr) {
            for (uint32_t i = 0; i < 2048; ++i)
              if (nwii::runtime::g_ctx_ptr->mmu.read8(base + i))
                ++tex_nz;
          }
          for (uint32_t i = 0; i < 512; ++i) {
            uint32_t idx = toff + i;
            if (idx < sizeof(nwii::runtime::gx::g_state.tlutMem) &&
                nwii::runtime::gx::g_state.tlutMem[idx])
              ++tlut_nz;
          }
          std::cout << " tex0=" << std::hex << base << std::dec << "/"
                    << nwii::runtime::gx::g_state.texStages[0].width << "x"
                    << nwii::runtime::gx::g_state.texStages[0].height << "/f"
                    << (int)nwii::runtime::gx::g_state.texStages[0].format
                    << " texnz=" << tex_nz << "/2048 tlutnz=" << tlut_nz << "/512"
                    << std::endl;
        }
      }

      if (!headless) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        int draw_w = 0, draw_h = 0;
        SDL_GL_GetDrawableSize(window, &draw_w, &draw_h);
        glViewport(0, 0, draw_w, draw_h);
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);

        glBindFramebuffer(GL_READ_FRAMEBUFFER, efb_fbo);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        glBlitFramebuffer(0, 0, 640, 480, 0, 0, draw_w, draw_h,
                          GL_COLOR_BUFFER_BIT, GL_LINEAR);

        if (const char *shot_pfx = std::getenv("NWII_SCREENSHOT")) {
          static int shot_frame = 0;
          if ((++shot_frame % 300) == 0) {
            std::vector<unsigned char> px(640 * 480 * 4);
            glBindFramebuffer(GL_READ_FRAMEBUFFER, efb_fbo);
            glReadPixels(0, 0, 640, 480, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
            std::vector<unsigned char> flip(px.size());
            for (int y = 0; y < 480; ++y)
              std::memcpy(&flip[(size_t)y * 640 * 4],
                          &px[(size_t)(479 - y) * 640 * 4], 640 * 4);
            SDL_Surface *s = SDL_CreateRGBSurfaceFrom(
                flip.data(), 640, 480, 32, 640 * 4, 0x000000FF, 0x0000FF00,
                0x00FF0000, 0xFF000000);
            if (s) {
              std::string p = std::string(shot_pfx) + "_" +
                              std::to_string(shot_frame) + ".bmp";
              SDL_SaveBMP(s, p.c_str());
              SDL_FreeSurface(s);
            }
            glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
          }
        }

        SDL_GL_SwapWindow(window);
      }

      if (audio_dev > 0) {
        if (SDL_GetQueuedAudioSize(audio_dev) < 1024 * 4 * 2) {
          static int16_t abuf[1024 * 2];
          nwii::runtime::hw::dsp_audio_pull(abuf, 1024);
          SDL_QueueAudio(audio_dev, abuf, 1024 * 4);
        }
      }
      
      ctx->vblank_pending = true;
      if (headless) std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    if (!headless) {
      if (audio_dev > 0) SDL_CloseAudioDevice(audio_dev);
      SDL_GL_DeleteContext(gl_ctx);
      SDL_DestroyWindow(window);
      SDL_Quit();
    }
  }

  ctx->is_running = false;
  ctx->pc = 0; 
  if (cpu_thread.joinable())
    cpu_thread.join();

  nwii::runtime::shutdown();
  return 0;
}