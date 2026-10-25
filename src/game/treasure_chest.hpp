// The treasure chest, class 0x4743c0 (pool 0x20, type 21 of the stage tables; 63 actors in 52 rooms): a chest you
// open (talk when in front of it) -> the item-get event (event mode 4, then the item pose), its lid's animation, its
// content (a special weapon or an event flag) and its opened flag.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x4743c0: the chest's update (ActorUpdateFn)
void treasureChestUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
