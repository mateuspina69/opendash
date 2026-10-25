// ST55 (the digger's licence exam) actor classes, batch B: the drone hatch and its drones, the ambush controller and its
// flyers, the floating mine, the burrowing digger and the homing point's marker. See content/manda/exam_enemies_b.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x5aa920: ST55 pool 0x60 type 0x2b: the hopping drone released by the hatches (counted while it lives)
void examDroneUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x426df0: ST55 pool 0xe0 type 0x21: the ambush records (pool 0x20 type 0x67) woken by the player
void examAmbushUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x4da900: ST55 pool 0x20 type 0x4f: the drone hatch (releases pool-0x60 type 0x2b drones)
void examHatchUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x592580: ST55 pool 0x60 type 4: the floating mine (its glows: pool 0xa0 type 6)
void examMineUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x4dd9d0: ST55 pool 0x20 type 0x53: the burrowing digger (claw hits on bones 2 / 4)
void examDiggerUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x4ed1a0: ST55 pool 0x20 type 0x67 variant 0: the ambush flyer woken by the ambush controller
void examAmbushFlyerUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x55f350: pool 0xa0 type 6 (ST55 / Nino): the 8-tick ring marker at the homing point (the player's bone point 0;
// a 0xf0 draw command)
void homingMarkerUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
