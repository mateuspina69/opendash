// The minimap callbacks of the Nino stages (game/minimap): ST15..ST19's (exe 0x54f190) and the ruin of ST35..ST38's
// (exe 0x552c20).
#pragma once

#include <cstddef>
#include <cstdint>

#include "game/content_registry.hpp"

namespace od::content {

void registerNinoMinimaps(od::port::ContentRegistry& r);

// exe 0x54fc00 / 0x54fc70: the map page shares the town's patched sprites and draws its lists in neutral colour
void ninoMapTownSpritesUpdate();
void ninoMapSpriteListDraw(size_t c, uint32_t list);

} // namespace od::content
