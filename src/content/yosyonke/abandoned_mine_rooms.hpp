// the Abandoned Mine's rooms (ST0F) in the room scripts: their room-script handlers (game/room_scripts dispatches them through
// the content registry).
#pragma once

#include "game/content_registry.hpp"

namespace od::content {

// the Abandoned Mine's room-script handlers in the chain
void registerAbandonedMineRooms(od::port::ContentRegistry& r);

} // namespace od::content
