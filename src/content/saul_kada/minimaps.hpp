// The minimap callbacks of the Saul Kada stages (game/minimap): the ruin of ST26..ST28 / ST33 / ST34's (exe 0x54caf0),
// ST48's (exe 0x5539e0) and the ruin of ST41 / ST42's (exe 0x555670).
#pragma once

#include "game/content_registry.hpp"

namespace od::content {

void registerSaulKadaMinimaps(od::port::ContentRegistry& r);

} // namespace od::content
