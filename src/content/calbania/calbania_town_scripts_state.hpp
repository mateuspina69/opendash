// The parts of the script / stage block that calbania_town_scripts.cpp keeps (game/script_state ScriptPart,
// game/stage_state StagePart): their members and the exe offsets they stand for.
#pragma once

#include <cstdint>

#include "game/script_state.hpp"

namespace od::port::calbania_town_scripts_state {

// the script's own bytes: the yaw the player's upper body turns toward and the turn's step
struct TownReturnBytes : ScriptPart<TownReturnBytes> {
    int16_t lookYaw = 0; // +0x10
    int16_t lookStep = 0; // +0x14
};

} // namespace od::port::calbania_town_scripts_state
