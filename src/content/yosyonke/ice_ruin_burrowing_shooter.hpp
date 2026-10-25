// The ice ruin's burrowing shooter (ST2F pool 0x20 type 0x17, exe 0x479130) and its charged shot (pool 0x40 type 0xc,
// exe 0x5c5980), content/yosyonke/ice_ruin_burrowing_shooter.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x479130: buried until the player comes within 0xc00, pops up; hops toward / around the player at middle range,
// fires the charged shot up close, burrows again far away; can be lifted / carried / thrown by the player (and
// wriggles free); hurt / knocked back by hits; dies in a blast + its row's drops
void burrowingShooterUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x5c5980: the charged shot -- grows at the shooter's hand (three 0xf0 rings), launched when the shooter's
// sub-state leaves 2, flies along its rotation spinning (hit entry, wall hits); its falling form (a 0x2c billboard
// under gravity) and its spark burst
void chargedShotUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
