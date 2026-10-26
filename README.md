# OpenDash

OpenDash is an open-source reimplementation of the engine of *Mega Man Legends 2* (PC version, 2002), written in
C++20 with SDL3 and NVRHI (Direct3D 12 on Windows, Vulkan on Linux). It is in the spirit of projects like OpenLara: the code here is new,
and the game itself — its data files and its executable — must come from a copy you own.

**This repository contains no game data, no executable code and no assets of the original game.** OpenDash reads
the `.DAT` files and the per-stage tables of your own installed PC copy at run time.

## Requirements

- An installed copy of the PC version of *Mega Man Legends 2* (the `dat/` folder and `dash2.exe`).
- Windows 10/11, Visual Studio 2026 (C++ workload), CMake 3.28+, [vcpkg](https://vcpkg.io) (manifest mode); or
  Linux with GCC 13+ / Clang 17+, CMake 3.28+, Ninja, SDL3, stb, NanoSVG, DXC and a Vulkan 1.3 driver.
- Python 3.11+ for the tools.

## Building

```
cd opendash
cmake --preset vs2026
cmake --build --preset release
```

The first configure fetches the pinned upscaler SDKs (DLSS, FSR, XeSS, NIS) into `third_party/`. Every build
includes all four; each one is offered at run time when its DLL is next to the exe and the GPU supports it.

On Linux (Arch: `pacman -S cmake ninja sdl3 stb nanosvg directx-shader-compiler vulkan-icd-loader`; NVRHI, Dear ImGui
and DirectXMath are fetched by the configure at pinned versions):

```
cmake --preset linux
cmake --build --preset linux-release
```

The Linux build renders through Vulkan (dynamic rendering, synchronization2) and has no upscalers yet (`aa=none`); the
depth buffer is D32 with the scene's depth written at D24 precision, so coplanar faces tie as on Direct3D 12.

On its first start, if the game is not installed in `C:/Program Files (x86)/Capcom/Dash2` (on Linux, the same path in
`~/.wine`), OpenDash asks for the game's folder (the one with `dash2.exe`, `dat/` and `snd/`) and remembers it as
`dat_dir=` / `exe=` in `opendash.ini` next to the executable. The environment variables `OPENDASH_DAT_DIR` / `OPENDASH_EXE` override
it. The tests take `OPENDASH_DAT_DIR`, `OPENDASH_EXE` and `OPENDASH_GAME_DIR` from the CMake cache.

An experimental USA PS1 disc data source is also available with `data=disc`, `disc=<path to .cue>` and
`lang=en` in `opendash.ini`. It can start without the PC installation, but has known gameplay gaps; see
[PS1 setup and current limitations](docs/ps1_disc.md#running-the-disc-data-source-2026-10-25).

## Tests

The tests compare OpenDash with data recorded from your own copy of the game. That data is derived from the game and
is never committed (`tests/data/` is ignored), so it must be generated locally before `ctest` can run.

## Layout

- `opendash/src/engine/` — platform, data loading, rendering.
- `opendash/src/game/` — game systems, one file per system.
- `opendash/src/content/<area>/` — the scripts, rooms and actors of each area.
- `opendash/src/app/` — window, renderer host, debug UI.
-  — Python reference implementations of the data formats.
-  — records reference data from your copy of the game for the tests.

## Legal

OpenDash is an unofficial fan project. It is not affiliated with, endorsed by, or sponsored by Capcom.
*Mega Man*, *Mega Man Legends* and all related names and characters are trademarks of Capcom Co., Ltd.; they are
used here only to describe what game this software is compatible with. No copyrighted material of the original game
is distributed with this project; you need a legally obtained copy of the game to use it.

The OpenDash source code is released under the BSD 2-Clause license (see `LICENSE`). The license covers only the
original work of the OpenDash contributors; it grants no rights to the original game, its code, its data or Capcom's
trademarks. Bundled third-party files keep
their own licenses (the M PLUS Rounded 1c fonts are under the SIL Open Font License, see
`opendash/assets/fonts/OFL.txt`).
