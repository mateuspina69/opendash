// The bottom of the ruin under Manda Island (ST51, reached through the sub-ruin ST4E from ST24): the room-script
// handlers of its guardian hall and its item room. Dispatched through the content registry's room-handler chain.
#pragma once

#include <cstdint>

#include "engine/routine_ids.hpp"

namespace od::content {

// the handler fn of one of these rooms (run = false: only whether it is transcribed)
bool st51RoomHandler(RoutineId fn, bool run);

} // namespace od::content
