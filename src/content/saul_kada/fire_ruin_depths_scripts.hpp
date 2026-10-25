// The fire ruin's story-13 scripts of ST28 (the lava pit, the boss): 0x44 / 0x45 / 0x46 / 0x47, see
// content/saul_kada/fire_ruin_depths_scripts.cpp.
#pragma once

#include <cstdint>

#include "engine/routine_ids.hpp"
#include "game/actor_movement.hpp"

namespace od::port {

// handler = [0x89b6d0 + id * 4]: exe 0x52ea50 (0x44), 0x52ef50 (0x45), 0x52f320 (0x46), 0x52f5e0 (0x47)
bool fireRuinDepthsScriptsPorted(RoutineId handler);
// one tick of the script block for the handler; false if it is not one of the above
bool fireRuinDepthsScriptsTick(const PlayerEnv& env, RoutineId handler);

} // namespace od::port
