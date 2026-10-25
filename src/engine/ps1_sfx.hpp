// PS1 sound banks (block 0x05) in the PC SFX bank layout (DAT segment type 8; game/audio_logic loadSfxBank). The PC
// converted every PS1 bank one to one: one entry per 20-byte tone record (same order), the samples decoded from SPU
// ADPCM without resampling, the entry frequency from the record's pitch. What differs is the voice barks (the USA
// script's): ps1SfxBank rebuilds a bank from the disc so the game loads it in place of the PC's.
//
// Block (after u32 type, size, sectors): +0x0c u32 extra << 16 | arg (the PC TOC's bank slot / id), +0x10 u32 SPU base
// address, +0x14 u32 table bytes, +0x18 u32 body bytes, +0x1c u16 8-byte header items before the records. Table (from
// +0x30): the header items, then 20-byte records: +0 u32 SPU address, +6 volume, +8 centre note, +9 fine (1/128
// semitone), +0xa key played, +0x12 u16 sample index. Body: from the first sector boundary after the table, + 0x10; a
// record's sample starts at (address - base) there and runs to its end frame (flag bit 0), the loop at a later flag-4
// frame when the end frame repeats (bit 1).
#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace od {

// SPU ADPCM frames (16 bytes: shift / filter, flags, 28 nibbles) -> PCM16
std::vector<int16_t> decodeSpuAdpcm(std::span<const uint8_t> frames);

// the block's PC TOC key: extra << 16 | arg
uint32_t ps1SfxBankKey(std::span<const uint8_t> block);

// The PC-layout bank of a PS1 sound bank block (the whole block, header included), given the PC's bank for the same
// slot: an entry takes the PS1 record's sample + 1, volume and frequency (u16(44100 * 2^((key - note + fine / 128) /
// 12))) except where the PC's entry is silent or disabled; a sample the block has no complete copy of is the PC's.
// Empty when the bank equals the PC's (the same entries, every played sample within the PC decoder's rounding of 16
// and the same loop): the PC's stays. Throws std::runtime_error on a malformed block or PC bank.
std::vector<uint8_t> ps1SfxBank(std::span<const uint8_t> block, std::span<const uint8_t> pcBank);

} // namespace od
