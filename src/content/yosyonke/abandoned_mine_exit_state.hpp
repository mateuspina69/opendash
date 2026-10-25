// The parts of the script / stage block that abandoned_mine_exit.cpp keeps (game/script_state ScriptPart,
// game/stage_state StagePart): their members and the exe offsets they stand for.
#pragma once

#include <cstdint>

#include "game/script_state.hpp"

namespace od::port::abandoned_mine_exit_state {

// The script's own bytes: the countdown after the last message
struct RuinExitBytes : ScriptPart<RuinExitBytes> {
    int16_t countdown = 0; // +0x10
    // the countdown set one byte at a time (the exe's two byte stores)
    void setCountdownBytes(uint8_t low, uint8_t high) {
        countdown = int16_t(uint16_t(low | high << 8));
    }
};

} // namespace od::port::abandoned_mine_exit_state
