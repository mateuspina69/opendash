// The pirate base's story-12 scripts 0x5c / 0x5d / 0x5e (ST3B / ST3C), see content/saul_kada/pirate_base_scripts.cpp.
#pragma once

#include <cstdint>

#include "engine/routine_ids.hpp"
#include "game/actor_movement.hpp"

namespace od::port {

bool pirateBaseScriptsPorted(RoutineId handler);
bool pirateBaseScriptsTick(const PlayerEnv& env, RoutineId handler); // false if not ported

} // namespace od::port
