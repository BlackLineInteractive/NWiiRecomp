# Dolphin signature database (third-party)

`totaldb.dsy` is **not** part of NWiiRecomp and is **not** covered by the
NWiiRecomp license in the repository root. It is redistributed here verbatim
from the Dolphin emulator project.

- Upstream: https://github.com/dolphin-emu/dolphin — `Data/Sys/totaldb.dsy`
- Copyright: the Dolphin Emulator Project and contributors
- License: **GPL-2.0-or-later** — full text in `COPYING` next to this file

## What it is

A database of ~10549 checksum→name signatures for Nintendo SDK functions. Each
entry maps a checksum computed over a function's opcodes to that function's SDK
name.

## What it is used for

Recompile-time symbol naming only. `nWiiAnalyzer` (`apply_signature_db()`)
matches each analyzed function's opcode checksum against the database and, on a
match with an equal size, records the SDK name. The names land in
`<output_dir>/sdk_symbols.csv` and make traces/debugging readable.

It has **no effect on generated code or on game behaviour**. If the file is
absent the analyzer logs `[Analyzer] No signature DB at <path>` and generation
continues normally.

## Wiring

Referenced from the per-game TOML configs:

```toml
sdk_sigdb = "third_party/dolphin-sigdb/totaldb.dsy"
```

## Licensing note

Because this file is GPL-2.0-or-later while NWiiRecomp itself is under its own
license, it is kept isolated in this directory with its own license text rather
than mixed into the project sources. The GPL applies to this file; it does not
apply to NWiiRecomp's own code, which neither derives from nor links against
Dolphin.
