// The Nino ruin's lowest floor ST38 (story 10+): the room scripts of the stage case 0x587800 (handlers [[0x8cb478 +
// story * 4] + sub * 4]) and the post-handler part of that case, content/nino/st38_rooms.cpp. Its boss and the boss's
// attacks: content/nino/platform_boss.
#pragma once

#include <cstdint>

#include "engine/routine_ids.hpp"

namespace od::port {

// The ST38 room-script handlers (table 0x8cb478, stories 10..20 -> 0x8cb410): 0x5878d0 (sub 2), 0x587920 (sub 3),
// 0x587960 (sub 5), 0x587a00 (sub 6), 0x587a40 (sub 7), 0x587a70 (sub 8), 0x587ab0 (subs 10 / 12), 0x587b00 (sub 11).
// run = false only asks whether fn is known.
bool nino38RoomHandler(RoutineId fn, bool run);
// exe 0x587800 after the handler call (before its flag-0x700 door test, game/transition): without flag 0x92 the
// records 0x8cb4d0 x 2 once and every tick the pace 3 + the player in water; with flag 0x92 once the pace 2 + the
// player out of water
void nino38RoomCaseTail();

} // namespace od::port
