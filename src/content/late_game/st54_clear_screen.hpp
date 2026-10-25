// ST54's game-clear screen, script 0x76, content/late_game/st54_clear_screen.cpp.
#pragma once

#include <cstdint>
#include <vector>

#include "engine/screen_prim.hpp"
#include "game/actor_movement.hpp"

namespace od::port {

// The screen primitives the last clearScreenScriptTick drew (script 0x76: the clear-time sprites of exe 0x53f2f0 and
// the tiled backdrop exe 0x5e7fe0(1)), in the ordering-table walk order (entry 0x88 first, then 0x78; the chains of an
// entry newest first), draw modes resolved into the sprites' tpage. Empty unless the script block runs script 0x76.
const std::vector<ScreenPrim>& clearScreenPrims();

// Script 0x76, dispatched by the handler [0x89b6d0 + id * 4]
bool clearScreenScriptPorted(uint8_t id);
bool clearScreenScriptTick(const PlayerEnv& env, uint8_t id);

} // namespace od::port
