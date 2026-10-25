// ST5C: the attack classes of the boss 0x50b9c0 (content/late_game/st5c): the rocks of attack 9 (pool 0x20 type 0x86,
// exe 0x50ee90), attack 10's charge (static type 0x5b, exe 0x5bb6e0) and ball (pool 0x40 type 0x33, exe 0x5d1a10),
// attack 7's charge (static type 0x60, exe 0x5be190), the blades of attack 6 (static type 0x66, exe 0x5c16b0) and the
// stomp's shots (pool 0x40 type 0x32, exe 0x5d16d0). See content/late_game/st5c_fx.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

void bossFallingRockUpdate(const PlayerEnv& env, uint32_t actor); // exe 0x50ee90
void ballChargeUpdate(const PlayerEnv& env, uint32_t actor);  // exe 0x5bb6e0
void beamChargeUpdate(const PlayerEnv& env, uint32_t actor);  // exe 0x5be190
void bossBladeUpdate(const PlayerEnv& env, uint32_t actor);   // exe 0x5c16b0
void stompShotUpdate(const PlayerEnv& env, uint32_t actor);   // exe 0x5d16d0
void chargeBallUpdate(const PlayerEnv& env, uint32_t actor);  // exe 0x5d1a10

} // namespace od::port
