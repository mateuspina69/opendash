// The ST45 area boss and the children it spawns (content/late_game/st45_boss.cpp): their class updates in the class
// registry (content/late_game/register.cpp).
#pragma once

#include <cstdint>

namespace od::port {

struct PlayerEnv;

// exe 0x4c78e0: the area boss of ST45 sub 1 (pool 0x20 type 0x45; its death sets flag 0x176)
void st45BossUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x5c4820: its breath puff (pool 0x40 type 5)
void st45BreathPuffUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x5ca060: its spit blob (pool 0x40 type 0x1a)
void st45SpitBlobUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x5b0230: its shock wave (pool 0x60 type 0x3c)
void st45ShockWaveUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x5c62d0: its mouth shot (pool 0x40 type 0xf)
void st45MouthShotUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
