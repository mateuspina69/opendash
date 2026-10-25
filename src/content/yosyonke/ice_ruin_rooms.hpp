// The ice ruin's rooms in the room scripts: the handlers of the room scripts' stage cases ST2D / ST2E / ST2F (they hold
// the door test), content/yosyonke/ice_ruin_rooms.cpp.
#pragma once

#include <cstdint>

#include "engine/routine_ids.hpp"

namespace od::port {

// the handler fn of one of these stage cases (run = false: only whether it is transcribed)
bool iceRuinRoomHandler(RoutineId fn, bool run);

} // namespace od::port
