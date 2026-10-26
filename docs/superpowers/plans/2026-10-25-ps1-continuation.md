# PS1 data source continuation

**Goal:** Continue the existing disc-backed PC engine, starting with failures in playable Yosyonke. This is support for reading a USA PS1 disc on the PC host.

**Architecture:** Keep `Ps1DataSource`, its address-only mappings and the existing game systems. Read all original bytes from the user's disc; do not supply PC data as a fallback.

**References:** `docs/ps1_disc.md`, `docs/exe_dependency.md`.

## Constraints and decisions

- Preserve the inherited diagnostic patch in `build/ps1-inherited-diagnostics.patch` while investigating; remove its Windows-only stack tracing from the portable engine once the diagnosis is complete.
- Work in the supplied checkout, preserving existing work. Do not commit or change the user's configuration.
- Existing PS1 parity tests skip missing tables. Add explicit assertions for tables needed by the reproduced gameplay path.
- Disc-only startup must not require the PC installation; check the startup and audio paths before claiming that it works.

## Tasks

- [x] Reproduce the missing `RollSt09FollowerPoint` failure; add a failing required-table check to `tests/test_ps1_source.cpp`; locate its original disc bytes and add its address to `src/engine/ps1_tables.inc`.
- [x] Run scripted Yosyonke sessions, both story 0 and story 1, and investigate any further source/identity failures exposed by those runs. Add regression coverage before each fix.
- [x] Check disc-only startup with deliberately absent PC paths. Resolve PC installation checks that still block the disc source, with focused coverage.
- [x] Rebuild affected targets, run PS1 and related PC regression tests, review the diff, and document verified behavior and remaining port gaps in `docs/ps1_disc.md`.

## Initial evidence

- Four existing tests pass: `test_disc`, `test_ps1_files`, `test_ps1_source`, `test_lang_pack`.
- Scripted ST09/story 0 stops at `RollSt09FollowerPoint`; story 1 runs five ticks.
- The missing point is at `DAT/ST09T.BIN:0x800f2cf4`, immediately after its mapped spawn record, with the same eight bytes as the PC record. The checked-in mapping contains only addresses.
- `ensureGameFiles` still unconditionally checks `dat/INIT.DAT`, `snd/`, and `dash2.exe`.

## Verification and handoff

- Release build succeeded (`cmake --build build --config Release --parallel 6`).
- Final focused CTest run: 7/7 pass (`test_disc`, `test_game_files`, `test_ps1_files`, `test_ps1_source`, `test_lang_pack`, `test_tables`, `test_spawn`).
- Both final ST09 sessions exit successfully after 120 ticks, without failed/stopped/port-gap messages. PC data/exe paths point at nonexistent `build/no-pc/`; story 1 receives an explicit save path.
- Independent review found that directory entries alone could accept corrupt/truncated images. Fixed by validating the boot executable's magic/text extent and reading INIT. A synthetic ISO regression failed before the fix and passes after it.
- The broad 109-test run was stopped during long cinematic golden groups after 28 completed tests: 27 passed, `test_storm_draw` failed with the same six `drawcmd_a.bin` mismatches recorded before this task. Do not report the full suite as passing. Full output: `build/ps1-final-tests.log`.
- Further source gaps are recorded in `docs/ps1_disc.md`: voices require record conversion, ST0A's mouth table is missing, the introduction's fade direction table is missing, sequenced music and disc netplay remain open.
- Inherited limitation deferred: SDL's UTF-8 picker output and narrow filesystem/config path conversions need a project-wide Unicode pass, including the existing PC folder picker.
- Existing diagnostic work is preserved at `build/ps1-inherited-diagnostics.patch`; the engine no longer includes its temporary Windows-only stack tracer. No commits or user configuration changes were made.
