// The late game's rooms (ST40, ST4B, ST54, ST56, ST57, ST59..ST5B) in the stage / room setup and the room scripts:
// their room helpers, the parts of their room functions beyond the template and the stage cases' own code
// (game/stage_setup and game/room_scripts dispatch them through the content registry).
#pragma once

#include "game/content_registry.hpp"

namespace od::content {

// the late game's room helpers / room-script handlers in the chains and its room functions' parts
void registerLateGameRooms(od::port::ContentRegistry& r);

} // namespace od::content
