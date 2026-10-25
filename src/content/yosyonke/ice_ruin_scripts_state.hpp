// The parts of the script / stage block that ice_ruin_scripts.cpp keeps (game/script_state ScriptPart,
// game/stage_state StagePart): their members and the exe offsets they stand for.
#pragma once

#include <cstdint>

#include "game/script_state.hpp"

namespace od::port::ice_ruin_scripts_state {

// The script's own bytes: the player's point at the walk's end
struct CritterSceneBytes : ScriptPart<CritterSceneBytes> {
    int16_t walkEndX = 0; // +0xc
    int16_t walkEndY = 0; // +0xe
    int16_t walkEndZ = 0; // +0x10
};

} // namespace od::port::ice_ruin_scripts_state
