// The parts of the script / stage block that mission_result.cpp keeps (game/script_state ScriptPart,
// game/stage_state StagePart): their members and the exe offsets they stand for.
#pragma once

#include <cstdint>

#include "game/script_state.hpp"

namespace od::port::mission_result_state {

// The script's own bytes: the steps' countdown, the ticks
struct ResultScriptBytes : ScriptPart<ResultScriptBytes> {
    uint8_t countdown = 0; // +0xc
    int32_t ticks = 0; // +0x1c
    // exe 0x40dc40 over +0xc..+0x1b: the members there zeroed (past it: kept)
    void clearSetUpRun() override {
        countdown = {};
    }
};

// the countdown byte the steps count down through (the script points the exe global 0xa59f4c at it every tick)
struct ResultGlobals {
    uint32_t countdown = 0; // the countdown byte's exe address (the pointer the exe keeps; the steps use the byte)
};
// the script's exe global
inline ResultGlobals& resultGlobals() {
    static ResultGlobals s;
    return s;
}

} // namespace od::port::mission_result_state
