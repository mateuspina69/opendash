// The leaping critter of the sub-ruins (pool 0x20 class 0x4dff50: ST2D sub 1, ST2E sub 1, ST4F sub 7), see
// leaping_critter.cpp.
#pragma once

#include <cstdint>

namespace od::port {

struct PlayerEnv;

// exe 0x4dff50: a waddling critter that waits for the player, wanders, leaps at the player or walks a path; it can be
// lifted and thrown (game/carry); it dies in an explosion and the drops, or (variant 0) times out in three puffs when
// the player keeps away
void leapingCritterUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
