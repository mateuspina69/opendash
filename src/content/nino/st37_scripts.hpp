// The Nino ruin's ST37 scripts, content/nino/st37_scripts.cpp.
#pragma once

#include <cstdint>

#include "engine/routine_ids.hpp"

namespace od::port {

struct PlayerEnv;

// Script 0x5f (exe 0x536ec0): ST37 sub 6 at story 10 (handler 0x5873c0, flag 0x590), the boss room's opening; by its
// handler's named routine
bool st37ScriptPorted(RoutineId handler);
bool st37ScriptTick(const PlayerEnv& env, RoutineId handler);

} // namespace od::port
