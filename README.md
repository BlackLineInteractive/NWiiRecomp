# NWiiRecomp

A static recompilation (AOT) tool for Nintendo Wii games. 

Inspired by projects like [N64Recomp](https://github.com/Mr-Harvey/N64Recomp) and [PS2Recomp](https://github.com/drewol/PS2Recomp).

The goal is to translate PowerPC executables (`.dol`, `.elf`, `.rel`) into C++ code that links against a custom cross-platform runtime environment.

## Structure

* `src/recompiler/` - The offline tool. Parses Wii binaries, decodes PowerPC instructions, and generates C++ code.
* `src/runtime/` - The cross-platform HLE runtime (OS, GX, input). Links with the generated code.
* `include/` - Shared headers.

## Build

```sh
mkdir build
cd build
cmake ..
cmake --build .
```
