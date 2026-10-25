// The room scripts of ST32 (the stage case 0x5864c0), content/calbania/st32_rooms.cpp.
#pragma once

#include <cstdint>

#include "engine/routine_ids.hpp"
#include "game/actor_movement.hpp"

namespace od::port {

// the handler fn of one of these stage cases (run = false: only whether it is transcribed)
bool st32RoomHandler(RoutineId fn, bool run);

} // namespace od::port
