// The parts of the script / stage block that st16_script33.cpp keeps (game/script_state ScriptPart,
// game/stage_state StagePart): their members and the exe offsets they stand for.
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/script_state.hpp"

namespace od::port::st16_script33_state {

// The script's own bytes: the carrier, the player's start x / z
struct CatchScriptFields : ScriptPart<CatchScriptFields> {
    ActorRef<AnimatedActor> carrier; // +0xc
    int16_t startX = 0;              // +0x10
    int16_t startZ = 0;              // +0x12
};

} // namespace od::port::st16_script33_state
