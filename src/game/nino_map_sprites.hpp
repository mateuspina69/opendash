// The sprite lists of the Nino stages' maps (ST15..ST19): the minimap callback (content/nino/minimaps) and the status
// screen's map page (game/status_map_nino) draw them.
#pragma once

#include <cstddef>
#include <cstdint>

namespace od::port {

// the lists' default colour and sprite code (the minimap's)
constexpr uint32_t kNinoMapSpriteColour = 0xa0a0a0;
// exe 0x54fc00: the town list's sprites 3, 5, 6 and 8 by the story flags 0x33 / 0x32 / 0x34
void ninoMapTownSpritesUpdate();
// exe 0x54fc70(packet, list, colour): the list's semi-transparent sprites at the lanes' point plus their offsets
// (through the corner lanes), in the clut of the lane 0xa2
void ninoMapSpriteList(size_t c, uint32_t list, uint32_t colour, uint8_t code);
// the minimap's lists (its colour, the free sprite code | 2)
void ninoMapSpriteList(size_t c, uint32_t list);
// the map page's: in neutral colour
void ninoMapSpriteListDraw(size_t c, uint32_t list);

} // namespace od::port
