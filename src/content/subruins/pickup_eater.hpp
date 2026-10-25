// The pickup eater of the sub-ruins (pool 0x20 class 0x4ab6f0: ST2D / ST2E / ST4F, class tables 0x8f0bf0 / 0x8f2028 /
// 0x90fe68), see pickup_eater.cpp.
#pragma once

#include <cstdint>

namespace od::port {

struct PlayerEnv;

// exe 0x4ab6f0: a small critter that walks loops around a drifting centre, shies away from the player, runs to the
// pickups (game/pickups: the largest first) and eats them (frees those within 0x3c); flinches / is knocked back, can be
// lifted and thrown (game/carry); dies in an explosion and the drops
void pickupEaterUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
