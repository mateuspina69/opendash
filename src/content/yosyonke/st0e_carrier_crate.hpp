// ST0E (the vehicle chase, see st0e_boss_vehicle.hpp): the carrier crate the boss vehicle drops
// (content/yosyonke/st0e_carrier_crate.cpp).
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x5a8150: static ST0E type 0x24 (spawned by the boss vehicle's crate sub-state) -- the carrier crate: it homes on
// its target with a Servbot inside (pool 0x20 type 0xb sub 0, content/yosyonke/st0e_servbots), can be lifted and thrown
// at the boss vehicle's front weak point; it breaks into a blast and the Servbot jumps out
void carrierCrateUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
