// The ice ruin's ST2D / ST2E shrinking block (pool 0x20 type 0x52, exe 0x4dd770), content/yosyonke/ice_ruin_shrink_block.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x4dd770 (pool 0x20 type 0x52: ST2D / ST2E): a solid block with a scrolling texture; shrinks away in sequence
void shrinkBlockUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
