// The rooms of ST47 (where the Abandoned Mine's device leads) in the room scripts: their room-script handlers (game/
// room_scripts dispatches them through the content registry).
#pragma once

#include "game/content_registry.hpp"

namespace od::content {

// ST47's room-script handlers in the chain
void registerSt47Rooms(od::port::ContentRegistry& r);

} // namespace od::content
