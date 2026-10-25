// The room scripts of ST4B / ST4C and the cutscene script 0x59 (0x533b20, ST4B sub 0),
// content/late_game/st4b_st4c_rooms.cpp.
#pragma once

#include <cstdint>

#include "engine/routine_ids.hpp"
#include "game/actor_movement.hpp"

namespace od::port {

// the handler fn of one of these stage cases (run = false: only whether it is transcribed)
bool st4bSt4cRoomHandler(RoutineId fn, bool run);

// the scripts (block 0xa725a0): script 0x59 (0x533b20)
bool st4bScript59Ported(RoutineId handler);
bool st4bScript59Tick(const PlayerEnv& env, RoutineId handler);

} // namespace od::port
