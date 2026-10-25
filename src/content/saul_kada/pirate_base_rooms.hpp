// The room handlers of the pirate base (ST3B / ST3C, stories 12 and 13), see content/saul_kada/pirate_base_rooms.cpp.
#pragma once

#include <cstdint>

#include "engine/routine_ids.hpp"

namespace od::port {

// the handler fn of one of their stage cases (run = false: only whether it is transcribed)
bool pirateBaseRoomHandler(RoutineId fn, bool run);

} // namespace od::port
