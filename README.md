# NWiiRecomp

A static recompiler for the Nintendo Wii.

Inspired by [N64Recomp](https://github.com/Mr-Wiseguy/N64Recomp) and [PS2Recomp](https://github.com/Ran-J/PS2Recomp) (shoutout to Ran-J btw, working on that with him).

Currently in the early stages. The idea is to take Wii executables (DOL/ELF) and recompile them into native C++ code so they can run natively without a traditional emulator.

Right now the focus is strictly on the Wii. Wii U support might come later, but one step at a time.

## Project Structure

Kept it similar to PS2Recomp for sanity:

- `nWiiAnalyzer` - parses the executable, extracts function boundaries and data sections.
- `nWiiRecomp` - the actual offline recompiler that spits out C++ code.
- `nWiiRuntime` - cross-platform runtime library that the generated code links against (handles hardware simulation, OS functions, etc).
- `nWiiLoader` - loads up the original executable and handles memory mapping.
- `nWiiStudio` - GUI tool to make debugging and testing way easier.

## Endianness

Since the Wii's CPU (Broadway/PowerPC) is big-endian and we're targeting little-endian hosts (x86_64/ARM64), byte swapping is a major thing.
The plan is to handle this mostly in the recompiler directly. When we translate memory loads/stores (`lwz`, `stw`), we'll emit intrinsic byte-swapping instructions (like `bswap` or `__builtin_bswap32`). This keeps the runtime fast.
At the C++ level for data structures, we use wrapper types (e.g., `be32_t`) that handle swapping on read/write seamlessly.

## Building

Dependencies:

- CMake
- Raylib (automatically fetched via CMake)

```bash
cmake -B build
cmake --build build -j10
```

## Usage

Recompile an unpacked game and run the native executable:

```bash
# 1. Recompile the DOL
./build/nWiiRecomp/nwiirecomp path/to/unpacked_game/ path/to/symbols.csv

# 2. Build the generated project
cd export
cmake -B build
cmake --build build -j10

# 3. Execute
./build/RecompiledGame
```

## References

The development of this recompiler heavily relies on community documentation of the GameCube and Wii hardware:

- [WiiBrew](https://wiibrew.org/)
- [YAGCD (Yet Another GameCube Documentation)](https://hitmen.c02.at/files/yagcd/)

## License

MIT License. See [LICENSE](LICENSE) for details. (c) 2026 Vova Vovchok.

**Disclaimer**: This project does not contain any copyrighted Nintendo code, SDKs, or ROMs. You must legally provide your own executables.
