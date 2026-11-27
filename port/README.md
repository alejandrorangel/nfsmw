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

**The game boots to its frontend and renders the 3D garage on Metal** --
splash, main menu and sub-menus over the frontend garage with the player's
car, driven by the game's own screens, car loader and camera mover.
Keyboard, gamepad and mouse work in the menus. Quick Race works through the
track select and the quick play brief (race database, random car in the
garage); accepting loads the region (L2RA) and currently stops at the
unported simulation `Scheduler`.

In place:

- Textures: `TexturePackNative.cpp` (native records) + `TexturePlatMetal.cpp`
- Frontend: `FERenderMetal.cpp` (`eViewPlatInterface::FE*`, `eDisplayFrame`)
- Models: `SolidNative.cpp` loads PC geometry packs (native `eSolid`, meshes
  in Metal buffers, any number of vertex buffers/strides)
- 3D: `SceneNative.cpp` (`eViewPlatInterface::Render(eModel*)`, view
  visibility tests, the frontend frame = garage + `RenderFrontEndCars`),
  `ViewsNative.cpp` (`epInitViews`), `MetalRenderer.mm` (mesh pass under the
  frontend)
- Reconstructed from speed.exe 1.3: `SelectCarCameraMover`, `StringToNumber`,
  input action generation, `Attrib::DefaultDataArea`
- `eMathNative.cpp`: the Ecstasy matrix layer (D3DX on PC)
- Lighting/shading from the PC effects (WORLD/CAR), the game's light rigs
- Gameplay: `GRaceDatabase`/`GRaceParameters`/`GRaceBin`/`GRaceCustom`,
  `GManager` vault loading, `GVault`
- `port/src/Audio/EAAudioFormat.*`: EA-XA R2 / SCxl stream / BNKl bank
  decoding (not wired to the game yet)

Not yet: reflections/env maps, sound output (the Csis/snd9 layers),
saves, movies, EAGL4 animation data (32-bit ELF objects; skeletons, world
animations and NIS scenes are skipped natively), the in-game simulation.

Aspect ratio (`NFSMW_ASPECT`): `fill` (default) scales the 640x480
frontend by window height and lets elements outside the 4:3 area use the
extra width, like the community widescreen fixes; `4:3` pillar/letterboxes;
`stretch` stretches like the PC original. (PC's `eIsWidescreen()` is hard
false, and the install only ships two widescreen screens, so the game's
own console widescreen path stays off.)

Debug switches:

- `NFSMW_AUTO_KEYS=<s>:<key>,...` (enter esc space up down left right),
  `NFSMW_AUTO_ENTER=<s>,...`: scripted key presses for automated runs
- `NFSMW_SMOKE_CAR=<car>` / `NFSMW_SMOKE_SOLIDS=<name text>`: draw models over
  the frontend with an orbit camera; `NFSMW_SCENE_ON_TOP=1` draws the 3D
  scene over the frontend; `NFSMW_SHADE=texture` ignores vertex colours
- `NFSMW_TRACE_SCENE`, `NFSMW_TRACE_ACTIONS`, `NFSMW_DUMP_CONTROLLERS`,
  `NFSMW_DUMP_MESH=<dir>`, `NFSMW_TRACE_FLOW`, `NFSMW_TRACE_FRAMES`,
  `NFSMW_TRACE_FE` (per-poly texture/mask/vertex dump), `NFSMW_TRACE_TEXTURES`,
  `NFSMW_TRACE_FILES`
- `NFSMW_SOUND=1`: sound (frontend sound effects play: native Csis runtime
  `src/Audio/CsisNative.cpp`, AEMS module banks + voices on the mixer
  `src/Audio/SndNative.cpp`; music/streams and in-game audio aren't ported).
  `NFSMW_AUDIO_LOG=1` logs Csis/AEMS/voice activity, `NFSMW_AUDIO_DUMP=<wav>`
  records the mixer output
- `NFSMW_MEMCARD=1`: turn on the unported save path
- `NFSMW_FENG_POOL=1`: frontend allocations in the FEng pool (debugging)
- `NFSMW_HOLD_FRAME=<s>`, `NFSMW_TEXTURE_SMOKE=1`

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
