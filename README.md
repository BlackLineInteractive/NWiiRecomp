# NWiiRecomp

A static recompiler for the Nintendo Wii. 

Inspired by [N64Recomp](https://github.com/Mr-Wiseguy/N64Recomp) and [PS2Recomp](https://github.com/Ran-J/PS2Recomp) (shoutout to Ran-J). 

Currently in the early stages of development. The goal is to take Wii executables and recompile them into native C/C++ code, similar to how those projects handle their respective consoles. 

Right now the focus is strictly on the Wii. Wii U support might be considered later.

## Structure
- `src/recompiler`: The offline recompiler.
- `src/runtime`: The runtime library that the recompiled games link against.

## Endianness
The Wii uses a PowerPC CPU, which is big-endian. The recompiled code will handle byte swapping when running on little-endian hosts (like x86/ARM) using intrinsics for performance.

## Building
Requires CMake.
