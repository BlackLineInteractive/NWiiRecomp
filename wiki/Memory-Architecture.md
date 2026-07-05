# Memory Architecture

The Wii and GameCube memory layout dictates physical and virtual memory allocation. To maintain compatibility with AOT recompilation, `nWiiRuntime` implements precise bounds and mapping logic instead of full page-table translation.

## Physical Memory Layout

### MEM1
The primary memory block containing the `.dol`/`.elf` executable, stack, and fast RAM.
- **GameCube**: 24 MB
- **Wii**: 24 MB (usable for OS and standard logic)

### MEM2 (Wii Only)
The secondary 64 MB memory block used for large assets, audio buffers, and expanded heap space.

## Low Memory Management

The runtime explicitly configures the hardware abstraction flags at address `0x80000000` to mirror authentic BIOS/Apploader state before the executable begins.

- **0x80000020**: Boot magic (`0x0D15EA5E` for GC, `0x10000006` for Wii).
- **0x80000024 / 0x80000028**: Simulated and physical MEM1 sizes.
- **0x80000030**: `MEM1 ArenaLo`. This pointer designates the start of the usable heap. The runtime dynamically raises `ArenaLo` above the static `.bss`/`.data` segments and the CRT stack base. Guard padding (+0x100 bytes) is injected to protect DSP initialization vectors from stack overwrite.
- **0x80000034**: `MEM1 ArenaHi`. Represents the top boundary of usable MEM1. If a virtual disc is present, `ArenaHi` is positioned right below the FST (File System Table).
- **0x80000038**: FST Base. If a virtual disc is parsed via `nWiiRuntime::VirtualDisc`, its layout is published here for OS boot initialization routines.
- **0x800000F4**: BI2 (Boot Information 2) block pointer. Sourced directly from `sys/bi2.bin` and mapped to `0x81200000` (Apploader scratch region), mimicking standard Dolphin and original hardware mechanics.

### Wii MEM2 Low Memory (0x800031xx)
For Wii mode, MEM2 limits are mapped concurrently:
- **0x80003118 / 0x8000311C**: Simulated and physical MEM2 sizes.
- **0x80003120 / 0x80003124**: `MEM2 ArenaLo` and `MEM2 ArenaHi`.
- **0x80003128**: IPC / Debug Arena boundaries mapped specifically to prevent the `OSInit` heap routines from defaulting to `0x0`, which would otherwise corrupt the exception vectors.

## MMU & Pointer Redirection

The static recompiler output replaces standard C++ pointers with index lookups through the `CPUContext::mmu`.

- **Virtual memory mapping**: Logical read/write operations (e.g., `lwz`, `stw`) trigger inline bound checks and pointer conversion.
- **Data cache management**: Instruction cache invalidate (`icbi`) and data cache block commands (`dcbf`, `dcbst`) are evaluated structurally; invalidations that affect translated segments invoke the interpreter fallback or notify the HLE framework.
