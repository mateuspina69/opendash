// The ST1D rock burst (controller class exe 0x41aa10, pool 0xe0 type 9): a burst of rocks and dust in the elephant's
// arena.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x41aa10: four stage statics of type 0x20 (the arena's rocks, variants 2 / 3) and eight global hit sparks
// (variant 3) scattered by the game's RNG around the controller's point, then the controller freed
void rockBurstUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
