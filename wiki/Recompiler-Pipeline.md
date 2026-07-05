# Recompiler Pipeline

NWiiRecomp implements an ahead-of-time (AOT) static recompilation strategy. The process translates PowerPC 750CL and Latte (Wii U) machine code into native C++ ahead of execution, circumventing the overhead associated with runtime instruction decoding and dispatching present in interpreter or JIT models.

## Phase 1: Disassembly and Analysis (`nWiiAnalyzer`)

The pipeline begins by parsing the executable header (`.dol`, `.elf`, `.rpx`, or `.rpl`) to map text and data sections into virtual memory.

1. **Entry Point Tracing**: The analyzer starts at the executable's entry point (`__start` or equivalent) and recursively follows static control flow (branches, jumps, subroutine calls).
2. **Function Boundary Discovery**: Due to the lack of symbol data in stripped binaries, boundaries are determined heuristically. The analyzer scans for standard prologue/epilogue sequences (e.g., `stwu r1, -xx(r1)`, `blr`) and tracks tail calls.
3. **Indirect Branch Resolution**: Jump tables (e.g., `switch` statements) are resolved by matching `rlwinm` + `lis` + `lwz` patterns that index into the `.rodata` section. Virtual function tables (vtables) are reconstructed using structural pattern matching.

## Phase 2: Translation (`nWiiRecomp`)

Once boundaries are established, the recompiler converts the PPC instructions of each identified function into semantically equivalent C++ code.

- **Instruction Lifting**: Instructions are translated into macro-like inline C++ functions operating on a global `CPUContext` struct, which maintains the GPR, FPR, SPR, CR, and XER states.
- **Local Control Flow**: Intra-function branches (e.g., `beq`, `bne`) are converted directly to standard `goto` statements targeting labeled local blocks (`loc_XXXXXXXX`).
- **Tail-Call Optimization**: Unconditional branches (`b`) targeting code outside the current function boundary are explicitly modeled as a modification of the Program Counter (`ctx.pc`) followed by a `return`, returning control to the main dispatcher loop.
- **Mid-Function Branching**: To handle indirect branches targeting arbitrary instructions within a function, every recompiled function begins with a `switch (ctx.pc)` statement, routing execution to the correct `loc_` label.

## Phase 3: Hardware Specialization

Certain PPC750CL capabilities require specialized translation:

- **Paired-Singles (SIMD)**: Translated utilizing standard C++ primitives and bitwise operations. Hardware Quantization (GQR registers) scales are directly mapped to C++ load/store scaling intrinsics.
- **Time Base & Decrementer**: `mftb`, `mtdec`, and `mfdec` are mapped to hardware-agnostic tick counters maintained by the runtime layer, ensuring synchronization with OS Alarm mechanisms.
- **Zero-filled Functions**: Sections of the `.dol` file containing uninitialized code (zeroes) intended for runtime population (e.g., dynamically loaded overlays, DSP bootcode) are omitted from static translation and routed to the runtime interpreter fallback.
