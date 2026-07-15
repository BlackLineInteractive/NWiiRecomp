# NWiiRecomp Media Showcase

This document showcases the development progress, tools, and runtime execution of **NWiiRecomp**.

---

## 🎮 Mario Party 7 — In-Game Rendering

The first GC game to successfully boot, stream from DVD, parse GX display lists, decode hardware textures, and render to screen — running **natively** via static recompilation, with **zero instruction-level emulation**.

<p align="center">
  <img src="image/game/mp7.png" alt="Mario Party 7 — Health & Safety screen rendered via NWiiRecomp" width="100%"/>
</p>

<p align="center">
  <i>Health &amp; Safety screen — correctly positioned, full-resolution, stable frame output.</i>
</p>

<p align="center">
  <img src="image/game/mario-party-7.jpg" alt="Mario Party 7 cover" width="340"/>
</p>

---

## nWiiRuntime in Action

The **Runtime** layer translates hardware interactions (HLE) and handles the graphics FIFO and memory management.

<p align="center">
  <img src="image/video/gx-fifo-test.gif" alt="NWiiRuntime GX FIFO test" width="105%"/>
</p>

---

## nWiiStudio Interface

**nWiiStudio** is a comprehensive GUI tool built with Raylib and ImGui. It's used for DOL/ELF inspection, memory viewing, and function analysis.

### Disassembly & Function Analysis

<p align="center">
  <img src="image/1.png" alt="NWiiStudio Disassembly" width="115%"/>
</p>

### Settings & Configuration

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
