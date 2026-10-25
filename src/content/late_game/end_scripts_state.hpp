// The parts of the script / stage block that end_scripts.cpp keeps (game/script_state ScriptPart,
// game/stage_state StagePart): their members and the exe offsets they stand for.
#pragma once

#include <cstdint>

#include "game/script_state.hpp"

namespace od::port::end_scripts_state {

// 0x8adabc): the RNG saved in the script's bytes, seeded 0x873ca9e6; skips locked until step 0; the end requests ST57.
// The script's own bytes: the saved RNG, the player's push
struct SavedRngWalkFields : ScriptPart<SavedRngWalkFields> {
    uint32_t savedRng = 0; // +0xc
    int16_t walk = 0; // +0x10
};

// 0x8a80f0): each step is one sub-room scene ending in a request to ST52 sub 1..4; the last returns to ST04 sub 0 and
// advances the story. The pool-0xa0 cast 0x8a7a70 (the cast's slot 0x12, its last parameter byte a brightness step)
// fades in and out.
// The script's own bytes: the saved RNG, the player's push, the VRAM flip-book's phase and row
struct Script3fFields : ScriptPart<Script3fFields> {
    uint32_t savedRng = 0; // +0xc
    int16_t walk = 0; // +0x10
    uint8_t flip = 0; // +0x12
    uint8_t flipRow = 0; // +0x13
};

// The script's own bytes: the unit at which the next page comes, a fade counter, the page row, the page's brightness,
// shown, the background's brightness (0x500..0x800), its ramp (1 down, 2 up), the tick / unit counters
struct Script6eFields : ScriptPart<Script6eFields> {
    int16_t nextUnit = 0; // +0xc
    uint16_t fade = 0; // +0xe
    uint32_t page = 0; // +0x10
    uint8_t pageBrightness = 0; // +0x14
    uint8_t shown = 0; // +0x15
    uint16_t background = 0; // +0x16
    uint8_t ramp = 0; // +0x18
    uint8_t ticks = 0; // +0x19
    uint16_t units = 0; // +0x1a
};

// step tick into the cast timeline. The cast 0..5 come from the camera sequence; their state / sub-state / step are
// their own state / action / phase requests (content/late_game/late_cast).
// The script's own bytes past the timeline: the grey of the closing sprite, a byte it sets
struct Script6fFields : ScriptPart<Script6fFields> {
    uint8_t grey = 0; // +0x10
    uint8_t level = 0; // +0x11
};

// 0x8ae6ec): the RNG is saved (advanced once) in the stage's block and seeded 0x873ca9e4 for the cutscene, restored
// at the end.
// The script's own bytes: the driven walk's counter and its pose ticks, the side it walks on, the pose's frame, a byte
// it clears, the shake's ticks
struct Script75Fields : ScriptPart<Script75Fields> {
    uint16_t counter = 0; // +0xc
    uint16_t poseTicks = 0; // +0xe
    uint8_t side = 0; // +0x10
    uint8_t frame = 0; // +0x11
    uint8_t cleared = 0; // +0x12
    uint8_t shakeTicks = 0; // +0x14
};

// 0x53be40, its phases [0x8adc9c + phase * 4]), pages 0x8adad8 (0x10 bytes each: +1 the page's length in units of 0x13
// ticks, +2 the hold ticks; 0xff ends)). dat\staff.dat's segments are parsed one per page. The page (exe 0x53c0c0)
// and background (exe 0x53c2b0) draws are render output.
// The staff roll's globals: the next segment, the file's buffer, the skip taken
struct StaffRollState {
    uint32_t segment = 0; // 0xa59fa0
    uint32_t buffer = 0; // 0xa59fa4
    uint8_t skipped = 0; // 0x7ce236
};
inline StaffRollState& staffRoll() {
    static StaffRollState s;
    return s;
}

} // namespace od::port::end_scripts_state
