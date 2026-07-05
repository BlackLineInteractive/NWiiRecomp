# NWiiRecomp Media Showcase

This document showcases the development progress, tools, and runtime execution of **NWiiRecomp**. Here you can find visual demonstrations of what the project looks like and how it works.

## nWiiRuntime in Action

The **Runtime** layer translates hardware interactions (HLE) and handles the graphics FIFO and memory management.

<p align="center">
  <img src="image/video/gx-fifo-test.gif" alt="NWiiRuntime GX FIFO test" width="105%"/>
</p>
<p align="center">
  <img src="image/video/gx-fifo-test-2.gif" alt="NWiiRuntime GX FIFO test 2" width="105%"/>
</p>

## nWiiStudio Interface

**nWiiStudio** is a comprehensive GUI tool built with Raylib and ImGui. It's used for DOL/ELF inspection, memory viewing, and function analysis. It directly integrates with the `recomp_config.toml` file to manage settings efficiently.

### Disassembly & Function Analysis
Inspect the recompiled code, view decoded mnemonics, and navigate through the instruction-level boundaries.

<p align="center">
  <img src="image/1.png" alt="NWiiStudio Disassembly" width="115%"/>
</p>

### Settings & Configuration
Clean and straightforward configuration interface with thematic support (like the "Nintendo" Indigo style).

<p align="center">
  <img src="image/2.png" alt="NWiiStudio Settings" width="115%"/>
</p>
<p align="center">
  <img src="image/3.png" alt="NWiiStudio Settings" width="115%"/>
</p>
<p align="center">
  <img src="image/4.png" alt="NWiiStudio Settings" width="115%"/>
</p>

---
[Return to README](README.md)
