# Build Instructions

This document outlines the requisite steps for building the `nWiiRecomp` toolkit and subsequently compiling an exported game repository.

## Prerequisites

- **CMake**: Minimum version 3.20
- **Compiler**: Any C++20 compliant compiler (e.g., GCC 11+, Clang 14+, MSVC 2022)
- **Network Access**: Required during the initial CMake configuration phase to fetch the `raylib` dependencies for the `nWiiStudio` component.

## Stage 1: Toolkit Compilation

Compile the primary utilities (`nWiiAnalyzer`, `nWiiRecomp`, `nWiiRuntime`, and `nWiiStudio`).

```bash
cmake -B build
cmake --build build -j$(nproc)
```

## Stage 2: Recompilation Pipeline

Execute the analyzer and recompiler against a specified target. Parameters and file paths are handled exclusively through the `recomp_config.toml` structure.

```bash
./build/nWiiRecomp/nwiirecomp config.toml
```

This procedure generates an `export/` directory within the workspace. The export folder is entirely self-contained; it includes all generated `output_*.cpp` routines and a statically linked copy of `nWiiRuntime`.

## Stage 3: Game Execution Compilation

Navigate into the generated export directory and build the standalone game binary.

```bash
cd export
cmake -B build
cmake --build build -j$(nproc)
```

## Stage 4: Execution

The resultant binary acts as a native application. Pass the path to the extracted file system (`sys/` and `files/` structure) alongside the configuration file.

```bash
cd ..
./export/build/<ProjectName> path/to/extracted-game config.toml
```

### Environment Variables

Specific logging triggers are controlled via standard environment variables:

- `NWII_HEADLESS=1`: Bypasses window creation and UI loop logic, running completely headlessly for continuous integration and automated boot-testing.
- `NWII_TRACE_CALLS=1`: Injects a standard output stream tracing mechanism at the entry block of every recompiled function boundary (requires the trace block to have been emitted during recompilation).
