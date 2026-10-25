// The Nino ruin's ST36 (its second floor; story 10+): the room-script side of the stage, content/nino/st36_rooms.cpp.
#pragma once

#include <cstdint>

#include "engine/routine_ids.hpp"

namespace od::port {

// Room-script handlers of the ST36 table 0x8cb250 (story 10.. = 0x8cb1e0) and the sub-1 handler 0x5878b0 shared by
// ST36 / ST37 / ST38: 0x586e40 (sub 2), 0x586e60 (sub 4), 0x586f00 (sub 5), 0x586f40 (sub 10), 0x5878b0 (sub 1).
// false = not one of these (game/room_scripts roomScriptHandler chain).
bool ninoSt36RoomHandler(RoutineId fn, bool run);
// exe 0x586d80 (the room scripts' case 0x36) after its handler call: the flooded rooms (flag 0x90: records 0x8cb2a8
// x 2 once, the pace 3 and the player in water) or the dry ones (the pace 2, the player out of water), once per room.
// The flag-0x700 door test stays in game/transition roomScripts.
void ninoSt36RoomCaseTail();
// exe 0x586c90 (the ST36 room helper) after its handler step cleared / exe 0x55c000(-1, 0, 0) (content/nino/nino_rooms
// ninoRoomHelper): the opened floor cells of subs 4 / 10 (flags 0x9a / 0x9b) and the flooded rooms' sound.
void ninoSt36RoomHelperTail();

} // namespace od::port
