// The room scripts of ST41 / ST42 / ST44, content/saul_kada/st41_st44_rooms.cpp.
#pragma once

#include <cstdint>

#include "engine/routine_ids.hpp"


namespace od::port {

// the handler fn of one of these stage cases (run = false: only whether it is transcribed)
bool st41St44RoomHandler(RoutineId fn, bool run);

} // namespace od::port
