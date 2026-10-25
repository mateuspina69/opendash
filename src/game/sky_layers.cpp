// Native sky layer initialization (exe 0x5efb90).
#include "game/sky_layers.hpp"

#include "game/game_state.hpp"
#include "game/display.hpp"

namespace od::port {

RoomSkyLayers RoomSkyLayers::load(const TableReader& r) {
    RoomSkyLayers result;
    const TableReader list = r.follow(0, uint32_t(kSkyLayers * 4));
    if (r.u32(0) == 0) return result;
    for (size_t i = 0; i < kSkyLayers; ++i) {
        if (list.u32(i * 4) == 0) continue;
        const TableReader header = list.follow(i * 4, 0x20);
        auto& d = result.layers[i];
        d.flags = header.u16(0);
        d.width = header.u16(2);
        d.height = header.u16(4);
        d.tpage = header.u16(6);
        d.clut = header.u16(8);
        d.speedX = header.s16(0xa);
        d.base = header.s16(0xc);
        d.horizontalOffset = header.s16(0xe);
        d.colour = header.u32(0x10);
        d.drawMode = header.u16(0x14);
        d.tilesAddress = list.u32(i * 4) + 0x20;
        const uint64_t count = uint64_t(d.width) * d.height;
        if (count > (UINT32_MAX - 0x20u) / 4u)
            throw DataSourceError("sky layers: tilemap too large");
        const TableReader record = list.follow(i * 4, uint32_t(0x20 + count * 4));
        d.tiles.reserve(size_t(count));
        for (size_t j = 0; j < count; ++j) d.tiles.push_back(record.u32(0x20 + j * 4));
    }
    return result;
}

const RoomSkyLayers& roomSkyLayers(int stage, int room) {
    return loadTable<RoomSkyLayers>({TableId::RoomSkyLayers, roomIndex(stage, room)});
}

void skyLayersSetup() {
    const GameState& g = gameState();
    const auto& definitions = roomSkyLayers(g.stage, g.room);
    SkyState& sky = skyState();
    sky.ticks = 0;
    sky.parallax = 0;
    sky.roomList = dataSource().table({TableId::RoomSkyLayers, roomIndex(g.stage, g.room)}, 4).u32(0);
    for (size_t i = 0; i < kSkyLayers; ++i) {
        SkyLayer& layer = skyLayers()[i];
        const auto& d = definitions.layers[i];
        layer.flags = uint8_t(d.flags); // the flag word: the flags and the mode
        layer.mode = uint8_t(d.flags >> 8);
        if (d.tilesAddress == 0) continue; // unused slots clear only their flag word
        layer.width = d.width;
        layer.height = d.height;
        layer.tpage = d.tpage;
        layer.clut = d.clut;
        layer.scrollSpeedX = d.speedX;
        layer.scrollSpeedY = 0;
        layer.base = d.base;
        layer.horizontalOffset = d.horizontalOffset;
        layer.colour = d.colour;
        layer.drawMode = d.drawMode;
        layer.tiles = d.tilesAddress;
        if (layer.mode != 0) {
            layer.scrollX = int32_t(uint32_t(uint16_t(d.base)) << 16);
            layer.scrollY = int32_t(uint32_t(uint16_t(d.horizontalOffset)) << 16);
            layer.lastScrollX = layer.scrollX;
            layer.lastScrollY = layer.scrollY;
        }
    }
}

void skyLayersTick() {
    if (display().drawingOff) return;
    ++skyState().ticks;
    for (size_t i = 0; i < kSkyLayers; ++i) {
        SkyLayer& layer = skyLayers()[i];
        if (!(layer.flags & kSkyLayerRun) || !(layer.mode & 1)) continue;
        layer.lastScrollX = layer.scrollX;
        layer.lastScrollY = layer.scrollY;
        layer.scrollX += int32_t(layer.scrollSpeedX) * 0x100;
        layer.scrollY += int32_t(layer.scrollSpeedY) * 0x100;
    }
}

bool skyLayersInRoom(int stage, int room) {
    const GameState& g = gameState();
    return g.stage == stage && g.room == room &&
           skyState().roomList ==
               dataSource().table({TableId::RoomSkyLayers, roomIndex(stage, room)}, 4).u32(0);
}

uint32_t skyLayerTicks() { return skyState().ticks; }

} // namespace od::port
