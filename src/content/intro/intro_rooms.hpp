// The game's start's rooms (ST00, ST03, the tutorial ST4A) in the room setup: the parts of their room functions beyond
// the template (game/stage_setup runs them through the content registry).
#pragma once

#include "game/content_registry.hpp"

namespace od::content {

// the room functions' parts of the start's stages
void registerIntroRooms(od::port::ContentRegistry& r);

} // namespace od::content
