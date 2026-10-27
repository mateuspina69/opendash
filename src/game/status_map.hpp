// The status sub-screen's map page (exe 0x5e86a0): the area's map, the route's destination and the minimap switch.
#pragma once

#include <cstdint>

namespace od::port {

struct StatusScreen;

// The map page's block (exe 0xa71700), shared with the stages' map callbacks. MapMarker holds +0, +0x24 and +0x28.
struct StatusMapState {
    uint8_t tileLayer = 0;        // +0x1: the room block's map layer
    uint8_t area = 0;             // +0x2
    uint8_t positioned = 0;       // +0x4: the map centred
    uint8_t ownPositioned = 0;    // +0x5: the second picture centred
    uint8_t layer = 0;            // +0x8: the floor shown
    uint8_t previousLayer = 0;    // +0x9
    uint8_t layerCount = 0;       // +0xa
    uint8_t layerLabel = 0;       // +0xb: the first floor's label
    int16_t scrollX = 0, scrollY = 0; // +0xc, +0xe: the map's centre
    int16_t minX = 0, minY = 0, maxX = 0x100, maxY = 0x100; // +0x10: the map's bounds
    int16_t x = 0x58, y = 0x24, w = 0xd4, h = 0x8c; // +0x18: the viewport
    uint32_t previewRecords = 0;  // +0x20: the destination under the list's cursor
    uint8_t displayMode = 0;      // +0x2c: the drawing area's display mode
    uint8_t minimapChoice = 0;    // exe 0xa5e56c: the minimap question's choice

    void clearPosition() { positioned = ownPositioned = 0; }
};
inline StatusMapState& statusMap() {
    static StatusMapState s;
    return s;
}

// exe 0x5e85a0: the centre clamped to the bounds and viewport, the possible scroll directions as four bits.
uint32_t statusMapClamp();
// exe 0x5e86a0: one frame of the map page.
void mapPage(StatusScreen& s);

} // namespace od::port
