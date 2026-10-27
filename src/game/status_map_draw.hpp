// The map page's drawing (exe 0x5e8f70 .. 0x5e96c0): the route list's names, the area's caption and the browsing
// arrows; the map itself is the stage's second room callback (game/status_map_stage).
#pragma once

#include <cstdint>

namespace od::port {

struct StatusScreen;

// exe 0x5e8f70: the room's area name on one row (the two parts 120 px apart); room 0xff: "cancel route"
void statusMapAreaNameDraw(uint32_t packed, uint8_t stage, uint8_t room);
// exe 0x5e9080: the route list, its cursor / preview target, scroll bar and frame
void statusMapRouteListDraw(StatusScreen& s, int x, int y);
// exe 0x5e93e0: the stage's name and, for a map with layers, the current layer's name
void statusMapCaptionDraw();
// exe 0x5e94c0: the layer tabs and the enabled scroll arrows (the held directions brighter)
void statusMapBrowseControlsDraw(uint32_t colour, uint32_t flags);

} // namespace od::port
