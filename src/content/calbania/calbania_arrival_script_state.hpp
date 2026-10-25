// The parts of the script / stage block that calbania_arrival_script.cpp keeps (game/script_state ScriptPart,
// game/stage_state StagePart): their members and the exe offsets they stand for.
#pragma once

#include <cstdint>

#include "game/script_state.hpp"

namespace od::port::calbania_arrival_script_state {

// the script's own bytes: the Flutter's rest height, the ticks after it landed
struct ArrivalBytes : ScriptPart<ArrivalBytes> {
    int16_t restY = 0; // +0xc
    int32_t restTicks = 0; // +0x1c
    // exe 0x40dc40 over +0xc..+0x1b: the members there zeroed (past it: kept)
    void clearSetUpRun() override {
        restY = {};
    }
};

} // namespace od::port::calbania_arrival_script_state
