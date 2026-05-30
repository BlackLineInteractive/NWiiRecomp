![NWiiRecomp Logo](assets/logo_wide.jpg)

# NWiiRecomp

A static recompiler for the Nintendo Wii. 

Inspired by [N64Recomp](https://github.com/Mr-Wiseguy/N64Recomp) and [PS2Recomp](https://github.com/Ran-J/PS2Recomp) (shoutout to Ran-J). 

Currently in the early stages of development. The goal is to take Wii executables and recompile them into native C/C++ code, similar to how those projects handle their respective consoles. 

Right now the focus is strictly on the Wii. Wii U support might be considered later.

## Structure
- `src/recompiler`: The offline recompiler.
- `src/runtime`: The runtime library that the recompiled games link against.

## GUI Tools & Analysis
The project will feature graphical tools for recompilation and analysis, built upon the foundations of [ps2xStudio](https://github.com/vovavovchok/ps2xStudio). The UI framework is being rewritten to be fully cross-platform (expanding beyond OpenGL) to ensure broad compatibility.

## Endianness
The Wii uses a PowerPC CPU, which is big-endian. The recompiled code will handle byte swapping when running on little-endian hosts (like x86/ARM) using intrinsics for performance.

## Building
Requires CMake.

## Legal
This project does not contain any copyrighted material, game assets, or proprietary Nintendo SDK code. NWiiRecomp is an independent educational and research project. We do not distribute ROMs, ISOs, or any other copyrighted software. To use this tool, you must provide your own legally dumped game executables from copies you physically own. 

Nintendo, Wii, and Wii U are trademarks of Nintendo Co., Ltd. This project is not affiliated with, authorized, or endorsed by Nintendo in any way.
