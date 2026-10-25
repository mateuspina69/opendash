// The swimming reaverbots of the Nino ruin (pool 0x20 type 0x77: ST37 / ST38 / ST50) and the torpedo they fire,
// content/nino/ruin_swimmers.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x4fda00 (sub 0, states [0x897b04]): the big swimmer -- it wanders, and with its group attacks in turn when the
// player comes close in front of it moving: it lunges at the player or fires torpedoes; on dry land it hops about
void torpedoSwimmerUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x4fec40 (sub 1, states [0x897b54]): the small swimmer -- it wanders and lunges at the player when hit (or when
// its group's order calls it); on dry land it hops about
void lungingSwimmerUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x5cac60 (pool 0x40 stage type 0x1e, states [0x8d15dc]): the torpedo -- it homes on the player for 0xb4 ticks
// keeping 0x64 above the ground, a bubble trail under water / an exhaust ring (draw command 0xf0) on dry land; the
// player can lift and throw it; a hit or a wall blows it up (a blast)
void torpedoUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
