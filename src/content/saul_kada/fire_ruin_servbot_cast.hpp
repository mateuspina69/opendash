// A Servbot in the fire ruin's cutscenes (pool 0x20 type 0xb sub 2: ST28 sub 5, scripts 0x42..0x45, exe 0x465b20),
// content/saul_kada/fire_ruin_servbot_cast.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x465b20 (pool 0x20 type 0xb sub 2: ST28 sub 5): a cast member of scripts 0x42..0x45 (cues, head turn, blinks)
void servbotScriptCastUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
