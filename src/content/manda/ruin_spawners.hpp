// The Manda ruin's enemy spawners (pool 0xe0 type 0xa): ST12 exe 0x41b990, ST13 exe 0x41c070 (all variants), ST14 exe
// 0x41c910: the classes that spawn the stage's enemies through the encounter slots (game/encounter) or directly, track
// them and keep their defeated ids and the story flags. State 0 initialises, state 1 runs the variant; sub-states and
// steps in the header's sub-state / step bytes. Their records, points and tables are read from the exe image.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x41b990: ST12 e0 type 0xa: groups 0 / 2..5, type-3 respawns (ids 1..3), type-0x1b emitters, type-0x2a drops
void spawnerSt12Update(const PlayerEnv& env, uint32_t actor);
// exe 0x41c070: ST13 e0 type 0xa: hopper groups (all variants 0..5)
void spawnerSt13Update(const PlayerEnv& env, uint32_t actor);
// exe 0x41c910: ST14 e0 type 0xa: script group 1, type-3 respawns (ids 8 / 9)
void spawnerSt14Update(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
