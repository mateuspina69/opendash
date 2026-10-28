# OpenDash

OpenDash is an open-source reimplementation of the engine of *Mega Man Legends 2* (PC version, 2002), written in
C++20 with SDL3 and NVRHI (Direct3D 12 on Windows, Vulkan on Linux). It is in the spirit of projects like OpenLara: the code here is new,
and the game itself — its data files and its executable — must come from a copy you own.

**This repository contains no game data, no executable code and no assets of the original game.** OpenDash reads
the `.DAT` files and the per-stage tables of your own installed PC copy at run time.

## Status

- **PC version:** the game can be played from start to finish on OpenDash with the data of the PC version.
- **PS1 disc:** support for running from the USA PS1 disc (`.cue`/`.bin`) is in progress. It already starts
  without the PC installation, but parts of the game are still missing; see [PS1 disc](#ps1-disc).

## Download

Windows builds are on the [Releases](https://github.com/mateuspina69/opendash/releases) page. Unzip the folder
anywhere and run `opendash.exe`; it needs a DirectX 12 GPU and the
[Microsoft Visual C++ Redistributable (x64)](https://aka.ms/vs/17/release/vc_redist.x64.exe). The game's own
files still come from your copy (see below).

## Requirements

- An installed copy of the PC version of *Mega Man Legends 2* (the `dat/` folder and `dash2.exe`).
- Windows 10/11, Visual Studio 2026 (C++ workload), CMake 3.28+, [vcpkg](https://vcpkg.io) (manifest mode); or
  Linux with GCC 13+ / Clang 17+, CMake 3.28+, Ninja, SDL3, stb, NanoSVG, DXC and a Vulkan 1.3 driver.
- Python 3.11+, and Git with Git LFS (the configure fetches the upscaler SDKs).

## Building

```
cd opendash
cmake --preset vs2026
cmake --build --preset release
```

The configure fetches the pinned upscaler SDKs (DLSS, FSR, XeSS, NIS) into `third_party/` (not in git) through
`tools/fetch_sdks.py`. Every build includes all four; each one is offered at run time
when its DLL is next to the exe and the GPU supports it.

On Linux (Arch: `pacman -S cmake ninja sdl3 stb nanosvg directx-shader-compiler vulkan-icd-loader`; NVRHI, Dear ImGui
and DirectXMath are fetched by the configure at pinned versions):

```
cmake --preset linux
cmake --build --preset linux-release
```

The Linux build renders through Vulkan (dynamic rendering, synchronization2). Its upscaler is FSR 3.1, built when
`python tools/fetch_sdks.py --only fidelityfx-vk` has fetched the SDK and `glslangValidator` is installed; the
depth buffer is D32 with the scene's depth written at D24 precision, so coplanar faces tie as on Direct3D 12.

On its first start, if the game is not installed in `C:/Program Files (x86)/Capcom/Dash2` (on Linux, the same path in
`~/.wine`), OpenDash asks for the game's folder (the one with `dash2.exe`, `dat/` and `snd/`) and remembers it as
`dat_dir=` / `exe=` in `opendash.ini` next to the executable. The environment variables `OPENDASH_DAT_DIR` / `OPENDASH_EXE` override
it. Every setting of `opendash.ini` is described in [docs/configuration.md](docs/configuration.md). The tests take `OPENDASH_DAT_DIR`, `OPENDASH_EXE` and `OPENDASH_GAME_DIR` from the CMake cache.

## PS1 disc

Running from the USA PS1 disc (`SLUS_011.40`, a `.cue` with its raw `.bin` tracks) is a work in progress. Set
`data=disc`, `disc=<path to .cue>` and `lang=en` in `opendash.ini`. It starts without the PC installation, but
it cannot yet play through the whole game; see
[PS1 setup and current limitations](docs/ps1_disc.md#running-the-disc-data-source-2026-10-25).

## Tests

The tests compare OpenDash with data recorded from your own copy of the game. That data is derived from the game and
is never committed (`tests/data/` is ignored), so it must be generated locally before `ctest` can run.

## Layout

- `src/engine/` — platform, data loading (PC installation and PS1 disc).
- `src/render/` — renderer, shaders, upscalers.
- `src/audio/` — sound effects, music and voices.
- `src/game/` — game systems, one file per system.
- `src/content/<area>/` — the scripts, rooms and actors of each area.
- `src/app/` — window, renderer host, debug UI.
- `src/net/` — netplay (see `docs/netplay.md`).
- `tests/` — the tests; `docs/` — design notes.

## Legal

OpenDash is an unofficial fan project. It is not affiliated with, endorsed by, or sponsored by Capcom.
*Mega Man*, *Mega Man Legends* and all related names and characters are trademarks of Capcom Co., Ltd.; they are
used here only to describe what game this software is compatible with. No copyrighted material of the original game
is distributed with this project; you need a legally obtained copy of the game to use it.

The OpenDash source code is released under the BSD 2-Clause license (see `LICENSE`). The license covers only the
original work of the OpenDash contributors; it grants no rights to the original game, its code, its data or Capcom's
trademarks. Bundled third-party files keep
their own licenses (the M PLUS Rounded 1c fonts are under the SIL Open Font License, see
`assets/fonts/OFL.txt`).
