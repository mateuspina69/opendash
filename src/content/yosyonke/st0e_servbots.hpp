// ST0E (the vehicle chase, see st0e_boss_vehicle.hpp): the Servbots of the chase -- the one in the carrier crate, the
// ones riding the boss vehicle and the ones flung out of the turret (content/yosyonke/st0e_servbots.cpp).
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x464a30: pool 0x20 type 0xb sub 0 -- the Servbot of the carrier crate (variant 0 inside it, variant 1 jumps out
// when it breaks)
void crateServbotUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x464dc0: pool 0x20 type 0xb sub 1 (spawned by the boss vehicle) -- the Servbots riding the boss vehicle's bone 2,
// lobbing bombs (pool 0x40 type 0x24) at the defended vehicle; knocked off by a hit or flag 0x582
void riderServbotUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x492f20: pool 0x20 type 0x2d (variant 0x41, spawned by the turret when a part breaks) -- a Servbot flung out
// spinning (smoke), lands, runs around the player, knocked back by hits, liftable / throwable (hurts enemies when
// thrown)
void flungServbotUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
