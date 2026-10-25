// The ruin under Nino's scripts (ST35: the guardians), content/nino/st35_scripts.cpp.
#pragma once

#include <cstdint>

#include "engine/routine_ids.hpp"

namespace od::port {

struct PlayerEnv;

// Scripts 0x15 (exe 0x51d3e0), 0x16 (0x51d740), 0x17 (0x51d9e0), 0x19 (0x51e8d0): by their handler's named routine
bool ninoRuinSt35ScriptPorted(RoutineId handler);
bool ninoRuinSt35ScriptTick(const PlayerEnv& env, RoutineId handler);

} // namespace od::port
