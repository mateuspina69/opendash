// The town's rooms in the stage / room setup and the room scripts: the room helpers of the Flutter (ST04..ST07) and of
// the town (ST08..ST0E, ST47), the parts of their room functions beyond the template, ST0E's encounter kinds, and their
// room-script handlers (game/stage_setup and game/room_scripts dispatch them through the content registry).
#pragma once

#include "game/content_registry.hpp"

namespace od::content {

// the town's room helpers / room-script handlers in the chains, its room functions' parts and its stage cases' tails
void registerYosyonkeRooms(od::port::ContentRegistry& r);

} // namespace od::content
