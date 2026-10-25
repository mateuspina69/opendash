// The fade task's state (game/fade runs it): the block 0xa72260 the fade requests (exe 0x53f6b0) set up and the task
// (task 2: the fades exe 0x53f950, the wipes exe 0x53fa40..0x53fac0, the slides) steps, the background flag a wipe
// saves, and the exe's tables of the fade requests.
#pragma once

#include <cstdint>

#include "engine/data_source.hpp"
#include "game/state_array.hpp"

namespace od::port {

// +0: the task's kind (the request's range picks it)
enum class FadeType : uint8_t {
    Darken = 0,   // requests 0x01..0x04 / 0x10..0x14: a black tile's level (subtractive)
    Brighten = 1, // requests 0x09..0x0c / 0x19..0x1c: a white tile's level (additive: the script flashes)
    Wipe20 = 2,   // the wipes, named by the requests that start them: 0x20..0x23
    Wipe2A = 3,   // 0x2a..0x2b
    Wipe24 = 4,   // 0x24..0x25
    Wipe26 = 5,   // 0x26..0x27
    Wipe28 = 6,   // 0x28..0x29
    Slide30 = 7,  // the slides: 0x30..0x31
    Slide32 = 8,  // 0x32..0x33
    Slide34 = 9,  // 0x34
};
// +1 of the fades (Darken / Brighten)
enum class FadePhase : int8_t {
    Uncover = 0, // the level falls to 0 (from covered)
    Cover = 1,   // the level rises to 0xff (to covered)
    Hold = 2,    // the pace's ticks held, then the task ends
};
// +1 of the wipes
enum class WipePhase : int8_t {
    CloseDisplay = 0, // on a frame of buffer 1: the display list closed, the shown image held
    Strips = 1,       // the strips run their count (screen-space packets: render output)
    Reopen = 2,       // on a frame of buffer 1: the display list reopened, the task ends
};
// +2 of a wipe's Strips phase
enum class WipeStripStep : int8_t {
    Start = 0,   // the count set by the wipe's type
    Running = 1, // counting down
};
// +9: the task's direction (0: no task)
constexpr int8_t kFadeFromCovered = 1;
constexpr int8_t kFadeToCovered = -1;
// the level of a covered screen
constexpr int16_t kFadeCoveredLevel = 0xff;

struct FadeState {
    uint8_t type = 0;            // FadeType
    int8_t phase = 0;            // FadePhase / WipePhase
    int8_t stripStep = 0;        // WipeStripStep
    uint8_t variant = 0;         // the request's offset in its wipe / slide range
    uint8_t stripTicks = 0;      // the wipe's strip count
    StateArray<uint8_t, 3> counterSpare{}; // the bytes after it a request's dword store clears (no reader)
    uint8_t covered = 0;         // the screen is covered by the fade's tile
    int8_t busy = 0;             // kFadeFromCovered / kFadeToCovered while the task runs, 0 idle
    uint8_t keepBufferMode = 0;  // the request's parameter: a wipe leaves the buffer mode as it is
    uint8_t savedBufferMode = 0; // the buffer mode a wipe restores
    uint16_t speed = 0;          // the level's step per tick, then the hold's ticks
    int16_t level = 0;           // 0 clear .. kFadeCoveredLevel covered
    uint16_t blend = 0;          // the tile's draw-mode bits (exe 0x40b8c0's)
    uint8_t savedClearBackground = 0; // the draw environments' background flag a wipe saves

    // no fade task runs
    bool idle() const { return busy == 0; }
    // the wipe's counter dword (the strip count and the three bytes after it) cleared, as a request does
    void clearCounterWord() {
        stripTicks = 0;
        counterSpare.fill(0);
    }
    // the strip step and the variant cleared (the exe's word store at +2)
    void clearStripWord() {
        stripStep = 0;
        variant = 0;
    }
};
inline FadeState& fadeState() {
    static FadeState s;
    return s;
}

// The exe's tables of the fade requests: the task's direction by the request's high nibble (bytes, 2 apart) and the
// level's speed by its low three bits
// (exe data 0x8ae804 / 0x8ae808)
inline const DataTable<StridedScalarTable<int8_t, 2, 2>> fadeDirections{TableId::FadeDirections};
inline const DataTable<ScalarTable<uint16_t, 8>> fadeSpeeds{TableId::FadeSpeeds};

} // namespace od::port
