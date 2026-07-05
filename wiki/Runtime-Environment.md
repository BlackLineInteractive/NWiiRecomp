# Runtime Environment (`nWiiRuntime`)

The High-Level Emulation (HLE) layer serves as the host interface for the recompiled executable. Because the AOT translation strips away the original CPU environment, `nWiiRuntime` provides substitute mechanisms for OS execution, memory, interrupts, and I/O.

## Operating System APIs (HLE Hooks)

To avoid recompiling low-level library stubs or standard OS boilerplate, `nWiiRuntime` hooks directly into the host execution via configuration logic (`[hle_hooks]` in `config.toml`).

- **OSInit / Kernel Initialization**: Low-memory layouts are populated directly. When the guest calls `OSInit`, the runtime simulates basic system memory maps, ensuring standard allocations pass.
- **IPC / Starlet (Wii)**: The runtime intercepts `IOS_Open`, `IOS_Ioctl`, and `IOS_Close` commands. `/dev/di`, `/dev/fs`, `/dev/es`, `/dev/stm`, and `/dev/usb` endpoints are serviced natively, directly translating system calls into native filesystem or virtual device logic.
- **Hardware Mailboxes**: DSP requests and synchronization commands are monitored through MMIO trapping instead of cycle-accurate device modeling.

## Device Emulation (MMIO)

Memory-mapped I/O requests bypass native pointer dereferencing and are handled by a dedicated MMU abstraction layer within the `CPUContext`:

- **VI (Video Interface)**: Framebuffer pointers and dimensions are tracked to facilitate rasterization timing and flip commands.
- **DI (Drive Interface)**: Implemented as a virtual disk emulator. Read commands translate directly into asynchronous file I/O operations against extracted filesystem structures (`sys/`, `files/`) or directly mounted `.iso`/`.wbfs` images.
- **EXI (External Interface)**: Simulates the RTC (Real-Time Clock) and SRAM blocks. Correct checksums are generated on the fly so the guest OS boots without configuration corruption errors.
- **SI (Serial Interface)**: Emulates the Joybus protocol. Controller input state (from keyboard, generic gamepads, or UDP packets) is serialized into the bit formats expected by GC/Wii polling logic.

## Interrupt Handling

The PI (Processor Interface) module drives exception dispatch:

- Hardware interrupts (e.g., VI VBlank, DI transfer complete) raise bits in the PI Interrupt Cause register (`INT_CAUSE_*`).
- The runtime immediately vectors execution to the guest `__OSInterruptTable`.
- `mtdec` and `mfdec` triggers are implemented via native high-resolution timers; decrements drive the standard `OSAlarm` dispatcher efficiently without tick-level polling.

## Interpreter Fallback

A fully functional, integer-only PPC750 emulator is embedded within `nWiiRuntime`.

- Execution is seamlessly transferred to the interpreter whenever `ctx.pc` points to a memory segment that was not part of the `.dol` file at recompilation time.
- Use cases include DSP bootcode routines stored below the standard heap arena, runtime-patched code, dynamically linked overlays, and custom memory clear loops copied to low RAM.
