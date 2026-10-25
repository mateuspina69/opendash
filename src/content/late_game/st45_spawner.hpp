// The ST45 enemy spawner (pool 0xe0 type 0xa, exe 0x422260): the classes that spawn the stage's enemies through the
// encounter slots (game/encounter) or directly, track them and keep their defeated ids and the story flags. State byte
// +8 dispatches the class's table: state 0 initialises, state 1 runs the variant (+6) or the sub-room; sub-states in +9
// / +0xa. Transcribed one routine per exe routine over the game state (its addresses; the content's raw form until its
// area's task puts it on views); its records, positions and routes are read from the exe image.
#pragma once

#include <cstdint>
#include <vector>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x422260: ST45 e0 type 0xa: flag 0x176 -> fades 0x1a / 0xa, classes 0x494 freed, flag 0x174
void spawnerSt45Update(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
