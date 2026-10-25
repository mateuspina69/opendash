// Roll as Megaman's companion in the chapter after the crash landing (content/yosyonke/roll_follower.cpp): the follower
// of ST08 / ST09, Roll waiting at Joe's lab, and the follower of the field (ST0D) with hit points.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x445b00 (pool 0x20 type 0 sub 1 of the ST08 / ST09 tables): follows the player to the point where her
// line-of-sight probe last saw him, runs when he is far, searches when the sight is blocked, talks (her message),
// falls, turns away when bumped; the head turns toward him
void rollUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x443330 (ST08 pool 0x20 type 0 sub 2): Roll waiting at Joe's lab -- the talk (the choice that opens the lab),
// her head look switched by the message's flags 0x681 / 0x682, falls
void shedRollUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x4e4c20 (ST0D pool 0x20 type 0x60): the follower with hit points (hurt by the player's shots: flinch / knocked
// back / fainted), lifted by the player, walking along the tracks
void fieldRollUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
