# nWiiAnalyzer

## Overview
`nWiiAnalyzer` is an offline parsing and analysis tool for Nintendo GameCube/Wii (`.dol`, `.elf`) and Wii U (`.rpx`, `.rpl`) executables.

## How it works
- **Parsing:** Reads executable headers and separates code from data sections.
- **Disassembly:** Scans PowerPC machine code to identify function boundaries, basic blocks, and branch targets.
- **Exporting:** Generates a control flow graph (CFG) and extracts metadata. This metadata is passed to the static recompiler (`nWiiRecomp`) to ensure safe and accurate translation without losing synchronization during indirect branches.
