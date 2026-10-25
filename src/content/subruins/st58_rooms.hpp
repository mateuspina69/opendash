// The room scripts of ST58 (the stage case 0x58c950), content/subruins/st58_rooms.cpp.
#pragma once

#include <cstdint>

#include "engine/routine_ids.hpp"

namespace od::port {

// the handler fn of one of these stage cases (run = false: only whether it is transcribed)
bool st58RoomHandler(RoutineId fn, bool run);

} // namespace od::port
