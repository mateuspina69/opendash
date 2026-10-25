// The story-13 visit scripts of the fire ruin and the ruin (0x2e ST27, 0x63 ST26, 0x2d ST33), see
// content/saul_kada/fire_ruin_visit_scripts.cpp.
#pragma once

#include <cstdint>

#include "engine/routine_ids.hpp"
#include "game/actor_movement.hpp"

namespace od::port {

bool fireRuinVisitScriptsPorted(RoutineId handler);
bool fireRuinVisitScriptsTick(const PlayerEnv& env, RoutineId handler); // false if not ported

} // namespace od::port
