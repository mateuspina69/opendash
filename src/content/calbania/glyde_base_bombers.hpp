// The bomb-throwing Birdbots of Glyde's base, class exe 0x4967d0 (pool 0x20 type 0x2d sub 3 of ST30),
// content/calbania/glyde_base_bombers.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x4967d0: a bomber Birdbot of the base's waves -- it rises in, takes a bomb (static type 0x22), aims at the
// player and throws it; the wave nearly beaten, it turns away and sinks (variant 0: gone; the others reappear at one of
// six places). Shot down: a knocked-down Birdbot (pool 0x20 type 0x2d sub 0) in its place; with a variant, flag 0x21
// and the key (static type 0x42) thrown toward the gate
void glydeBaseBomberUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
