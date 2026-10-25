// The room handlers of the tutorial (ST4A, the stage case exe 0x58af00, handler table 0x8cd530 by sub-room): the lock
// flags of its doors, the fights' spawn records and the return to the title once it is over
// (content/intro/st4a_rooms.cpp).
#pragma once

#include <cstdint>

#include "engine/routine_ids.hpp"

namespace od::port {

// the handler `fn` of the stage case (run = false: only whether it is transcribed); false for another handler
bool st4aRoomHandler(RoutineId fn, bool run);

} // namespace od::port
