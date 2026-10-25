// The ruin under Nino, ST35 (opened after Glyde's defeat, story 0xa): its room scripts, content/nino/st35_rooms.cpp.
// Its scripts: content/nino/st35_scripts; its guardian: content/nino/ruin_guardian.
#pragma once

#include <cstdint>

#include "engine/routine_ids.hpp"

namespace od::port {

// Room-script handlers of ST35 at story 0xa (table 0x8cb0e8 via 0x8cb130): 0x5869c0 / 0x586aa0 / 0x586ac0 /
// 0x586af0 / 0x586b30. fn = the handler, run = tick it (false: only "is it known")
bool ninoRuinSt35RoomHandler(RoutineId fn, bool run);
// The tail of the stage's room-script case 0x586950 after the handler: once per room the pace 2 and the player out of
// water (the flag-0x700 door test is game/transition's)
void ninoRuinSt35RoomTail();

} // namespace od::port
