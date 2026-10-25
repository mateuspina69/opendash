// Story chapter 2 after the Manda key (story 5 ->): the room-script handlers and the stage cases of the flight from the
// Sulphur-Bottom (ST46) to the next island (ST15 .. ST19, ST1B) and its missions, content/nino/ch2_rooms.cpp.
#pragma once

#include <cstdint>

#include "engine/routine_ids.hpp"

namespace od::port {

// Room-script handlers of the chapter: fn = the handler, run = tick it (false: only "is it known")
bool ch2RoomHandler(RoutineId fn, bool run);
// Inline stage cases of the room scripts' tick: ST15 0x57eeb0 / ST16 0x57f040 (the missions)
bool ch2RoomCaseKnown(uint8_t stage);
bool ch2RoomCase();
// exe 0x57fcd0(setting): ST19's gate-lock light (a stage light at the setting, exe data)
void ninoLockEffect(uint32_t setting);
// the word of the room grid's cell (column, row) (its head dword, as the exe's inlined index reads it)
uint32_t ninoGridCell(uint32_t column, uint32_t row);

} // namespace od::port
