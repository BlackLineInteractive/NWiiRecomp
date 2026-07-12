# nWiiRecomp

## Overview
`nWiiRecomp` is the core static recompiler. It takes the original PowerPC executable and the metadata from `nWiiAnalyzer` and performs full Ahead-Of-Time (AOT) translation into native C++ code.

## How it works
- **Translation:** Converts every PowerPC machine instruction into an equivalent C++ expression.
- **CPU Emulation:** Manages the CPU state (GPRs, FPRs, CRs) through the `CPUContext` structure.
- **Compilation Optimization:** Automatically splits the generated code across numerous small `.cpp` files to prevent modern compilers from hanging and to speed up build times.
- **Clean Architecture:** The recompiler does not emulate hardware or OS logic. It strictly translates CPU instructions, deferring all hardware accesses (MMIO) and OS calls to the `nWiiRuntime` module.
