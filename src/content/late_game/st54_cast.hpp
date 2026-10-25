// ST54's effect-spawning mover (pool 0x20 type 0x85, exe 0x50e190), content/late_game/st54_cast.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x50e190 (pool 0x20 type 0x85: ST54, its first parameter -> its kind): an effect-spawning mover driven by its step
// (kind 0: rises, rings of stage type-3 effects; kind 1: takes off, four type-0x67 puff emitters, sparks, then a blast
// and type-3 variant-0x21 effects at the frozen position); two glow quads while drawn
void effectMoverUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
