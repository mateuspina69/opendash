// Script 0x58 of ST48 (exe 0x533820, the desert's first visit: an actor rises out of the sand), see
// content/saul_kada/st48_sand_script.cpp.
#pragma once

#include <cstdint>

#include "engine/routine_ids.hpp"
#include "game/actor_movement.hpp"

namespace od::port {

bool st48SandScriptPorted(RoutineId handler);
bool st48SandScriptTick(const PlayerEnv& env, RoutineId handler); // false if not ported

} // namespace od::port
