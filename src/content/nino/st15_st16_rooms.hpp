// The room scripts of ST15 / ST16 (the whole stage cases 0x57eeb0 / 0x57f040), content/nino/st15_st16_rooms.cpp.
#pragma once

#include <cstdint>

#include "engine/routine_ids.hpp"
#include "game/actor_movement.hpp"

namespace od::port {

// the whole stage cases 0x57eeb0 (ST15) / 0x57f040 (ST16), accepted by their own address (run = false: only
// whether it is transcribed)
bool st15St16RoomHandler(RoutineId fn, bool run);

} // namespace od::port
