// The Birdbot soldier's shot (pool 0x40 class 0x5c6950, stage projectile type 0x10 of ST15..ST17, ST30 / ST31: spawned
// by game/birdbot_soldier's shoot step at its bone 2, facing its yaw): it rises a little, flies forward falling slowly
// with a trail of sparks, and ends in a spark on a wall, a hit or after 0x78 ticks.
#pragma once

#include <cstdint>

namespace od::port {

struct PlayerEnv;

constexpr uint8_t kWalkerShotStageType = 0x10; // its stage projectile type

// exe 0x5c6950: the shot's update (ActorUpdateFn)
void walkerShotUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
