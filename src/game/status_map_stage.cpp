// The picture maps of the status sub-screen: the stage's second room callback (exe 0xa72230).
#include "game/status_map_stage.hpp"

#include "game/display.hpp"
#include "game/game_state.hpp"
#include "game/loaded_data.hpp"
#include "game/menu_draw.hpp"
#include "game/minimap.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/room_grid.hpp"
#include "game/stage_state.hpp"
#include "game/status_map.hpp"
#include "game/status_map_nino.hpp"
#include "game/status_map_tiles.hpp"
#include "game/visited_rooms.hpp"

namespace od::port {

namespace {

using namespace menu_draw;

// The minimap's picture records: byte +2 says that its last two shorts are a fixed player point on the full map.
const DataTable<RecordTable<MinimapPicture, 11, 8>> squarePictures{TableId::CalbaniaSquareMinimapPictures};
const DataTable<RecordTable<MinimapPicture, 18, 8>> townPictures{TableId::St0DMinimapPictures};
const DataTable<RecordTable<MinimapPicture, 5, 8>> islandPictures{TableId::St10MinimapPictures};
const DataTable<RecordTable<MinimapPicture, 11, 8>> poktePictures{TableId::PokteMinimapPictures};
const DataTable<RecordTable<MinimapPicture, 18, 8>> desertPictures{TableId::St48MinimapPictures};
const DataTable<RecordTable<MinimapPicture, 15, 8>> latePictures{TableId::St40MinimapPictures};
const DataTable<ScalarTable<uint32_t, 3>> islandSprites{TableId::St3EMinimapSpriteLists};

struct MapPoint {
    int32_t x, y;
};
MapPoint mapOrigin() {
    const StatusMapState& m = statusMap();
    return {int16_t((m.w >> 1) - m.scrollX + m.x), int16_t((m.h >> 1) - m.scrollY + m.y)};
}
size_t mapOpen(uint16_t mode) {
    const StatusMapState& m = statusMap();
    const size_t c = otOpen(kOtOverlay);
    otClip(c, m.x, displayDrawArea().y + m.y, m.w, m.h);
    otDrawMode(c, mode);
    return c;
}
void mapBounds(int16_t minX, int16_t minY, int16_t maxX, int16_t maxY, uint8_t layers) {
    StatusMapState& m = statusMap();
    m.minX = minX;
    m.minY = minY;
    m.maxX = maxX;
    m.maxY = maxY;
    m.layerCount = layers;
    m.previousLayer = 0xff;
}
void mapPosition(MapPoint p) {
    StatusMapState& m = statusMap();
    m.scrollX = int16_t(p.x);
    m.scrollY = int16_t(p.y);
    m.positioned = 1;
}
MapPoint picturePoint(const MinimapPicture& pic, bool fixed = true) {
    if (fixed && (uint16_t(pic.pageZ) & 0xff)) return {pic.originX, pic.originZ};
    return {((int32_t(player().pos.x.whole) + 0x8000) >> 9) + pic.originX - roomGrid().originX,
            pic.originZ - ((int32_t(player().pos.z.whole) + 0x8000) >> 9) + roomGrid().originZ};
}
void previewPosition() {
    StatusMapState& m = statusMap();
    if (!m.previewRecords) return;
    m.scrollX = dataScalar<uint8_t>(m.previewRecords + 2);
    m.scrollY = dataScalar<uint8_t>(m.previewRecords + 3);
}
void mapClamp() {
    statusMapClamp();
    statusMap().previousLayer = statusMap().layer;
}
void pictureDraw(size_t c, MapPoint origin, uint16_t clut = 0x7fc0) {
    otSprite(c, origin.x, origin.y, spriteUv(clut, 0, 0), 0x100, 0x100);
}
void playerDraw(size_t c, MapPoint origin, MapPoint p) {
    minimapPlayerHeading(c, int16_t(origin.x + p.x), int16_t(origin.y + p.y));
}
void routeDraw(size_t c, MapPoint origin) {
    const StatusMapState& m = statusMap();
    const uint32_t route = mapMarker().records;
    if (mapRouteState() == MapRouteState::Set && route)
        statusMapRouteIconDraw(c, int16_t(origin.x + dataScalar<uint8_t>(route + 2)),
                               int16_t(origin.y + dataScalar<uint8_t>(route + 3)), true);
    if (m.previewRecords)
        statusMapRouteIconDraw(c, int16_t(origin.x + dataScalar<uint8_t>(m.previewRecords + 2)),
                               int16_t(origin.y + dataScalar<uint8_t>(m.previewRecords + 3)), false);
}

// exe 0x54ae30: Calbania's square and interiors, on one picture.
void squareMap() {
    const GameState& g = gameState();
    StatusMapState& m = statusMap();
    if (!m.positioned) {
        switch (g.stage) {
        case 0x1f: m.area = 0; break;
        case 0x20: m.area = uint8_t(g.room + 4); break;
        case 0x21: m.area = 1; break;
        case 0x22: m.area = 2; break;
        case 0x23: m.area = 3; break;
        case 0x30: m.area = uint8_t(g.room + 9); break;
        default: m.area = uint8_t(g.room + 6); break;
        }
        m.layer = 0;
        m.layerLabel = 0;
        mapBounds(0, 0, 0x100, 0x100, 1);
        mapPosition(picturePoint(squarePictures[m.area]));
    }
    const MapPoint point = picturePoint(squarePictures[m.area]);
    previewPosition();
    mapClamp();
    const size_t c = mapOpen(0x88);
    const MapPoint origin = mapOrigin();
    pictureDraw(c, origin);
    minimapIcon(c, int16_t(origin.x + (g.story < 0x11 ? 0xb3 : 0x84)),
                int16_t(origin.y + (g.story < 0x11 ? 0xb7 : 0x4b)), 6);
    playerDraw(c, origin, point);
    routeDraw(c, origin);
    otClipDrawArea(c);
}

// exe 0x54e9b0: Yosyonke and its interiors; the room chooses its point every frame.
void townMap() {
    const GameState& g = gameState();
    StatusMapState& m = statusMap();
    uint32_t area;
    switch (g.stage) {
    case 8: area = g.room + 4u; break;
    case 9: area = 6; break;
    case 0xa: area = g.room == 0 || g.room == 3 ? 7 : g.room == 1 ? 8 : 9; break;
    case 0xb: area = g.room + 0xau; break;
    case 0xc: area = g.room + 0xcu; break;
    case 0xd: area = 0; break;
    case 0xe: area = g.room + 1u; break;
    default: area = g.room + 0xfu; break;
    }
    const MapPoint point = picturePoint(townPictures[area]);
    if (!m.positioned) {
        m.layer = 0;
        mapBounds(0, 0, 0x100, 0x100, 1);
        mapPosition(point);
    }
    m.previousLayer = m.layer;
    previewPosition();
    statusMapClamp();
    const size_t c = mapOpen(0x88);
    const MapPoint origin = mapOrigin();
    pictureDraw(c, origin);
    minimapIcon(c, int16_t(origin.x + 0x1f), int16_t(origin.y + 0x88), 4);
    playerDraw(c, origin, point);
    routeDraw(c, origin);
    otClipDrawArea(c);
}

// exe 0x54c740: the Forbidden Island picture, its landing marker changing at story 2.
void islandMap() {
    const GameState& g = gameState();
    StatusMapState& m = statusMap();
    if (!m.positioned) {
        m.area = g.stage == 0x10 ? g.room : g.stage == 0x11 ? 2 : uint8_t(3 + (g.room != 0));
        m.layer = 0;
        mapBounds(0, 0, 0x100, 0x100, 1);
        mapPosition(picturePoint(islandPictures[m.area], false));
    }
    const MapPoint point = picturePoint(islandPictures[m.area], false);
    previewPosition();
    mapClamp();
    const size_t c = mapOpen(0x88);
    const MapPoint origin = mapOrigin();
    pictureDraw(c, origin);
    if (g.story < 2) otTile(c, origin.x + 0x59, origin.y + 0xdb, 2, 2, 0xffff, kTileOpaque);
    else minimapIcon(c, int16_t(origin.x + 0x5b), int16_t(origin.y + 0xdc), 6);
    playerDraw(c, origin, point);
    routeDraw(c, origin);
    otClipDrawArea(c);
}

// exe 0x5509d0: Pokte and its interiors, the landing marker at (0x95, 0x9c) of the picture.
void pokteMap() {
    const GameState& g = gameState();
    StatusMapState& m = statusMap();
    if (!m.positioned) {
        m.area = g.stage == 0x24 ? g.room : g.stage == 0x25 ? uint8_t(g.room + 3) : 0xa;
        m.layer = 0;
        mapBounds(0, 0, 0x100, 0x100, 1);
        mapPosition(picturePoint(poktePictures[m.area]));
    }
    const MapPoint point = picturePoint(poktePictures[m.area]);
    previewPosition();
    mapClamp();
    const size_t c = mapOpen(0x88);
    const MapPoint origin = mapOrigin();
    pictureDraw(c, origin);
    minimapIcon(c, int16_t(origin.x + 0x95), int16_t(origin.y + 0x9c), 6);
    playerDraw(c, origin, point);
    routeDraw(c, origin);
    otClipDrawArea(c);
}

// exe 0x553c70: the desert's outside and interiors; the landing marker moves at story 0xd.
void desertMap() {
    const GameState& g = gameState();
    StatusMapState& m = statusMap();
    if (!m.positioned) {
        switch (g.stage) {
        case 0x29: m.area = g.room; break;
        case 0x2a: m.area = uint8_t(g.room + 2); break;
        case 0x2b: m.area = uint8_t(g.room + 4); break;
        case 0x2c: m.area = uint8_t(g.room + 9); break;
        case 0x3b: m.area = 0xd; break;
        case 0x3c: m.area = uint8_t(g.room + 0xe); break;
        case 0x48: m.area = uint8_t(g.room + 0x10); break;
        default: notPortedAt("status map", "desert stage", g.stage);
        }
        m.layer = 0;
        m.layerLabel = 0;
        mapBounds(0, 0, 0x100, 0x100, 1);
        mapPosition(picturePoint(desertPictures[m.area]));
    }
    const MapPoint point = picturePoint(desertPictures[m.area]);
    previewPosition();
    mapClamp();
    const size_t c = mapOpen(0x88);
    const MapPoint origin = mapOrigin();
    pictureDraw(c, origin);
    minimapIcon(c, int16_t(origin.x + (g.story < 0xd ? 0xac : 0x7d)),
                int16_t(origin.y + (g.story < 0xd ? 0xbe : 0x63)), g.story < 0xd ? 7 : 6);
    playerDraw(c, origin, point);
    routeDraw(c, origin);
    otClipDrawArea(c);
}

struct St04Area : DataRecord {
    int16_t x = 0, y = 0;
    uint8_t layer = 0, count = 0, adjustment = 0, extra = 0;
    static St04Area load(const TableReader& r) {
        return {{}, r.s16(0), r.s16(2), r.u8(4), r.u8(5), r.u8(6), r.u8(7)};
    }
};
struct MapSprite : DataRecord {
    int16_t x = 0, y = 0;
    uint16_t uv = 0, size = 0;
    static MapSprite load(const TableReader& r) { return {{}, r.s16(0), r.s16(2), r.u16(4), r.u16(6)}; }
};
const DataTable<RecordTable<St04Area, 17, 8>> st04Areas{TableId::StatusMapSt04Areas};
const DataTable<RecordPointerTable<RecordTable<MapSprite, 3, 8>, 3, 24>> st04Sprites{TableId::StatusMapSt04Sprites};

// exe 0x5543a0 / 0x5543e0 / 0x554480: the three rooms whose player point follows collision-cell steps.
MapPoint st04Point(const St04Area& a) {
    MapPoint p{a.x, a.y};
    const int32_t x = (int32_t(player().pos.x.whole) + 0x8000) >> 9;
    const int32_t z = (int32_t(player().pos.z.whole) + 0x8000) >> 9;
    switch (a.adjustment) {
    case 0: p.y = z == 0x3f ? 0xc : z == 0x40 ? -0xc : -0x2c; break;
    case 1:
        if (z == 0x3f) p.y = -8;
        else if (z == 0x40) p.y = -0x18;
        else p.x = (x == 0x3e ? -0x18 : x == 0x3f ? -8 : x == 0x40 ? 8 : 0x18), p.y = -0x28;
        break;
    case 2:
        if (z == 0x3f) p.y = -0x18;
        else p.x = (x == 0x3e ? -0x18 : x == 0x3f ? -8 : x == 0x40 ? 8 : 0x18), p.y = -0x28;
        break;
    default: break;
    }
    return p;
}
// exe 0x554080 / 0x554520: the Flutter's decks, placed as a sprite list per floor.
void flutterMap() {
    const GameState& g = gameState();
    StatusMapState& m = statusMap();
    if (!m.positioned) {
        const uint8_t first = g.stage == 4 ? 0 : g.stage == 5 ? 3 : g.stage == 6 ? 6 : g.stage == 7 ? 0xc : 0xf;
        m.area = uint8_t(g.room + first);
        const St04Area& a = st04Areas[m.area];
        m.layer = a.layer;
        m.layerLabel = uint8_t(0x30 + (a.extra != 0));
        mapBounds(-0x60, -0x60, 0x60, 0x60, a.count);
        mapPosition(st04Point(a));
    }
    const St04Area& a = st04Areas[m.area];
    const MapPoint point = st04Point(a);
    mapClamp();
    const size_t c = mapOpen(0x88);
    const MapPoint origin = mapOrigin();
    otTile(c, origin.x - 0x80, origin.y - 0x80, 0x100, 0x100, 0xe0c0a0, kTileOpaque);
    for (const MapSprite& e : st04Sprites[m.layer + a.extra].records) {
        if (e.x == 0x7fff) break;
        otSprite(c, int16_t(origin.x + e.x), int16_t(origin.y + e.y),
                 spriteUv(0x7fc0, uint8_t(e.uv), uint8_t(e.uv >> 8)), uint8_t(e.size), uint8_t(e.size >> 8));
    }
    // exe 0x554336: the player's point passed without the sprite origin added.
    if (m.layer == a.layer) minimapPlayerHeading(c, int16_t(point.x), int16_t(point.y));
    otClipDrawArea(c);
}

// exe 0x5548c0 / 0x554c70: the Forbidden Island's three sprite maps (the last has an extra quad).
void islandRoomsMap() {
    const GameState& g = gameState();
    StatusMapState& m = statusMap();
    if (!m.positioned) {
        m.area = g.stage == 0x3d ? g.room : g.stage == 0x3e ? uint8_t(g.room + 4) : uint8_t(g.room + 0xa);
        m.layer = uint8_t(g.stage - 0x3d);
        m.layerLabel = 0x30;
        mapBounds(-0x60, -0x70, 0x60, 0x70, 3);
    }
    const uint32_t at = stateAddress(TableId::St3EMinimapOffsets) + m.area * 8;
    MapPoint point{dataScalar<int16_t>(at), dataScalar<int16_t>(at + 2)};
    if (!dataScalar<uint32_t>(at + 4)) {
        point.x += player().pos.x.whole >> 5;
        point.y -= player().pos.z.whole >> 5;
    }
    if (!m.positioned) mapPosition(point);
    mapClamp();
    const size_t c = mapOpen(0x9c);
    const MapPoint origin = mapOrigin();
    otTile(c, origin.x - 0x80, origin.y - 0x80, 0x100, 0x100, 0x686038, kTileOpaque);
    for (uint32_t e = islandSprites[m.layer]; dataScalar<uint32_t>(e) != 0xffffffff; e += 8) {
        const uint32_t d = dataScalar<uint32_t>(e);
        const uint32_t xy = ((d >> 16) + uint16_t(origin.y)) << 16 | ((d & 0xffff) + uint16_t(origin.x));
        otSprite(c, int16_t(xy), int16_t(xy >> 16),
                 spriteUv(0x7fc0, dataScalar<uint8_t>(e + 4), dataScalar<uint8_t>(e + 5)),
                 dataScalar<uint8_t>(e + 6), dataScalar<uint8_t>(e + 7));
    }
    if (m.layer == 2)
        otQuad(c, {{origin.x - 0x14, origin.x + 0x1c, origin.x - 0x14, origin.x + 0x1c}},
               {{origin.y - 0x13, origin.y - 0x13, origin.y + 0x1d, origin.y + 0x1d}}, kNeutral, kPolyFT4,
               {{0x20, 0x50, 0x20, 0x50}}, {{0x20, 0x20, 0x50, 0x50}}, 0x7fc0, 0x9c);
    if (m.layer == g.stage - 0x3d) playerDraw(c, origin, point);
    otClipDrawArea(c);
}

// exe 0x5551a0 / 0x5551f0 / 0x555480: the town picture and sub 1's separately scaled picture.
void lateMap() {
    const GameState& g = gameState();
    StatusMapState& m = statusMap();
    if (!m.positioned) {
        m.area = g.stage == 0x40 ? g.room : 0xe;
        m.positioned = 1;
    }
    MapPoint point;
    if (m.area == 1) point = {player().pos.x.whole >> 6, -(player().pos.z.whole >> 6)};
    else point = picturePoint(latePictures[m.area]);
    if (!m.ownPositioned) {
        m.layer = 0;
        m.layerLabel = m.area == 1 ? 0x12 : 0;
        if (m.area == 1) mapBounds(-0x60, -0x60, 0x60, 0x60, 1);
        else mapBounds(0, 0, 0x100, 0x100, 1);
        m.scrollX = int16_t(point.x);
        m.scrollY = int16_t(point.y);
        m.ownPositioned = 1;
    }
    mapClamp();
    const size_t c = mapOpen(m.area == 1 ? 0x1b : 0x88);
    const MapPoint origin = mapOrigin();
    if (m.area == 1) pictureDraw(c, {origin.x - 0x80, origin.y - 0x80}, 0x7f80);
    else pictureDraw(c, origin);
    playerDraw(c, origin, point);
    otClipDrawArea(c);
}

} // namespace

void statusMapRouteIconDraw(size_t c, int16_t x, int16_t y, bool route) {
    using namespace menu_draw;
    otDrawMode(c, 0xd);
    if (!route) {
        otSprite(c, x, y, 0x7fd3a8b0, 0x10, 0x10, kNeutral, kSprite16);
        return;
    }
    uint32_t frame = gameState().frame;
    if (display().pace == 1) frame >>= 1;
    const uint32_t phase = (frame / 3) & 3;
    constexpr uint8_t u[] = {0x88, 0x98, 0xa8, 0xb0};
    otSprite(c, x, int16_t(y - 0xc), spriteUv(0x7fd3, u[phase], 0x90), phase < 2 ? 0x10 : 8, 0x10);
}

void statusMapStageDraw() {
    const RoutineId callback = stage().secondRoomCallback;
    if (statusMapTilesDraw(callback)) return;
    switch (callback) {
    case RoutineId::CalbaniaRoomsSquareSecondCallback: squareMap(); break;
    case RoutineId::St04SecondRoomCallback: flutterMap(); break;
    case RoutineId::St08SecondRoomCallback: townMap(); break;
    case RoutineId::St10SecondRoomCallback: islandMap(); break;
    case RoutineId::St15SecondRoomCallback: statusMapNinoDraw(); break;
    case RoutineId::St24SecondRoomCallback: pokteMap(); break;
    case RoutineId::St29SecondRoomCallback: desertMap(); break;
    case RoutineId::St3DSecondRoomCallback: islandRoomsMap(); break;
    case RoutineId::St40SecondRoomCallback: lateMap(); break;
    default: notPortedAt("status map", "stage callback", uint32_t(callback));
    }
}

} // namespace od::port
