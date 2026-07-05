# Welcome to the NWiiRecomp Wiki

**NWiiRecomp** is a comprehensive static recompilation and runtime toolkit for Nintendo GameCube (`.elf`, `.dol`), Wii (`.elf`, `.dol`), and Wii U (`.rpl`, `.rpx`) executables. It translates PowerPC 750CL / Latte machine code into native, standalone C++ executables, bypassing traditional instruction-level emulation for maximum performance.

## Core Philosophy

Traditional emulators rely on Just-In-Time (JIT) or interpreter-based instruction translation at runtime. **NWiiRecomp** shifts the entire translation workload to an **offline static recompilation phase**. The output is a C++ project that can be compiled directly for the target host platform. At runtime, the recompiled executable interfaces with the hardware via a High-Level Emulation (HLE) layer (`nWiiRuntime`), simulating OS kernels, IPC communications, memory architectures, and the graphics pipeline.

> **Note:** The engine is inherently universal, meaning it processes standard PPC750CL/Latte ISA instructions regardless of the specific game. Currently, primary tests are conducted against *Need For Speed: Hot Pursuit 2* (GameCube) and *Silent Hill: Shattered Memories* (Wii).

## System Architecture

The project is divided into four distinct components:

1. **nWiiAnalyzer**: The disassembler and heuristic analyzer. It parses the executable headers, traces control flows recursively from the entry point, and resolves function boundaries, vtables, and jump tables.
2. **nWiiRecomp**: The offline static recompiler. It lifts PPC instructions into equivalent native C++ code, implements tail-call optimizations, and handles mid-function branching (e.g., `switch(ctx.pc)` dispatch).
3. **nWiiRuntime**: The HLE environment. It provides the OS interfaces (IOS, Starlet, Cafe OS), handles memory layout matching (MEM1/MEM2 arenas, FST mapping), device MMIO emulation (DI, EXI, SI, VI, PI), and an interpreter fallback for dynamic code.
4. **nWiiStudio**: A Raylib/ImGui-based GUI application designed for debugging, inspecting disassembly, and managing recompilation settings seamlessly.

## Quick Links

- [Recompiler Pipeline & Execution Flow](Recompiler-Pipeline.md)
- [Runtime Environment & HLE Implementation](Runtime-Environment.md)
- [Memory Layout & Heap Initialization](Memory-Architecture.md)
- [Building and Exporting Games](Build-Instructions.md)

---
*Wiki is currently under construction. Please use the sidebar to navigate through specific technical documentations.*
