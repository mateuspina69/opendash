// The Nino ruin's ST37 (story 10+; entered from ST36 sub 9, left for ST38 sub 0 by sub 10, ST37 sub 15 <- ST38 sub 4):
// the room-script side of the stage, content/nino/st37_rooms.cpp. Its script 0x5f: content/nino/st37_scripts; its
// enemies: content/nino/gauge_reaverbots, content/nino/ruin_swimmers; its plates: content/nino/floor_switch.
#pragma once

#include <cstdint>

#include "engine/routine_ids.hpp"

namespace od::port {

// Room-script handlers of the ST37 tables 0x8cb2d0 (story 10) / 0x8cb310 (11..) (via 0x8cb390; their sub-1 handler
// 0x5878b0 and 0x581800 live elsewhere): the zone-slot encounter groups by the drained flag 0x91, the opened cells'
// flags, the hints, the boss room of sub 6. run = false: only "is it known"
bool st37RoomHandler(RoutineId fn, bool run);
// exe 0x586fe0 (the ST37 room helper) after its handler step cleared / exe 0x55c000(-1, 0, 0) (content/nino/nino_rooms
// ninoRoomHelper): subs 2 / 5 / 9 with their flags 0x9c / 0x9d / 0x9e: the cell switched open (variant 1); the water's
// sound unless drained (flag 0x91) or sub 15
void st37RoomHelperTail();
// exe 0x587120 (the room scripts' case 0x37) after its handler call: the flooded rooms (records 0x8cb3e8 x 2 once, the
// pace 3, the player in water) unless drained (flag 0x91) or sub 15 (the pace 2, the player out of water, once). The
// flag-0x700 door test stays in game/transition.
void st37RoomCaseTail();

} // namespace od::port
