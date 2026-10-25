// The Manda ruin's rooms (ST12 .. ST14) in the room setup and the room scripts: their room helpers and the room-script
// handlers of story 4 and after (game/stage_setup and game/room_scripts dispatch them through the content registry).
#pragma once

#include <cstdint>

#include "engine/routine_ids.hpp"
#include "game/content_registry.hpp"

namespace od::content {

// the Manda ruin's room helpers in the chain
void registerMandaRooms(od::port::ContentRegistry& r);
// the room-script handler fn of one of these rooms (run = false: only whether it is transcribed)
bool mandaRuinRoomHandler(RoutineId fn, bool run);

} // namespace od::content
