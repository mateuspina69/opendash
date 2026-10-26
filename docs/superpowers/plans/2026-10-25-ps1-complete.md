# Complete PS1 disc support

**Goal:** Run the complete desktop game using only the user's USA PS1 disc, including gameplay, menus, voices, music and save/load. Completion requires evidence from gameplay paths, not just parity checks of already mapped tables.

**Architecture:** Keep the native C++ game and source abstraction. Translate original PS1 layouts at the data boundary; source every original asset from the disc. Do not introduce PC data fallbacks, silent placeholders, guessed tables or swallowed port errors.

**Tech stack:** C++20, existing SDL3/NVRHI host, local Python reverse-engineering tools, CTest and scripted application runs.

**References:** `docs/ps1_disc.md`, `docs/exe_dependency.md`, prior continuation plan.

## Work and ownership

- [x] Voices: translate PS1 eight-byte XA descriptors and logical file names to the native engine's voice requests; cover primary, secondary and built-in voices. Voice worker owns `ps1_data_source.*`, new voice modules and language-pack clip helpers.
- [ ] Tables: inventory unmapped required tables, improve verified mappings, cover introduction, initial town story, stage/room families and progress through known scripts. Table worker owns generated mapping `.inc` files, local generators and `test_ps1_source.cpp`.
- [x] Music: identify actual PS1 sequence/bank format, implement portable playback with real samples/timing/loops and integrate music slots. Music worker owns new music modules and container integration.
- [ ] Host and presentation: source-aware netplay hash; remove remaining PC-only runtime assumptions; verify original PS1 menu art, fonts, title, save/load and source selection. Root owns app/render/mixer integration, hash modules and CMake.
- [ ] Validation: fail on missing tables or stopped gameplay, add disc-only smoke/coverage tests, exercise continuous progression and menus without PC data paths, review integrated changes and complete appropriate regression suite.

## Execution decisions

- Continue in this checkout to preserve inherited work; no commit/push requested.
- Parallel workers are assigned independent subsystems under the dispatching-parallel-agents skill. Coordinate main build directory mutations through root.
- Keep local original data and diagnostic artifacts ignored; commit-worthy generated files contain only names/offsets/addresses.
- The existing `tools/playthrough.py` references `tests/playthrough/`, absent in this checkout. Find available inputs or build disc-only scripted regression cases; never claim a full playthrough based on a short idle session.
- Existing baseline `test_storm_draw` has six `drawcmd_a.bin` mismatches; identify and fix if they affect complete disc rendering, and report its actual final state.

## Review focus

- Eight-byte XA records versus four-byte engine records, channel flags, sector alignment and voice wait completion.
- Equal overlay addresses in different stage files and pointer translation after room/stage transitions.
- PS1 UI art layout versus PC screen coordinates and default language/font choice.
- Empty/malformed assets and failures during startup must terminate or surface errors, rather than produce success exit codes.
- Source fingerprinting must read the selected source only, remain independent of local image paths and reject mismatched peers.

## Progress

- Initial continuation fixed disc-only installation selection and ST09 Roll spawn point. Full disc support remains
  unfinished; this iteration is being closed at the user's request.
- User requested closing and committing this iteration because the five-hour quota is nearly exhausted. Preserve
  remaining tasks above as unfinished; do not describe this checkpoint as complete disc support.
- Added native SFX, all 424 XA clips, 172 sequence tracks, disc-specific netplay hashing and source-aware startup.
- Kuna identified verified stage/class/cabin mappings and the omitted shared-menu overlay. Mandatory-table,
  source-parity and mixer regressions cover these corrections.
- Short disc-only New Game, town, junk-shop and landing/cabin paths pass. Full campaign/save/load, title/attract
  coverage, four composed menu-art packs and the desktop engine's map page remain pending.
