// The light effects class of the island / last stages (exe 0x4392a0: pool 0xa0 stage type 0x19 of ST1C's table 0x8e5a14
// and ST54's 0x9151f8): seven kinds by its sub-type -- the sun lens flare (screen discs along the sun -> screen-centre
// line), a ring dropping toward the effect along its tilt, a growing pulsing screen disc on a point, a shrinking
// pulsing disc that spawns turning light rays, a turning light ray (additive quads around the projected point), a
// full-screen tint (not drawn) and a 5-frame sprite. Each kind is a row of the state table (start, run,
// free).
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x4392a0 (ST1C / ST54 effect type 0x19; the kind +5 x the state +8 through the table 0x88d5a4)
void lensFlareUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
