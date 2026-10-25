// The ST34 ruin's sentries (sub 3, story 12+; pool 0x20 type 0x15 sub 1, exe 0x474710), see
// content/saul_kada/st34_ruin_sentry.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x474710: a dormant ruin reaverbot woken by the player's "use" (variants 3 / 4: by its enemy group's marks,
// variant 4 also the player within 0x400); its routine by variant (turn and march, lobbed shots, a throw of drops, a
// march round), staggers on hits, dies in an explosion with drops
void ruinSentryUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
