// ST3C (the pirate base, story 12): the script-driven cast member of pool 0x20 type 0x72 sub 0.
//
//   pirateBaseCastUpdate  exe 0x4f4740  pool 0x20 type 0x72 sub 0 (ST3C sub 2): follows the script's timeline (its step
//                                       tick, its step sub-id) -- variant 0 switches animations / face tracks at fixed
//                                       ticks through the step sub-ids 1..4; variant 1 waits for the script's start,
//                                       plays its own short timeline and, every 16th tick, may drop a spark emitter
//                                       (static type 3 variant 7) at bones 3 and 6
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

void pirateBaseCastUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
