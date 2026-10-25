// The story-12 room handlers of Saul Kada's town (ST29 / 2A / 2B / 2C / 48), see
// content/saul_kada/saul_kada_town_rooms.cpp.
#pragma once

#include <cstdint>

#include "engine/routine_ids.hpp"

namespace od::port {

// the handler fn of one of the town's stage cases (run = false: only whether it is transcribed)
bool saulKadaTownRoomHandler(RoutineId fn, bool run);
// exe 0x583730's tail (ST29): past story 0xc with flag 0x6ff -> script block 8
void saulKadaTownRoomCaseTail();

} // namespace od::port
