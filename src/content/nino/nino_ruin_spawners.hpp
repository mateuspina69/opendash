// The Nino ruin's enemy spawners (pool 0xe0 type 0xa): ST36 exe 0x4213e0, ST37 exe 0x421670, ST38 exe 0x421bb0: the
// classes that spawn the stage's enemies through the encounter slots (game/encounter) or directly, track them and keep
// their defeated ids and the story flags. State byte +8 dispatches the class's table: state 0 initialises, state 1 runs
// the variant (+6) or the sub-room; sub-states in +9 / +0xa. Transcribed one routine per exe routine over the game
// memory (its addresses; the content's raw form until its area's task puts it on views); its records, positions and
// routes are read from the exe image.
#pragma once

#include <cstdint>
#include <vector>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x4213e0: ST36 e0 type 0xa: room records, pool-0x60 type-0x48 countdown, script group 4
void spawnerSt36Update(const PlayerEnv& env, uint32_t actor);
// exe 0x421670: ST37 e0 type 0xa: room records, type-0x48 bursts / countdown, push-back trap
void spawnerSt37Update(const PlayerEnv& env, uint32_t actor);
// exe 0x421bb0: ST38 e0 type 0xa: room records, type-0x48 countdown (flag 0x92)
void spawnerSt38Update(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
