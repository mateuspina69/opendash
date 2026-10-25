// The ST1A movers, class 0x590100 (static pool type 1 of the ST1A table): content/nino/st1a_movers.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x590100: a mover's update (ActorUpdateFn)
void st1aMoverUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
