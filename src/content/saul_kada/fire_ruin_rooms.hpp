// The room handlers of the fire ruin (ST26 / ST27 / ST28, story 13 and its story-14 setups) and of the ruin ST33 /
// ST34, see content/saul_kada/fire_ruin_rooms.cpp.
#pragma once

#include <cstdint>

#include "engine/routine_ids.hpp"

namespace od::port {

// the handler fn of one of their stage cases (run = false: only whether it is transcribed)
bool fireRuinRoomHandler(RoutineId fn, bool run);

} // namespace od::port
