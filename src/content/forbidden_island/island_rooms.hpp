// Forbidden Island's rooms (ST10, ST11, ST1D, ST3D..ST3F, ST49) in the stage / room setup and the room scripts: their
// room helpers, the parts of their room functions beyond the template and their room-script handlers (game/stage_setup
// and game/room_scripts dispatch them through the content registry).
#pragma once

#include <cstdint>

#include "game/content_registry.hpp"

namespace od::content {

// the island's room helpers / room-script handlers in the chains and its room functions' parts
void registerIslandRooms(od::port::ContentRegistry& r);

} // namespace od::content
