// ST40's teleporter effects (pool 0xa0 stage type 0x1e, exe 0x43adb0), content/late_game/st40_fx.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x43adb0 (pool 0xa0 stage type 0x1e in ST40, table [0x901998 + 0x1e * 4]; by the variant through 0x88d850):
// variant 0 the teleporter pad's glowing panel (4 POLY_G4), 1 nothing, 2 the teleport ring (32 POLY_G4 around the pad,
// spawning variant-3 sparks), 3 a rising spark (LINE_G2)
void teleporterEffectUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
