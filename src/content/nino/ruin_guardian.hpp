// The guardian of the ruin under Nino (ST35: pool 0x20 type 0x3a sub 0) and its attacks (its beams, its orbs and
// mines), content/nino/ruin_guardian.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x4b1180: the guardian -- volleys, homing orbs, a ring of orbs, mines it sets off, twin beams; variant 0 (ST35
// sub 5) sets flag 0x94 when beaten, variant 1 (sub 0) flag 0x95
void ruinGuardianUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x5b9910 (pool 0x60 type 0x56): a beam (draw commands and OT quads)
void guardianBeamUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x5cd240 (pool 0x40 type 0x26): the guardian's orbs and mines (variant 1 drifting / homing orbs, 2 the big one,
// 3 the ring, 4 the mine)
void guardianOrbUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
