// The ST55 licence-exam enemies (also in ST12 / ST13 / ST14 and the sub-ruins): the small liftable reaverbot, the
// shelled reaverbot, the shield-bearing gunner, its shot and its knocked-off shield. See
// content/manda/exam_enemies_a.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x48e750 (pool 0x20 type 0x2a): the small liftable reaverbot
void examHopperUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x47a600 (pool 0x20 type 0x19): the shelled reaverbot (flips over, lifted, thrown, rolls back onto its feet)
void examTumblerUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x44e4c0 (pool 0x20 type 4): the shield-bearing gunner reaverbot
void examShieldGunnerUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x5cc220 (pool 0x40, ST55 stage type 0x22): the gunner's shot; exe 0x592fa0 (static type 5): its shield
void examGunnerShotUpdate(const PlayerEnv& env, uint32_t actor);
void examShieldUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
