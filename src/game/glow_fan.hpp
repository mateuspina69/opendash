// The screen-space glow fan shared by the effect variants 0x43c4f0 / 0x43ce80 (class 0x43bff0, content/yosyonke/st0a_fx)
// and 0x43f980 (class 0x43d030, content/late_game/end_cast): the effect's projected point S (exe 0x5f2dd0, its result
// ignored), radii wy / wx (exe 0x5f2f20 widths, pixels << the shift), an additive draw mode (exe 0x40b6d0(1, 1, tpage
// 0x3e)) and 64 POLY_G3 0x32 {black, rim i - 1} {centre colour, S} {black, rim i}, rim i = S + (sin64(i) * wx / 4096,
// cos64(i) * wy / 4096) (rounded toward zero; rim 0 = S + (0, wy)), one chain at the OT slot [0xa67840] + 0x78.
// Port: the ordering-table side channel (game/draw_commands otTriG3) with S as the camera-input point (the effect's
// position << the stage shift) and the pixel offsets brought back to its depth (view z / the projection distance).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"

namespace od::port {

// the glow fan of `effect` (its centre colour, its radii in pixels)
void glowFan(const EffectActor& effect, uint32_t centre, int32_t wy, int32_t wx);

} // namespace od::port
