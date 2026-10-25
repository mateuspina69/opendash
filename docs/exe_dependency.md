# dash2.exe dependency: what OpenDash reads from the exe, and where it lives on the PS1 disc

Question: can OpenDash run from a PS1 disc image alone, without the user's `dash2.exe`? Today `GameMem::mapExeData`
maps the exe's data sections at their VAs and `ExeTables` pattern-scans its `.text`.

## Method

1. `--exe-coverage PATH` / `OPENDASH_EXE_COVERAGE=PATH` (src/engine/exe_coverage.hpp): one flag per exe byte. A
   byte counts only when it comes from the file and is read **before anything wrote it** (game state that lives in
   `.data` does not count); `ExeTables` reads count, its `.text` scans are flagged apart. Runs OR into PATH.
2. The flagged bytes as ranges (gap 16), with the `src/` files using each address.
3. Each range searched in `SLUS_011.40` and every non-image / non-VAB block of `COMMON/*.BIN`, `DAT/*.BIN` (raw and
   LZ-decompressed): exact, masked (PC pointer words on PS1 pointers), split (greedy longest pieces >= 8 bytes),
   none (classified).

## Result (2026-10-19, playthrough chain a1..c3 + the ST4B sub 0 cutscene; PS1 USA disc)

Coverage is a **lower bound**: only what those runs executed (at the time a2/a3/c2/c3 stopped early; see the map section for the coverage after the chain fix).

| | bytes |
|---|---|
| exe data sections (no `.text`) | 1,007,616 |
| read from the file before written | **31,723** (~3%) |
| zero-fill (`.bss`) reads | 5,224 (need no file) |
| `.text` scans (`ExeTables`) | 1,862 in 5 windows |

On the disc:

| kind | bytes | share |
|---|---|---|
| exact | 18,870 | 59.5% |
| masked (pointers differ) | 5,263 | 16.6% |
| split (pieces) | 3,950 | 12.5% |
| none | 3,174 | 10.0% |
| short (< 4 B, not searched) | 466 | 1.5% |

Found bytes: SLUS 14.7 KB, `DAT/*` overlays (type-1 blocks with a PS1 load address) 7.8 KB, `COMMON/*` 1.6 KB.

* `0x847ad8..0x84bad8` (16 KB, ~90 users) = libgte `rcossin_tbl` (4096 x {sin, cos} 4.12), byte-identical in
  `SLUS_011.40` at file offset 0x64660.
* Uncovered by class: data-ptr 2,025 (pointer lists: relocation), code-ptr 689 (dispatch tables into `.text`,
  e.g. script steps 0x8aaf80 -> 0x534000..: OpenDash's ported functions), ascii 130 (PC file names `ST4B.DAT`),
  other 330 (PC pad defaults 0x8478f0, an x87 double 0x831848, save / spawn bits, 72 B of the elephant table).

Conclusion (hypothesis until the open items close): no game **content** found so far is PC-only. What the disc
lacks is PC structure (pointers, x86 dispatch, file names, PC input) that OpenDash owns or can rebuild.

## PC VA -> PS1 address map

Corpus: `SLUS_011.40` (t_addr 0x80010000) + type-1 blocks (stage / common overlays: load address at header +0x0c,
payload at +0x30), which hold 99% of the matches above. PS1 USA, 2026-10-19, after the playthrough chain was fixed
(the oracle's pace, docs/playthrough.md: a1, a2, a3, c1, c2, c3 all pass; b1 not authored) -- 46.6 KB read, up from
31.7 KB:

| status | read bytes | share |
|---|---|---|
| located (one PS1 address, >= 8 informative bytes) | 42,770 | 91.8% |
| derived (pointer list rebuilt from its located pointees) | 200 | 0.4% |
| modified (PS1 object the PC changed; diff listed) | 476 | 1.0% |
| weak / ambiguous / unspecific / conflict | 2,149 | 4.6% |
| none | 1,009 | 2.2% |

Pointer check (each masked PC/PS1 pointer pair translated through the map): 976 ok, **0 wrong**, 451 code pointers
(not checkable), 58 pointees outside the map. Getting to 0 wrong took three rules, each found by this check:

* windows of pointers and zeros match any PS1 spot with pointers in the same places: a unique match needs >= 8
  informative bytes, growing into the unread neighbours of the table;
* one pointer list per stage overlay, all at the same PS1 address with identical bytes (e.g. PC 0x905a08 = ST42,
  0x8fbcb0 = ST37): pointer-only windows must keep the distances between close data pointers, and a range found in
  several overlays keeps the overlay whose pointers land on the PC's pointees;
* code pointers (x86 vs MIPS functions) are exempt from the distance rule.

Two more rules recover tables, not precision: a range that matches nowhere is retried as its contiguous runs of read
bytes (the gap merges objects the PS1 keeps apart), and a range still too unspecific after widening is searched as
one whole pattern with the pointer words as wildcards (step tables {0xffffNNNN, function} interleave a pointer every
other word, so no single run is specific: ST4B's 0x8aaf80 = ST4BT.BIN 0x800fa7b8).

Two statuses beyond content matches, kept apart as their evidence differs:

* **derived**: a list of only pointers has nothing to anchor a content search; each PC data pointer translated
  through the located entries gives the PS1 pointer the list must hold, and those exact PS1 bytes are searched (code
  pointers and pointers outside the map are wildcards, >= 2 translated pointers, hits in the pointees' own overlay
  preferred). 12 lists, e.g. 0x8f7f20 = ST34T.BIN 0x800fa460.
* **modified**: a range found nowhere whole but at least half made of identical pieces (>= 8 bytes) of one PS1 file;
  the pieces (PC offset -> PS1 address) and every PC byte that differs are listed -- the data OpenDash
  must define itself. Only when the pieces keep their order and shift by <= 0x40 bytes from one another (one object,
  a few bytes inserted); pieces all over the file are neighbouring objects the range merged, and the range is cut
  at the pieces and each located on its own (the sine / cosine table 0x847ad8 and the fields read just before it
  had made a 4 KB false "modified").

**Function map**: the code slots of located tables pair each MIPS function (keyed by its
PS1 file: overlays share addresses) with its x86 function -- what a disc-fed table needs to be translated to the x86
addresses OpenDash dispatches on. 291 MIPS functions -> 169 x86 functions, 0 inconsistent:

* many MIPS -> one x86 is expected: code shared by the stage overlays was linked into every overlay on the PS1 and
  once on the PC (0x42a8a0 has 19 MIPS twins), and identical functions were folded (0x52ef00, 15 twins);
* one MIPS -> several x86 was either a slot the PC **disabled** (pointed at 0x52ef00, a bare `ret`: 7 slots of
  ST10-01's table 0x88cb04; 2 within the coverage) or a **wrong location**: 0x8c3f70 (blade trail states) and
  0x8c4f50 (player states) open with the same 12-byte probe box, and with the code slots as wildcards both matched
  the PS1 player table. Rule: the window with the most informative bytes keeps the pair, the other becomes conflict.

Extending the function map past the coverage:

* **Function bodies**: a known pair's MIPS body (up to `jr ra` + delay slot, >= 8 instructions, unique in its own
  file), with the linker-filled fields masked (j / jal targets, I-type immediates), is searched in every other
  overlay: one hit there is that overlay's copy of the same x86 function. +397 pairs (690 in all). Validation: body
  matches that land on a twin already known from content: **13,175 agree, 0 disagree**.
* **Unread tables**: a run of >= 2 code pointers is translated through the pairs (wildcards for unknown words,
  >= half known) and searched; a unique, consistent hit adds its other slots. Only 5 tables (14 slots): the exe has
  1,166 such runs pointing at 5,737 distinct functions, 123 of them known -- 876 tables have no known function at
  all (per-actor state tables). The shared code has no common layout across overlays (the distance between two
  shared functions changes per overlay), so nothing seeds them: they get located when the coverage reads them.

Findings:

* **Every PS1 data object is its own global on the PC**, placed independently and 8-aligned: the PC VA - PS1 address
  offset changes from object to object even within one overlay (the map is per object), and 4 bytes of padding can
  sit between objects that are contiguous on the PS1. E.g. ST4B's script 0x59 camera table 0x8aa6e0 = ST4BT.BIN
  0x800f9f1c (all 1,726 bytes but pointers); its spawn records (0x14 bytes, `spawnRecordsImpl` steps 0x14 and reads
  up to +0x13) sit 0x18 apart on the PC: 0x8aa3f8 + 0x14 = 0x8aa40c is not 8-aligned, the next record is at
  0x8aa410. The padding is never read: those records come from the disc unchanged.
* The PC keeps every stage overlay's data apart, so a table shared by overlays exists more than once in the exe.
* **PC-modified records** (14, all `modified`): the **room param structs** (`Maybe_SetupRoomParams` 0x5f59d0,
  per sub-room; PC 0x8e6e20, 0x8ea4d8, 0x8eb2c0, 0x8d8098, 0x90bb10..; PS1 e.g. ST1D01T.BIN 0x800fc0ec) are 0x24 bytes
  on the PS1 and 0x2c on the PC: the PC retuned the fog colour +0..+2 (a0 d0 e0 / b8 e8 d8 / a8 a8 b0 vs 40 40 40)
  and appended the **D3D fog start / end** floats +0x24 / +0x28 (`Maybe_SetD3DFog` 0x401760: 0.1 / 0.6, 0.0 / 0.3,
  0.2 / 0.8, ..). Not derivable from the PS1: the PS1 fog distance +4 is 0x3333 in almost every stage while start /
  end vary independently. OpenDash reads them today (app/room_mesh.cpp: clear colour, fogStart / fogEnd); without
  the exe it needs its own per-room fog (it already has `fog=remaster`) -- a product decision.

Not located, by class: code-ptr 588 B (dispatch tables into x86 code: OpenDash's ported functions), other 426 B
(small scattered ranges, PC pad defaults 0x8478f0), data-ptr 398 B (pointer lists whose pointees are not in the map),
ascii 150 B (PC file names: `ST4B.DAT`, `XA12_18.DAT`), float 12 B (an x87 double constant 0x831848).

## Open items

* Coverage of the rest of the game (more playthrough segments; the chain stops at a2 / c2).
* Per-room fog without the exe: OpenDash's own values or model (decision pending).
* The coverage is the limit of everything above: unread tables cannot be seeded (see the function map). The chain
  now passes (a1..c3); more coverage needs more segments (b1 is not authored; the game past c3 has none).
* JP disc vs USA: the PC exe is the Japanese build; the matches above are against USA, so text-adjacent data may
  differ by version.
