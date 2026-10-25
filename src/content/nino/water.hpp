// Water of the Nino ruin (ST35..ST38) and the effects the water code spawns (content/nino/water.cpp). The player's own water
// paths (splashes exe 0x57b990, skate spray exe 0x57ba60, the +0xba pull, the +0xbb sink, the +0xbe underwater
// physics) live in game/player/player_effects.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"
#include "game/actor_state.hpp"

namespace od::port {

constexpr uint8_t kSplashType = 0x48; // the splash's static type in the stage tables
constexpr uint8_t kSplashLoopFlags = kActorSweepGroup2 | kActorLateLoop; // a spawned splash: late loop, sweep group 2

// exe 0x5b3c40 (pool 0x60 type 0x48: the water splash / bubble burst spawned by exe 0x57b990, 0x57ba60 and 0x426d10)
void waterSplashUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x426cc0 (pool 0xe0 type 0x20, ST36 / ST37 / ST38 / ST50): the underwater bubbles of the player
void waterBubbleControllerUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
