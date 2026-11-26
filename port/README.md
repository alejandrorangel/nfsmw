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

## Known structural issues

- **64-bit pointers in asset data.** Chunk-loaded structs (e.g. `TextureInfo`,
  0x7C bytes) embed 32-bit pointers fixed up in place. Native loads need
  translation into native structs or 32-bit offset handles.
- **`size_t` is 64-bit.** Overloads written for 32-bit (`RotateNTo32` in
  `AttribSys.h`) become ambiguous.
- **STL.** Game containers were written against STLport/MSVC 7.1; libc++
  rejects some allocator rebinds (`UTL::Std::map` in `UStandard.h`).
