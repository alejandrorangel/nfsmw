# Native macOS / Metal port

Work-in-progress native Apple Silicon build of NFS: Most Wanted, rendering with
Metal. Lives only in this fork; it is independent from the matching decomp
build (`configure.py` + ninja), which keeps working unchanged.

## Build & run

```sh
cmake -S port -B build/port -G Ninja       # Debug by default
ninja -C build/port nfsmw_game             # compile the decompiled game
python3 port/tools/gen_stubs.py            # regenerate link stubs (after upstream merges)
ninja -C build/port nfsmw                  # link build/port/NFSMW.app

# run against a PC Black Edition install
NFSMW_DATA=~/Games/NFSMW build/port/NFSMW.app/Contents/MacOS/NFSMW
NFSMW_DATA=~/Games/NFSMW port/tools/crashtrace.sh   # stubs hit + backtrace under lldb
python3 port/tools/port_report.py          # categorised native compile errors
```

The app owns `main()` and runs the game's (renamed) `NFSMW_GameMain` on a
64 MB-stack thread, with `NFSMW_DATA` as the working directory.

### Link stubs

Most of the game isn't decompiled, so `gen_stubs.py` emits
`src/Stubs/GeneratedStubs.S`: a weak definition for every symbol the game
references but nothing defines.

- functions log `[stub] <name>` once and return 0 (`NFSMW_STUBS_FATAL=1`
  aborts instead, to catch the first one in a debugger)
- data is zero-filled (sized from the GameCube symbol table, doubled)
- vtables point every slot at a per-class logging trap

Stubs are weak, so decompiled or port-implemented code always wins; just
re-run the generator to drop stale ones.

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

**The game draws its boot loading screen on Metal** (`loading_boot.fng`:
MW logo on the frontend backdrop), through its own frontend renderer.

Boot then continues to `LoadGlobalChunks()` and stops at the attribute
database (`db.vlt`, another in-place pointer format).

Rendering pieces in place:

- `src/Platform/TexturePackNative.cpp`: texture packs copied into native
  records; `TexturePlatMetal.cpp` turns them into Metal textures
- `src/Platform/FERenderMetal.cpp`: `eViewPlatInterface::FE*` +
  `eDisplayFrame` record frontend quads (640x480 virtual screen)
- `src/Mac/MetalRenderer.mm`: draws the latest frontend frame, letterboxed
- `src/Platform/eMathNative.cpp`: the Ecstasy matrix layer (D3DX on PC)

Debug switches: `NFSMW_HOLD_FRAME=<s>` keeps ticking/drawing the frontend
once it first draws; `NFSMW_TRACE_FRAMES`, `NFSMW_TRACE_FE`,
`NFSMW_TRACE_TEXTURES`, `NFSMW_TRACE_FILES`; `NFSMW_TEXTURE_SMOKE=1`.

## Recovered data

`tools/gen_data.py` reads initialised tables straight out of `speed.exe`
(PE reader in `tools/pe.py`) into `src/Data/RecoveredData.cpp`; add a spec
entry per table. Global objects with constructors (whose args come from the
PC static initialisers) live in `src/Data/RecoveredObjects.cpp`.

## Conventions learned so far

- Integer callback params that carry pointers use `intptr_t`/`uintptr_t`.
  In matching builds `types.h` `#define`s these to `int`/`unsigned int`, so
  the change is type-identical there.
- Values that live in asset data stay 32-bit (e.g. `WGridNodeElemTag` is
  `unsigned int` natively, not `uintptr_t`).
- `bNewSlotPool` doubles slot sizes natively (most callers pass 32-bit
  sizeof literals; a struct at most doubles on 64-bit).
- `delete`/`bFree` route by address: game-pool memory vs system heap.
- Hard-coded 32-bit struct sizes (`+ 0x14` for `AllocationHeader`,
  `char[96]` for `MemoryPool`) become `sizeof(...)`.
- Plain `operator new/delete` stay on the system allocator natively (AppKit
  and libc++ share them); game pool memory comes from explicit `bMalloc`.
- `override` is `#define`d away in the C++98 matching builds; decomp
  signature TODOs marked `override` that don't actually override are
  un-marked.

## Known structural issues

- **64-bit pointers in asset data.** Chunk-loaded structs (e.g. `TextureInfo`,
  0x7C bytes) embed 32-bit pointers fixed up in place. Native loads need
  translation into native structs or 32-bit offset handles.
- **Linking.** Most of the game is not decompiled yet; linking will need a
  stub layer for every unresolved symbol.
