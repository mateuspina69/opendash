// The parts of the script / stage block that st48_sand_script.cpp keeps (game/script_state ScriptPart,
// game/stage_state StagePart): their members and the exe offsets they stand for.
#pragma once

#include <cstdint>

#include "game/script_state.hpp"

namespace od::port::st48_sand_script_state {

// The script's own bytes: the actor's rest height, the hold's ticks
struct SandScriptFields : ScriptPart<SandScriptFields> {
    int16_t restHeight = 0; // +0xc
    uint32_t holdTicks = 0; // +0x1c
    // exe 0x40dc40 over +0xc..+0x1b: the members there zeroed (past it: kept)
    void clearSetUpRun() override {
        restHeight = {};
    }
};

} // namespace od::port::st48_sand_script_state
