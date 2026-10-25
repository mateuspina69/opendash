// The lock-on reticle as the game draws it (presentation only: reads the player, never writes it). Kept apart from
// game/effects_draw (the combat effects); app/reticle_overlay turns it into triangles.
#pragma once

#include <vector>

#include "game/effects_draw.hpp"

namespace od::fx {

// exe 0x5f2870 (the lock-on reticle): the four arrowheads around the lock-on target, Prim::front set.
// rowBase = the first of its 4 gradient rows (gradients(): town, town zooming, field in range, out of range).
void reticle(int rowBase, std::vector<Prim>& out);

} // namespace od::fx
