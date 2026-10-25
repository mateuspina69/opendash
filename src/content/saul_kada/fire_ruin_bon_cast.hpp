// Bon Bonne in the fire ruin's cutscenes (pool 0x20 type 0x51: ST27 scripts 0x40 / 0x41, exe 0x4dd1e0; ST28 script
// 0x42, exe 0x4dd5f0), content/saul_kada/fire_ruin_bon_cast.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x4dd1e0 (pool 0x20 type 0x51 sub 2: ST27): Bon of scripts 0x40 / 0x41 (turns and animation chains on the cues)
void bonScriptCastUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x4dd5f0 (pool 0x20 type 0x51: ST28 sub 5): Bon of script 0x42 (walks back / lit and freed at the cue 1)
void bonRetreatCastUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
