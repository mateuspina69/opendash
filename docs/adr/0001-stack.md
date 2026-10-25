# ADR 0001 — Stack, repository and timing for the OpenDash native port

Date: 2026-10-06. Status: accepted (M0).

## Context

OpenDash is a standalone executable that reads the original `.DAT` files of an installed Rockman Dash 2 PC (and
per-stage tables from the user's own `dash2.exe`) and reimplements the game. The reverse-engineering prototypes live
(Python) and serve as golden references.

## Decision

- **Language/build:**
  - C++20, CMake ≥ 3.28, MSVC (VS 2026, x64);
  - dependencies through **vcpkg manifest mode**, currently only `sdl3`.
- **Platform layer:**
  - **SDL3** for window, input (keyboard + gamepad), timing and later audio;
  - M0 uses `SDL_Renderer` (2D) for debug views;
  - the 3D renderer (SDL_GPU, or D3D11 if SDL_GPU blocks us) is decided in M1.
- **Timing:**
  - fixed-step simulation at **30 Hz**, the game's gameplay rate (`pace_multiplier = 2`);
  - rendering runs at display rate with interpolation (`alpha` = accumulator / step);
  - an uncapped frame rate cannot speed up physics.
- **Data:**
  - no game asset is committed;
  - the `.DAT` directory comes from `OPENDASH_DAT_DIR` or `opendash.ini` (`dat_dir=`);
  - it defaults to the standard install path.
- **Tests:** CTest. A setup fixture runs, which produces reference outputs; C++ tests compare against them
  byte-for-byte.
- **Repository:**
  - `MML2_Recreation/opendash`, in the existing local git repo;
  - **never pushed to any remote**;
  - code is reimplemented from documented behaviour. Decompiler output is not copied, and leaked
    source is never used.

## Consequences

- The Python extractor stays the reference implementation. Every C++ port of a format gets a golden test.
- The UE 5.8 project is on hold; OpenDash does not depend on it.
