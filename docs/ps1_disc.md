# The PS1 disc's files vs the PC `dat/`

Question: does the PS1 disc (USA, `SLUS_011.40`) carry the data OpenDash reads from the PC `dat/*.DAT`? Read through
engine/disc_image; compared 2026-10-25 against the PC install (JP build).

## Files

The disc's `COMMON/*.BIN` and `DAT/*.BIN` have the PC's names, one for one (`ST4B.DAT` = `DAT/ST4B.BIN`,
`PL00T.DAT` = `COMMON/PL00T.BIN`), 331 of the PC's 345 files. Apart:

* PC only: `MAP0`..`MAP8` (8 x 68,120 B, one type-4 image each), `SUBSCN01/02/03/06`, `STFF`, `STFFT`.
* Disc only: `COMMON/SUBMAP.BIN`, `COMMON/SUBSCN.BIN` (one raw image each: the PC split them per screen),
  `COMMON/PL00R01.BIN` (the PC's table has no `PL00R01`), `XA/*.XA` (14 streams = the PC's `snd/xa*.dat`),
  `ZNULL.DAT` (the CD-DA track 2).

## Container

The PC file is a TOC (16-byte `{type, size, arg, extra}` entries up to `dummy header`, payloads from 0x800,
engine/dat). The PS1 file is a chain of sector-aligned blocks, each with a 0x30-byte header `{type, size, sectors,
...}` (`sectors` = 0 ends the chain) and its payload at +0x30, raw or LZ-compressed. The block types are numbered
apart from the PC's:

| PC segment | PS1 block | identical | note |
|---|---|---|---|
| 0 code / data at a PS1 address | 0x01 raw (address at +0x0c) | 100 / 100 | |
| 1 room actor models | 0x0c LZ (bitfield bytes at +0x10), 0x0a raw | 150 / 151 | ST37 #1: 1 byte differs |
| 2 room geometry | 0x0d LZ (bitfield bytes at +0x10) | 116 / 120 | ST08 #0: 7 bytes; ST0B #0, ST56 #0 differ in size; ST26T #66 matches no 0x0d block of ST26T |
| 3 CLUT, 4 page + CLUT, 5 image | 0x02 raw, 0x03 LZ | 7,249 / 7,374 VRAM uploads | see below |
| 6 message bank | 0x12 raw | 0 / 102 | PS1 text is the USA script in the PS1 Latin font's encoding |
| 7 font bitmaps (INIT) | -- | 0 / 2 | the PC's are the JP font (24 / 48 px glyphs) |
| 8 SFX bank (RIFF PCM16 WAVs) | 0x05 VAB (SPU ADPCM) | -- | needs an ADPCM decoder |
| 9 music (MP3) | 0x0e + 0x10 per track (106 / 104 in `ST*T`) | -- | hypothesis: sequence + its VAB; the PC's MP3s are recordings |

Images: a PS1 0x02 / 0x03 header holds up to two VRAM rects `{x, y}` / `{w, h}` (u16 pairs) at +0x0c / +0x10 and
+0x14 / +0x18 (the CLUT, then the page); the payload is their pixels back to back, except in a raw 0x02 whose size
is 0x7d0 + the page: the CLUT at +0x30 and the page sector-aligned at +0x800 (202 blocks). A 0x03's bitfield length
is the low 16 bits of +0x24 (the high half is a flag, 1 or 2). Compared as uploads (rect + pixels) against the PC's
type 3 / 4 / 5 sub-blocks: 7,249 identical, 55 same rect with other pixels, 70 rects the PS1 does not upload, in 12
files -- the PC's front-end art (`G_OVER0x`, `SELECT`, `LOGO` at x >= 640, 480 lines high), pages of `GAME`,
`DEMO`, `INIT`, and `ST01T` / `ST1FT` / `ST1F0xT` / `ST20T` (hypothesis: JP text in the images).

Blocks with no PC segment counterpart: 0x15 (945) and 0x00 (316) are all in `COMMON/STAFF.BIN` (the staff roll:
not stage data); 0x08 (20), 0x09, 0x0f open. The stage tables the PC carries in the exe (docs/exe_dependency.md)
are in the 0x01 blocks: 134 of them have no PC counterpart -- the stage overlays (MIPS code and data at their load
address), which hold 99% of the exe ranges located so far.

## Consequences for running from the disc

* The room data (models, geometry) and the code-address data come off the disc byte-identical: a PS1 container
  reader with the LZ is all they need. The 5 differing blocks are version differences to read one by one.
* Images: the same, through the VRAM uploads; the PC-only front-end art has no PS1 source (the PS1 screens are
  the 320 / 640-wide originals).
* Text and fonts change language: the disc gives the USA script in the PS1 font, not the PC's JP text.
* Sound needs decoders the PC never had: SPU ADPCM for the VAB SFX, a sequence player for the music, XA ADPCM for
  the streams.

## Text

The English text comes off the disc whole (engine/lang_pack, held to a pack folder by test_lang_pack): the message
banks are the 0x12 blocks of `DAT/<STEM>.BIN` (102 banks, same stems and message ids as the PC's type-6 banks), the
font is the 256x256 4bpp page of `COMMON/INIT.BIN`'s first block, the glyph widths are 256 bytes at `SLUS_011.40` RAM
0x8006b90c with the width limit in the immediate of `sltiu` at 0x80049b10, and the text / button CLUTs are the u16
CLUT ids at 0x80078c0c / 0x8006b988 read from INIT.BIN's VRAM. The PS1 copies of the 26 banks the PC keeps in
dash2.exe (item names / descriptions, the sub-screen texts, the options, the equipment, the shop, the save places,
the load / save menus, Roll's development room) sit at fixed offsets of `SLUS_011.40`, `COMMON/SUBSCN.BIN`,
`COMMON/DEMO.BIN`, `DAT/ST0AT.BIN` and `DAT/ST04T.BIN`, with the shop's 7 price strings. Not read from the disc yet:
the art, the SFX banks, the voices.
