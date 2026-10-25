// The ice ruin's critters (ST2F pool 0x20 type 0x4a), content/yosyonke/ice_ruin_critters.cpp:
//   sub 1 (exe 0x4d46f0): the cast of script 0x49 (records 0x8a9518 / 0x8a952c / 0x8a9540) -- idles / wanders / hops
//     off walls; the script's cue 2 makes it face the player and leap away
//   sub 0 (exe 0x4d3240): the critters to catch (records 0x8cac1c / 0x8cac30 / 0x8cac44, ST2F sub 6) -- wanders like
//     the other, is stunned by hits, lifted / carried / thrown by the player; talked to it is caught (flags 0x3f1 /
//     0x3f2 / 0x3f3 in turn) and freed
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x4d46f0: the critter of script 0x49's scene
void sceneCritterUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x4d3240: the critter to catch
void catchableCritterUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
