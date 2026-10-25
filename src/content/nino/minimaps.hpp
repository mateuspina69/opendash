// The minimap callbacks of the Nino stages (game/minimap): ST15..ST19's (exe 0x54f190) and the ruin of ST35..ST38's
// (exe 0x552c20).
#pragma once

#include "game/content_registry.hpp"

namespace od::content {

void registerNinoMinimaps(od::port::ContentRegistry& r);

} // namespace od::content
