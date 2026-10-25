// Calbania's enemy spawners (pool 0xe0 type 0xa): ST1F exe 0x41ef20, ST21 exe 0x41f850, ST22 exe 0x41f940: the classes
// that spawn the stage's enemies through the encounter slots (game/encounter) or directly, track them and keep their
// defeated ids and the story flags. State byte +8 dispatches the class's table: state 0 initialises, state 1 runs the
// variant (+6) or the sub-room; sub-states in +9 / +0xa. Transcribed one routine per exe routine over the game state
// (its addresses; the content's raw form until its area's task puts it on views); its records, positions and routes are
// read from the exe image.
#pragma once

#include <cstdint>
#include <vector>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x41ef20: ST1F e0 type 0xa: story-8 fight (flags 0x580..0x586, 0x599)
void spawnerSt1fUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x41f850: ST21 e0 type 0xa: story-8 records, two RNG-placed type-6 actors
void spawnerSt21Update(const PlayerEnv& env, uint32_t actor);
// exe 0x41f940: ST22 e0 type 0xa: story-8 records
void spawnerSt22Update(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
