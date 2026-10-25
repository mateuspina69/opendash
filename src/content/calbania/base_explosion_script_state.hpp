// The parts of the script / stage block that base_explosion_script.cpp keeps (game/script_state ScriptPart,
// game/stage_state StagePart): their members and the exe offsets they stand for.
#pragma once

#include <cstdint>

#include "game/script_state.hpp"

namespace od::port::base_explosion_script_state {

// the script's own byte: the white screen (kWhiteAsked by the step or the wrap-up's fade, kWhiteShown once no fade
// runs)
struct BaseBlownBytes : ScriptPart<BaseBlownBytes> {
    uint8_t whiteScreen = 0; // +0xc
};

} // namespace od::port::base_explosion_script_state
