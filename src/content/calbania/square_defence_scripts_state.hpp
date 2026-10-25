// The parts of the script / stage block that square_defence_scripts.cpp keeps (game/script_state ScriptPart,
// game/stage_state StagePart): their members and the exe offsets they stand for.
#pragma once

#include <cstdint>

#include "game/script_state.hpp"

namespace od::port::square_defence_scripts_state {

// the bytes the square's scripts keep: a byte nothing writes (cleared at the start: the step phase cleared at the
// wrap-up), the lift's count of script 0x20 and a byte the starts clear
struct SquareBytes : ScriptPart<SquareBytes> {
    uint8_t phaseHold = 0; // +0xd
    uint8_t liftCount = 0; // +0xf
    uint8_t unread4 = 0; // +0x10
};

} // namespace od::port::square_defence_scripts_state
