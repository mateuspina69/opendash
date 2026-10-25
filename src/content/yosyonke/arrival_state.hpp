// The parts of the script / stage block that arrival.cpp keeps (game/script_state ScriptPart,
// game/stage_state StagePart): their members and the exe offsets they stand for.
#pragma once

#include <cstdint>

#include "game/script_state.hpp"

namespace od::port::arrival_state {

// The script's own bytes: the second message's delay
struct CabinScriptBytes : ScriptPart<CabinScriptBytes> {
    uint8_t delay = 0; // +0xf
};

// The script's own bytes: the Flutter's place (its height and its z), the landing's ticks
struct DescentScriptBytes : ScriptPart<DescentScriptBytes> {
    int16_t landingY = 0; // +0xc
    int16_t landingZ = 0; // +0xe
    int32_t ticks = 0; // +0x1c
    // exe 0x40dc40 over +0xc..+0x1b: the members there zeroed (past it: kept)
    void clearSetUpRun() override {
        landingY = {};
        landingZ = {};
    }
};

} // namespace od::port::arrival_state
