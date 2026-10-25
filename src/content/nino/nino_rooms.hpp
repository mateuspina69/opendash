// Nino Island's rooms (ST15..ST1B, ST35..ST38) in the stage / room setup and the room scripts: their room helpers, the
// parts of their room functions beyond the template and ST19's stage case tail (game/stage_setup and game/room_scripts
// dispatch them through the content registry).
#pragma once

#include "game/content_registry.hpp"

namespace od::content {

// the island's room helpers in the chain, its room functions' parts and its stage cases' tails
void registerNinoRooms(od::port::ContentRegistry& r);

} // namespace od::content
