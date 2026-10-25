// The sky layers' descriptors (exe 0xa6c440: 0xb layers of 0x30 bytes; the stage setup loads them, app/sky draws them):
// the bytes the game logic writes -- the flags a sweep changes, the scroll a script sets.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "engine/data_source.hpp"

#include "game/state_array.hpp"
#include "game/vectors.hpp"

namespace od::port {

struct SkyLayer {
    uint8_t flags = 0;             // +0x00: kSkyLayerRun | kSkyLayerShown (neither: no layer)
    uint8_t mode = 0;              // +0x01 (the setup's word store at +0 writes it with the flags)
    uint16_t unread02 = 0;         // +0x02 (no reader traced)
    Fixed16 scrollX;               // +0x04: the layer's 16.16 scroll (a script moves it)
    Fixed16 scrollY;               // +0x08
    int32_t lastScrollX = 0;       // +0x0c: the scroll before the tick's (the draw's)
    int32_t lastScrollY = 0;       // +0x10
    int16_t base = 0;              // +0x14
    int16_t horizontalOffset = 0;  // +0x16: the layer's horizontal offset
    int16_t scrollSpeedX = 0;      // +0x18: its horizontal scroll per tick
    int16_t scrollSpeedY = 0;      // +0x1a
    uint16_t width = 0;            // +0x1c: the tilemap's size
    uint16_t height = 0;           // +0x1e
    uint16_t clut = 0;             // +0x20
    uint16_t tpage = 0;            // +0x22
    uint32_t colour = 0;           // +0x24
    uint16_t drawMode = 0;         // +0x28
    uint16_t unread2a = 0;         // +0x2a (no reader traced)
    uint32_t tiles = 0;            // +0x2c: the tilemap's address in the room's list (the data source's)
};
// the flags: the layer runs (scrolls), is shown; a layer with neither is unused
constexpr uint8_t kSkyLayerRun = 0x01, kSkyLayerShown = 0x02;
constexpr uint8_t kSkyLayerInUse = kSkyLayerRun | kSkyLayerShown;
constexpr size_t kSkyLayers = 0xb;
// the sky layers' descriptors (exe 0xa6c440, 0x30 bytes a layer) and their clocks
struct SkyState {
    StateArray<SkyLayer, kSkyLayers> layers{}; // 0xa6c440
    uint32_t ticks = 0;    // 0xa6bb88: the sky clock
    int32_t parallax = 0;  // 0xa6c428: the accumulated camera parallax
    uint32_t roomList = 0; // 0xa723a0: the room's layer list (the room table's pointer)
};
inline SkyState& skyState() {
    static SkyState s;
    return s;
}
inline StateArray<SkyLayer, kSkyLayers>& skyLayers() { return skyState().layers; }

// The room setup's list, read through the data source. Tile words are native
// owned data; textures and palettes are uploaded from the stage texture DAT.
struct SkyLayerDefinition {
    uint16_t flags = 0, width = 0, height = 0, tpage = 0, clut = 0;
    int16_t speedX = 0, base = 0, horizontalOffset = 0;
    uint32_t colour = 0;
    uint16_t drawMode = 0;
    uint32_t tilesAddress = 0;
    std::vector<uint32_t> tiles;
};
struct RoomSkyLayers : DataRecord {
    static constexpr uint32_t kBytes = 4;
    std::array<SkyLayerDefinition, kSkyLayers> layers;
    static RoomSkyLayers load(const TableReader& r);
};
const RoomSkyLayers& roomSkyLayers(int stage, int room);
// The native room setup initializes the descriptors before spawning actors.
void skyLayersSetup();
// The field frame advances manual layers; camera-relative layers use the sky clock.
void skyLayersTick();
bool skyLayersInRoom(int stage, int room);
uint32_t skyLayerTicks();

} // namespace od::port
