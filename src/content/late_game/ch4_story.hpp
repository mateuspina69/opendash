// Story chapter 4: after Calbania (story 0xa ->): the arrival on ST40 (sub 1, script 0x69's request) and what follows.
// See content/late_game/ch4_story.cpp.
#pragma once

#include <cstdint>

#include "engine/routine_ids.hpp"
#include "game/actor_movement.hpp"

namespace od::port {

// Scripts of the chapter (0x6a = exe 0x53a0c0: ST40 sub 1, the first arrival; 0x62 = exe 0x537b20: ST53 sub 1)
bool ch4ScriptPorted(RoutineId handler);
bool ch4ScriptTick(const PlayerEnv& env, RoutineId handler);
// Room-script handlers of the chapter (ST40 table 0x8cc648): fn = the handler, run = tick it (false: only "is it
// known")
bool ch4RoomHandler(RoutineId fn, bool run);
// The cast of script 0x62 (ST53 sub 1): exe 0x45d240 (pool 0x20 type 9 sub 2) / exe 0x4f66b0 (type 0x72 sub 2), the
// braking train (the stage static exe 0x5b6a60, a model solid) and the smoke column (the stage static type 0x26, exe
// 0x5a9680, draw commands)
void script62TalkerAUpdate(const PlayerEnv& env, uint32_t actor);
void script62TalkerBUpdate(const PlayerEnv& env, uint32_t actor);
void brakingTrainUpdate(const PlayerEnv& env, uint32_t actor);
void risingSmokeColumnUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
