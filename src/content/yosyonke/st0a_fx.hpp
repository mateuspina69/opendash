// The town's cosmetic effect, class 0x43bff0 of ST0A / ST0B / ST0C / ST47 (pool 0xa0, stage table type 0x1f;
// content/yosyonke/st0a_fx.cpp).
#pragma once

#include <cstdint>
#include <vector>

#include "game/actor_movement.hpp"
#include "game/hud.hpp"

namespace od::port {

// exe 0x43bff0 (pool 0xa0 type 0x1f, the stage tables 0x8d8a04 ST0A / 0x8d90d0 ST0B / 0x8d9784 ST0C): nine variants
// (the exe's table 0x88d870): 0 / 2 / 4 single 0x2c billboards, 3 a two-row billboard trail, 5 / 8 screen-space glow
// discs (POLY_G3 fans), 6 a burst (0xf0 rings + an OT G4 star), 7 a full-screen TILE tint, 1 nothing
void stageEffectUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x43c110 (0x43bff0's variants 2..8 as its variants 0..6)
void stageEffectFromVariant2Update(const PlayerEnv& env, uint32_t actor);
// The full-screen TILE of variant 7 (exe 0x43cdb0, linked into the OT [0xa67840] + 0x74) rebuilt from the state the last
// tick left, for the live variant-7 effects of the class in slot order (only blend mode 0: see the .cpp)
std::vector<hud::Prim> stageEffectTintDraw();

} // namespace od::port
