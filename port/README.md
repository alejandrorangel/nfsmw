# Native macOS / Metal port

Work-in-progress native Apple Silicon build of NFS: Most Wanted, rendering with
Metal. Lives only in this fork; it is independent from the matching decomp
build (`configure.py` + ninja), which keeps working unchanged.

## Build

```sh
cmake -S port -B build/port -G Ninja
ninja -C build/port nfsmw          # app shell: build/port/NFSMW.app
python3 port/tools/port_report.py  # compile all game units, write build/port/port_report.md
```

`nfsmw_game` compiles the same `Speed/Indep/SourceLists/z*.cpp` unity units
as the PC (`SPEED_EXE_1_3`) target. It is not linked into the app until
`-DNFSMW_PORT_LINK_GAME=ON`, which needs the decomp to be complete enough to
resolve.

## Layout

| path | purpose |
|---|---|
| `include/PortConfig.h` | force-included; selects the PC (`EA_PLATFORM_WIN32`) code paths and defines `EA_PORT_NATIVE` / `EA_PORT_MACOS` |
| `include/shims/` | stand-ins for Windows/D3D/MSVC headers the game expects |
| `src/Mac/` | Cocoa app, Metal renderer, later input/audio/file I/O |
| `tools/port_report.py` | categorised list of what fails to compile natively |

## Rules for touching shared decomp code

- Prefer fixing things in `port/` (shims, PortConfig). When a shared file must
  change, guard it with `#if defined(EA_PORT_NATIVE)` so the matching build is
  byte-identical and `git merge upstream/main` stays painless.
- Never send port changes upstream (upstream bans AI-assisted porting work).

## Status

All 32 game units compile natively for arm64 (`port_report.py` reports 0
errors). Nothing is linked or run yet.

## Conventions learned so far

- Integer callback params that carry pointers use `intptr_t`/`uintptr_t`.
  In matching builds `types.h` `#define`s these to `int`/`unsigned int`, so
  the change is type-identical there.
- Values that live in asset data stay 32-bit (e.g. `WGridNodeElemTag` is
  `unsigned int` natively, not `uintptr_t`).
- `override` is `#define`d away in the C++98 matching builds; decomp
  signature TODOs marked `override` that don't actually override are
  un-marked.

## Known structural issues

- **64-bit pointers in asset data.** Chunk-loaded structs (e.g. `TextureInfo`,
  0x7C bytes) embed 32-bit pointers fixed up in place. Native loads need
  translation into native structs or 32-bit offset handles.
- **Linking.** Most of the game is not decompiled yet; linking will need a
  stub layer for every unresolved symbol.
