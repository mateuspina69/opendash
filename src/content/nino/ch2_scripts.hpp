// Story chapter 2 after the Manda key: the cutscene scripts of the flight to the next island and its missions
// (0x31 / 0x32 / 0x34 / 0x35 / 0x36 / 0x37 / 0x38 / 0x3d / 0x3e), content/nino/ch2_scripts.cpp.
#pragma once

#include <cstdint>

#include "engine/routine_ids.hpp"

namespace od::port {

struct PlayerEnv;

// the script handlers this file runs (by the handler's named routine) and their tick
bool ch2ScriptPorted(RoutineId handler);
bool ch2ScriptTick(const PlayerEnv& env, RoutineId handler);

} // namespace od::port
