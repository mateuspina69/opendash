// The ice ruin (ST2D..ST2F) in the room setup: its room helpers (game/stage_setup dispatches them through the content
// registry), content/yosyonke/ice_ruin_room_helpers.cpp.
#pragma once

#include "game/content_registry.hpp"

namespace od::content {

// the ice ruin's room helpers in the chain
void registerIceRuinRooms(od::port::ContentRegistry& r);

} // namespace od::content
