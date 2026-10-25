// The minimap callbacks of the Calbania stages (game/minimap): the square's (ST1F, ST21..ST23, exe 0x54ab10) and
// Pokte's (ST24 / ST25, exe 0x550740).
#pragma once

#include "game/content_registry.hpp"

namespace od::content {

void registerCalbaniaMinimaps(od::port::ContentRegistry& r);

} // namespace od::content
