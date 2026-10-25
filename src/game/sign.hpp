// The sign, class 0x58d6e0 (static pool type 0 of ST09, ST19, ST20 ...): a notice you read (talk -> the event of its
// message), a stand-on solid with a hit-list entry; a hit with flag 0x80000 drops it (falls, two bounces) once its
// event flag is set. Variant 2 is scenery (+0 kActorScenery), variant 4 turns.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x58d6e0: the sign's update (ActorUpdateFn)
void signUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
