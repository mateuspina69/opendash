// The parts of the script / stage block that st02_attract_script.cpp keeps (game/script_state ScriptPart,
// game/stage_state StagePart): their members and the exe offsets they stand for.
#pragma once

#include <cstdint>

#include "game/script_state.hpp"

namespace od::port::st02_attract_script_state {

// The script's own bytes past the cast's timeline: the image scroll's ticks (cleared a byte at a time), the counters of
// the fading and the flipping screen quads (render output)
struct Script3Bytes : ScriptPart<Script3Bytes> {
    int16_t scrollTicks = 0; // +0x12
    uint8_t fadeQuadTicks = 0; // +0x14
    uint8_t flipQuadTicks = 0; // +0x15
    // the scroll's ticks cleared as the exe does (two byte stores)
    void clearScrollTicks() {
        scrollTicks = 0;
    }
};

struct TimedRings {
    uint32_t cursor = 0; // 0xa59f48: the current entry
};
inline TimedRings& timedRings() {
    static TimedRings s;
    return s;
}

} // namespace od::port::st02_attract_script_state
