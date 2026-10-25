// The parts of the script / stage block that abandoned_mine_scripts.cpp keeps (game/script_state ScriptPart,
// game/stage_state StagePart): their members and the exe offsets they stand for.
#pragma once

#include <cstdint>

#include "game/script_state.hpp"

namespace od::port::abandoned_mine_scripts_state {

// The scripts' own bytes (the block's script bytes, +0xc..)
struct RuinScriptBytes : ScriptPart<RuinScriptBytes> {
    uint8_t musicWait = 0; // +0x12: script 0x52's wake step: waiting for the music fade and the DAT
    int16_t turnTarget = 0; // +0x14: the yaw a turn step turns the player to
    int16_t headTarget = 0; // +0x16: the yaw runner 0x531920 turns the player's head to
    int16_t rate = 0; // +0x18: the head turn's step per tick; the lift's speed (step 0x531fc0)
    int16_t lifting = 0; // +0x1a: step 0x531fc0: the lift moves
};

// exe 0x531b00's own word: the step tick the walk onto the lift started
struct LiftArrivalState {
    int32_t walkStart = 0; // 0xa59f9c
};
inline LiftArrivalState& liftArrival() {
    static LiftArrivalState s;
    return s;
}

} // namespace od::port::abandoned_mine_scripts_state
