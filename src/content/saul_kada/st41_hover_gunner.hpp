// The hovering gunner of the pirate base's gate rooms (ST41 / ST42 pool 0x20 type 0x7d, exe 0x50a5a0), see
// content/saul_kada/st41_hover_gunner.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x50a5a0 (ST41 sub 12, ST42 subs 7 / 10): turns / moves toward the player, attacks with the pirates' shots (a
// single bomb, a burst of 3 rising shots, a stream of thrown ones), rises to y -0x400 (not once the gate is open),
// falls / lands when hit or staggered, dies with its drops
void hoverGunnerUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
