<p align="center">
  <img src="image/logo_wide.jpg" alt="NWiiRecomp" width="700"/>
</p>

<p align="center">
  Static recompilation and runtime toolkit for Nintendo GameCube and Wii executables.
</p>

<p align="center">
(The first recompiler that really works)
</p>

<p align="center">
  <a href="SHOWCASE.md"><strong>View Media Showcase</strong></a>
  <br><br>
  <a href="https://discord.gg/wp7zdxyqT">
    <img src="https://img.shields.io/badge/Discord-NWiiRecomp-5865F2?style=for-the-badge&logo=discord&logoColor=white" alt="NWiiRecomp Discord"/>
  </a>
  <br><br>
  <a href="https://youtube.com/@blacklineinteractive">
    <img src="https://img.shields.io/badge/YouTube-Blackline_Interactive-FF0000?style=flat-square&logo=youtube&logoColor=white" alt="Blackline Interactive YouTube"/>
  </a>
</p>

---

## What is this?

NWiiRecomp translates Nintendo Wii/GameCube (`.dol`, `.elf`) executables into native C++ code. The output is a standalone executable that runs natively without instruction-level emulation. Hardware interactions are handled by a High-Level Emulation (HLE) runtime layer.

> **Wii U Note:** Support for Wii U (`.rpx`, `.rpl`) and Cafe OS has been moved to our dedicated recompile repository! For Wii U, please visit: [nWiiURecomp](https://github.com/BlackLineInteractive/nWiiURecomp)

> **A Note on Our Approach:** Unlike some recompilation projects that rely heavily on Dolphin's VideoCommon or shader generation (where the recompiler is merely a PPC-to-C translator feeding the GX stream back into an existing emulator), NWiiRecomp features a fully **custom-built** rasterizer, shader generator, and HLE runtime from scratch. We are not simply reusing an existing emulator's pipeline; this is a fully standalone implementation.

> **Note:** The recompiler and runtime are designed to be **universal**. We have tested the architecture on radically different games (e.g., *Need For Speed: Hot Pursuit 2*, *Mario Party 7*, and *Silent Hill: Shattered Memories*). The fact that these fundamentally different engines yield identical, stable hardware behavior and 0 crashes confirms the universality of the core emulator design.
>
> **Latest Update:** Both *Need For Speed* and *Mario Party 7* (GameCube) successfully boot, stream from DVD, and generate GX display lists! The runtime now correctly parses 2D/3D geometry and decodes hardware texture formats (I4, I8, RGB565, CMPR) via Raylib!

---

## Project Structure

```
NWiiRecomp/
├── nWiiAnalyzer/   — DOL/ELF parser and function boundary analyzer
├── nWiiRecomp/     — Offline static recompiler (PPC → C++)
├── nWiiRuntime/    — Cross-platform runtime + HLE library
└── nWiiStudio/     — GUI debugging and inspection tool (Raylib + ImGui)
```

---

## What Works

### Analyzer (`nWiiAnalyzer`)

- Full DOL section parsing.
- Recursive disassembly from entry point.
- Branch analysis for function boundary discovery.
- Vtable and jump table pointer recovery.
- Heuristics for OS dispatch stubs (via `lis`/`addi` patterns).

### Recompiler (`nWiiRecomp`)

- Translates PowerPC 750CL instructions to C++.
- Implements core integer, logic, floating point, branch, and SPR instructions,
  including the time base (`mftb`) and decrementer (`mtdec`/`mfdec`).
- Implements paired-singles (SIMD) with GQR-based quantization scales mapped to C++ intrinsics.
- Tail-call detection and `goto`-based local branch inlining.
- Mid-function entry point dispatch via `switch(ctx.pc)`.
- Per-game HLE hooks from `[hle_hooks]` in the config TOML; the recompiler
  itself contains no game-specific addresses.
- Functions that are all zeroes in the DOL (filled with code at runtime)
  are routed to the runtime interpreter instead of being recompiled.
- Optional per-function call tracing, enabled at run time with
  `NWII_TRACE_CALLS=1`.

### Runtime (`nWiiRuntime`)

- **Configuration**: Per-game TOML profiles via `tomlplusplus` for target platform, HLE hooks, and input.
- **Interpreter fallback**: A PPC750 integer interpreter executes code that
  does not exist in the DOL image (arena-clear helpers copied to low memory,
  trampolines, streamed overlays), then hands control back to recompiled code.
- **Interrupts**: PI interrupt controller with the Dolphin `INT_CAUSE_*` bit
  layout dispatched into the guest `__OSInterruptTable`; decrementer
  exceptions drive `OSAlarm`.
- **Hardware MMIO**: Register-level emulation of VI, DI (drive commands with
  DMA from a virtual disc), SI (joybus protocol with controller ID, origin,
  and polling), EXI (RTC/SRAM chip with valid checksums), DSP mailboxes,
  AI, MI, and the Wii IPC (Starlet) interface.
- **IOS HLE**: /dev/di, /dev/fs (virtual NAND with SYSCONF and settings),
  /dev/es, /dev/stm, /dev/usb served through a fd-based kernel, reachable
  both from IOS_* library calls and from the raw HW IPC ring.
- **Virtual disc**: Direct reads from extracted game dumps (`sys/` +
  `files/`), WBFS, and ISO images.
- **Input**: Selectable modes in `[input]`: gamepad as Classic/GC pad,
  gamepad with stick- or gyro-assisted pointer, full tilt control with
  sensitivity setting, smartphone as Wiimote over UDP, keyboard and mouse.
- **GX Graphics**: Structure tracking and WGPIPE ring-buffer parsing.
- **Memory**: DOL loading and virtual memory mapping; GC and Wii low-memory
  layouts (arena, FST, BI2, console type) set up per platform.
- **Headless mode**: `NWII_HEADLESS=1` runs without a window for automated
  boot testing and log capture.

### Studio (`nWiiStudio`)

- Raylib + ImGui-based GUI
- DOL file browser and loader
- Function list panel with address and instruction count
- Disassembly viewer (raw PPC hex + decoded mnemonic)
- Basic memory map view
- **Settings & Config Integration**: Direct integration with `recomp_config.toml` to manage paths cleanly.
- **Thematic Themes**: Includes "Nintendo" theme (GameCube Indigo / Wii aesthetic) for a polished user experience.

---

> **A Note on Development:** To ensure maximum code quality and preserve the integrity of the architecture, AI is used **exclusively** for generating commit message titles and translating code comments to English—nothing more. All core reverse-engineering, recompilation logic, and architectural design are entirely human-driven.

---

## Building

**Requirements:** CMake 3.20+, a C++20 compiler, internet access (Raylib is fetched automatically).

```bash
cmake -B build
cmake --build build -j$(nproc)
```

---

## Usage

```bash
# 1. Analyze and recompile the game (paths and options come from the TOML)
./build/nWiiRecomp/nwiirecomp config.toml

# 2. Build the generated project
cd export
cmake -B build
cmake --build build -j12

# 3. Run with the extracted game directory
cd ..
./export/build/<ProjectName> path/to/extracted-game config.toml
```

The recompiler outputs a self-contained `export/` directory containing the
generated sources and a copy of `nWiiRuntime`. It can be built independently
without the rest of this repository. Delete `export/` (including its `build/`)
before regenerating so no stale sources survive.

Useful environment variables:

```bash
NWII_HEADLESS=1      # run without a window, log-only (boot debugging)
NWII_TRACE_CALLS=1   # print every recompiled function entry
```

---

- [PS2Recomp](https://github.com/ran-j/PS2Recomp) — Project structure and early inspiration from former friend Ran-J; the foundation this recompiler is built on.
- [N64Recomp](https://github.com/N64Recomp/N64Recomp) — Inspiration for the static recompilation approach

- [Dolphin Emulator](https://github.com/dolphin-emu/dolphin) — Huge thanks for the endless hardware documentation, GX/DSP accuracy, and HLE inspiration!
- [Cemu](https://github.com/cemu-project/Cemu.git) — Reference for Cafe OS RPL imports, hardware emulation, and GX2 to Vulkan translation.
- [Decaf-emu](https://github.com/decaf-emu/decaf-emu.git) — Great resource for RPX/RPL loaders and Cafe OS kernel/syscalls.
- [WiiUBrew](https://wiiubrew.org/wiki/Hardware/GX2) — Excellent Wii U GX2 and Cafe OS documentation.
- [CafeGLSL](https://github.com/Exzap/CafeGLSL.git) — Open-source shader compiler alternative, crucial for understanding GX2 shaders.
- [rpl2elf](https://github.com/Relys/rpl2elf.git) — Useful for RPX/RPL to ELF conversion and parsing.
- [GhidraRPXLoader](https://github.com/decaf-emu/GhidraRPXLoader.git) — RPX loader logic.
- [WiiBrew](https://wiibrew.org/wiki/Main_Page) — Wii hardware and software documentation
- [YAGCD](https://hitmen.c02.at/files/yagcd/) — Yet Another GameCube Documentation — Low-level GC/Wii CPU and hardware reference
- PowerPC 750CL User's Manual — Official ISA reference

---

## License

License. See [LICENSE](LICENSE) for details.  
© 2026 Volodymyr Vovchok.

> **Disclaimer:** This project contains no copyrighted Nintendo code, SDKs, or game data. You must provide your own legally obtained game executables.
