// The ice ruin's ST2F hidden block that appears (static type 0x1f sub 2, exe 0x5a5020),
// content/yosyonke/ice_ruin_appearing_block.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x5a5020 (static ST2F type 0x1f sub 2): a hidden block that appears by flag (its cell switched, fading in)
void appearingBlockUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
