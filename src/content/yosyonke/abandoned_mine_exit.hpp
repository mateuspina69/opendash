// Out of the Abandoned Mine (ST47 room 2, story 1): script 0x12 -- the messages in the ruin's exit, then the player back
// (content/yosyonke/abandoned_mine_exit.cpp).
#pragma once

#include <cstdint>

#include "engine/routine_ids.hpp"

namespace od::port {

struct PlayerEnv;

// script 0x12 (exe handler 0x51b820): whether `handler` is it / its tick
bool abandonedMineExitScriptPorted(RoutineId handler);
bool abandonedMineExitScriptTick(const PlayerEnv& env, RoutineId handler);

} // namespace od::port
