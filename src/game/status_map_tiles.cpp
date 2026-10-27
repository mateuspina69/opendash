// The ruin maps of the status sub-screen: each floor's explored cells, the treasure dots and the player's heading.
#include "game/status_map_tiles.hpp"

#include "game/display.hpp"
#include "game/game_state.hpp"
#include "game/loaded_data.hpp"
#include "game/menu_draw.hpp"
#include "game/minimap.hpp"
#include "game/player/player_state.hpp"
#include "game/status_map.hpp"
#include "game/story_flags.hpp"

namespace od::port {

namespace {

using namespace menu_draw;

// The minimap's area records also hold the room's placement in the full map (the last two shorts).
template <size_t N> struct AreaMap {
    AreaMap(TableId flags, TableId placements, uint32_t first, int8_t kind = -1)
        : bases(flags), areas(placements), firstBase(first), subruinKind(kind) {}
    DataTable<ScalarTable<uint16_t, N>> bases;
    DataTable<RecordTable<MinimapArea, N, 8>> areas;
    uint32_t firstBase;
    int8_t subruinKind = -1;
};
const AreaMap<22> st12{TableId::St12MinimapVisitedBases, TableId::St12MinimapAreas, 0};
const AreaMap<29> st26{TableId::St26MinimapVisitedBases, TableId::St26MinimapAreas, 0x68};
const AreaMap<30> st2d{TableId::St2DMinimapVisitedBases, TableId::St2DMinimapAreas, 0x12b};
const AreaMap<13> st0f{TableId::St0FMinimapVisitedBases, TableId::St0FMinimapAreas, 0x1bc};
const AreaMap<49> st35{TableId::St35MinimapVisitedBases, TableId::St35MinimapAreas, 0x2f1};
const AreaMap<31> st41{TableId::St41MinimapVisitedBases, TableId::St41MinimapAreas, 0x3a7};
const AreaMap<26> st43{TableId::St43MinimapVisitedBases, TableId::St43MinimapAreas, 0x431};
const AreaMap<7> st4a{TableId::St4AMinimapVisitedBases, TableId::St4AMinimapAreas, 0, 0};
const AreaMap<9> st4d{TableId::St4DMinimapVisitedBases, TableId::St4DMinimapAreas, 0, 1};
const AreaMap<11> st50{TableId::St50MinimapVisitedBases, TableId::St50MinimapAreas, 0, 2};
const AreaMap<13> st51{TableId::St51MinimapVisitedBases, TableId::St51MinimapAreas, 0x238};
const AreaMap<14> st4f{TableId::St4FMinimapVisitedBases, TableId::St4FMinimapAreas, 0x274};
const DataTable<ScalarTable<uint8_t, 29>> st26Layers{TableId::StatusMapSt26Layers};
const DataTable<ScalarTable<uint8_t, 30>> st2dLayers{TableId::StatusMapSt2DLayers};
const DataTable<ScalarTable<uint8_t, 13>> st0fLayers{TableId::StatusMapSt0FLayers};
const DataTable<ScalarTable<uint32_t, 22>> st12Dots{TableId::St12MinimapDots};
const DataTable<ScalarTable<uint32_t, 29>> st26Dots{TableId::St26MinimapDots};
const DataTable<ScalarTable<uint32_t, 30>> st2dDots{TableId::St2DMinimapDots};
const DataTable<ScalarTable<uint32_t, 49>> st35Dots{TableId::St35MinimapDots};
const DataTable<ScalarTable<uint32_t, 31>> st41Dots{TableId::St41MinimapDots};
const DataTable<ScalarTable<uint16_t, 3>> subruinFirstBases{TableId::SubruinMinimapFirstBases};

struct MapPoint {
    int32_t x, y;
};
// The area's far edges in map pixels, less its placement (exe 0x54bb20 ...).
MapPoint areaEdges(const MinimapArea& a) {
    return {((((a.cellX + a.columns) << 9) - 0x7dc1) >> 6) - a.pageX,
            ((((a.cellZ + a.rows) << 9) - 0x7dc1) >> 6) + a.pageZ};
}
MapPoint mapOrigin() {
    const StatusMapState& m = statusMap();
    return {int16_t((m.w >> 1) - m.scrollX + m.x), int16_t((m.h >> 1) - m.scrollY + m.y)};
}
void mapBounds(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t count, uint8_t label = 0x10) {
    StatusMapState& m = statusMap();
    m.minX = x;
    m.minY = y;
    m.maxX = w;
    m.maxY = h;
    m.layerCount = count;
    m.previousLayer = 0xff;
    m.layerLabel = label;
}
void mapPosition(const MapPoint& edge) {
    StatusMapState& m = statusMap();
    m.scrollX = int16_t((player().pos.x.whole >> 6) - edge.x);
    m.scrollY = int16_t(edge.y - (player().pos.z.whole >> 6));
    m.positioned = 1;
}
size_t mapOpen(uint16_t mode = 0x1d) {
    const StatusMapState& m = statusMap();
    const size_t c = otOpen(kOtOverlay);
    otClip(c, m.x, displayDrawArea().y + m.y, m.w, m.h);
    otDrawMode(c, mode);
    return c;
}
uint64_t areaRange(uint32_t first, uint32_t end) { return (uint64_t(1) << end) - (uint64_t(1) << first); }
bool areaSelected(uint64_t mask, uint32_t area) { return (mask & (uint64_t(1) << area)) != 0; }

// exe 0x54baa0: the Manda tiles the story changes, by the area being drawn.
uint8_t mandaTexel(uint8_t t, uint32_t area) {
    switch (area) {
    case 0:
        if ((storyFlag(0x5b6) || gameState().story >= 5) && t >= 0x11 && t <= 0x15) return uint8_t(t + 0x3f);
        return t;
    case 8: return storyFlag(1) && t == 0x3d ? 0x5e : t;
    case 0xc: return storyFlag(2) && t == 0x41 ? 0x5f : t;
    default: return t;
    }
}

// exe 0x54bb20 / 0x54d1b0 / 0x54df80 / 0x550230 / 0x5512f0 / 0x551d90 / 0x552750 / 0x553280 /
// 0x555c90 / 0x556b20: the tiles of an area, row zero at the bottom; only 0xff is omitted on the full map.
template <size_t N> void areaTiles(size_t c, const AreaMap<N>& map, uint32_t area, uint32_t unexplored,
                                 uint16_t clut, uint8_t rowOffset = 0, bool manda = false) {
    const MinimapArea& a = map.areas[area];
    const uint16_t base = map.bases[area];
    const uint32_t firstBase = map.subruinKind < 0 ? map.firstBase : subruinFirstBases[size_t(map.subruinKind)];
    const uint32_t tiles = minimapLayer(statusMap().tileLayer) + uint16_t((base - firstBase) * 8);
    const MapPoint origin = mapOrigin();
    const int16_t x = int16_t(a.pageX - a.columns * 8 + origin.x);
    const int16_t y = int16_t(a.rows * 8 - 0x10 + a.pageZ + origin.y);
    for (int32_t row = 0; row < a.rows; ++row)
        for (int32_t column = 0; column < a.columns; ++column) {
            const int32_t cell = row * a.columns + column;
            uint8_t t = dataScalar<uint8_t>(tiles + uint32_t(cell));
            if (t == 0xff) continue;
            const uint32_t colour = minimapCellExplored(base, cell) ? kNeutral : unexplored;
            if (colour == 0) continue;
            if (manda) t = mandaTexel(t, area);
            otSprite(c, int16_t(x + column * 0x10), int16_t(y - row * 0x10),
                     spriteUv(clut, uint8_t(t << 4), uint8_t((t & 0xf0) + rowOffset)), 0x10, 0x10,
                     colour, kSprite16);
        }
}
template <size_t N> void floorTiles(size_t c, const AreaMap<N>& map, uint64_t selected, uint32_t unexplored,
                                  uint16_t clut = 0x7fc0, uint8_t rowOffset = 0, bool manda = false) {
    for (uint32_t area = 0; area < N; ++area)
        if (areaSelected(selected, area)) areaTiles(c, map, area, unexplored, clut, rowOffset, manda);
}
// The treasure dots of each selected room; the sub-screen's half-speed yellow pulse, an opened treasure dark.
template <size_t N> void floorDots(size_t c, const AreaMap<N>& map,
                                 const DataTable<ScalarTable<uint32_t, N>>& dots, uint64_t selected) {
    const uint32_t pulse = minimapPulse((uint8_t(gameState().frame) >> 1) & 0x1f);
    const uint32_t colour = pulse | pulse << 8;
    const MapPoint origin = mapOrigin();
    for (uint32_t area = 0; area < N; ++area) {
        if (!areaSelected(selected, area)) continue;
        const MapPoint edge = areaEdges(map.areas[area]);
        for (uint32_t e = dots[area]; dataScalar<uint16_t>(e) != 0xffff; e += 8) {
            const int16_t x = dataScalar<int16_t>(e + 4), z = dataScalar<int16_t>(e + 6);
            otTile(c, int16_t((x >> 6) - edge.x + origin.x - 2),
                   int16_t(edge.y - (z >> 6) + origin.y - 2), 4, 4,
                   storyFlag(dataScalar<uint16_t>(e)) ? 0x008080 : colour, kTileOpaque);
        }
    }
}
void mapPlayer(size_t c, const MapPoint& edge) {
    const MapPoint origin = mapOrigin();
    minimapEntranceMarker(c, int16_t((minimap().entryX >> 6) - edge.x + origin.x),
                         int16_t(edge.y - (minimap().entryZ >> 6) + origin.y));
    minimapPlayerHeading(c, int16_t((player().pos.x.whole >> 6) - edge.x + origin.x),
                         int16_t(edge.y - (player().pos.z.whole >> 6) + origin.y));
}
void mapClamp() {
    statusMapClamp();
    statusMap().previousLayer = statusMap().layer;
}

// exe 0x54bb20: Manda's three floors (the first room also appears on the middle floor).
void st12Map() {
    const GameState& g = gameState();
    if (g.stage == 0x12 && g.room == 6) return;
    StatusMapState& m = statusMap();
    if (!m.positioned) {
        if (g.stage == 0x12) m.tileLayer = 7, m.area = g.room, m.layer = 2;
        else if (g.stage == 0x13) m.tileLayer = 0xb, m.area = uint8_t(g.room + 6), m.layer = 1;
        else m.tileLayer = 5, m.area = uint8_t(g.room + 0x11), m.layer = 0;
        mapBounds(-0xb0, -0xb0, 0xc0, 0xc0, 3);
        mapPosition(areaEdges(st12.areas[m.area]));
    }
    const MapPoint edge = areaEdges(st12.areas[m.area]);
    mapClamp();
    uint32_t first, end;
    uint16_t clut;
    uint8_t row;
    if (m.layer == 2) first = 0, end = 6, row = 0, clut = 0x7dc0;
    else if (m.layer == 1) first = 6, end = 0x11, row = 0x60, clut = 0x7dc1;
    else first = 0x11, end = 0x16, row = 0xc0, clut = 0x7dcd;
    const uint64_t selected = areaRange(first, end);
    const uint32_t dim = storyFlag(3) ? 0x303030 : 0;
    const size_t c = mapOpen();
    floorTiles(c, st12, selected, dim, clut, row, true);
    if (m.layer == 1) areaTiles(c, st12, 0, dim, 0x7dc0, 0, true);
    if (storyFlag(4)) floorDots(c, st12, st12Dots, selected);
    if (g.stage == 0x14 - m.layer) mapPlayer(c, edge);
    otClipDrawArea(c);
}

// exe 0x54d1b0: the fire ruin, its three floors selected by room masks.
void st26Map() {
    const GameState& g = gameState();
    StatusMapState& m = statusMap();
    if (!m.positioned) {
        switch (g.stage) {
        case 0x26: m.tileLayer = 7, m.area = g.room; break;
        case 0x27: m.tileLayer = 9, m.area = uint8_t(g.room + 7); break;
        case 0x28: m.tileLayer = 6, m.area = uint8_t(g.room + 0x10); break;
        case 0x33: m.tileLayer = 1, m.area = uint8_t(g.room + 0x16); break;
        default: m.tileLayer = 6, m.area = uint8_t(g.room + 0x17); break;
        }
        m.layer = st26Layers[m.area];
        mapBounds(-0x168, -0x1b0, 0x168, 0x1b0, 3);
        mapPosition(areaEdges(st26.areas[m.area]));
    }
    const MapPoint edge = areaEdges(st26.areas[m.area]);
    mapClamp();
    const uint64_t selected = m.layer == 0 ? 0x003f001f : m.layer == 1 ? 0x20 : 0x1fc0ffc0;
    const size_t c = mapOpen();
    floorTiles(c, st26, selected, storyFlag(0x60) ? 0x303030 : 0);
    if (storyFlag(0x61)) floorDots(c, st26, st26Dots, selected);
    if (m.layer == st26Layers[m.area]) mapPlayer(c, edge);
    otClipDrawArea(c);
}

// exe 0x54df80: the ice ruin's five floors (ST2D sub 9 has no map).
void st2dMap() {
    const GameState& g = gameState();
    if (g.stage == 0x2d && g.room == 9) return;
    StatusMapState& m = statusMap();
    if (!m.positioned) {
        if (g.stage == 0x2d) m.tileLayer = 0xa, m.area = g.room;
        else if (g.stage == 0x2e) m.tileLayer = 0xc, m.area = uint8_t(g.room + 9);
        else m.tileLayer = 9, m.area = uint8_t(g.room + 0x15);
        m.layer = st2dLayers[m.area];
        mapBounds(-0x108, -0x100, 0x108, 0x100, 5);
        mapPosition(areaEdges(st2d.areas[m.area]));
    }
    const MapPoint edge = areaEdges(st2d.areas[m.area]);
    mapClamp();
    constexpr uint32_t masks[] = {0x1ff, 0xffe00, 0x100000, 0x7e00000, 0x38000000};
    const uint64_t selected = masks[m.layer];
    const size_t c = mapOpen();
    floorTiles(c, st2d, selected, storyFlag(0x70) ? 0x303030 : 0);
    if (storyFlag(0x71)) floorDots(c, st2d, st2dDots, selected);
    if (m.layer == st2dLayers[m.area]) mapPlayer(c, edge);
    otClipDrawArea(c);
}

// exe 0x550230: the abandoned mine's three floors, the lower two only explored.
void st0fMap() {
    const GameState& g = gameState();
    StatusMapState& m = statusMap();
    if (!m.positioned) {
        m.tileLayer = 0xd;
        m.layer = g.room <= 4 ? 0 : g.room <= 9 ? 1 : 2;
        const uint8_t label = m.layerLabel;
        mapBounds(-0xe0, -0xa0, 0xe0, 0xa0, 3, label);
        mapPosition(areaEdges(st0f.areas[g.room]));
    }
    const MapPoint edge = areaEdges(st0f.areas[g.room]);
    mapClamp();
    const uint64_t selected = m.layer == 0 ? areaRange(0, 5) : m.layer == 1 ? areaRange(5, 10) : areaRange(10, 13);
    const size_t c = mapOpen();
    floorTiles(c, st0f, selected, m.layer == 0 ? 0x303030 : 0);
    if (m.layer == st0fLayers[g.room]) mapPlayer(c, edge);
    otClipDrawArea(c);
}

// exe 0x553280: the water ruin's five floors.
void st35Map() {
    const GameState& g = gameState();
    StatusMapState& m = statusMap();
    if (!m.positioned) {
        switch (g.stage) {
        case 0x35: m.tileLayer = 6, m.area = g.room, m.layer = g.room > 2 ? 4 : 0; break;
        case 0x36: m.tileLayer = 0xe, m.area = uint8_t(g.room + 6), m.layer = 1; break;
        case 0x37: m.tileLayer = 0x10, m.area = uint8_t(g.room + 0x14), m.layer = 2; break;
        default: m.tileLayer = 0xd, m.area = uint8_t(g.room + 0x24), m.layer = 3; break;
        }
        mapBounds(-0x108, -0x100, 0x108, 0x100, 5);
        mapPosition(areaEdges(st35.areas[m.area]));
    }
    const MapPoint edge = areaEdges(st35.areas[m.area]);
    mapClamp();
    constexpr uint8_t first[] = {0, 6, 0x14, 0x24, 3}, end[] = {3, 0x14, 0x24, 0x31, 6};
    const uint64_t selected = areaRange(first[m.layer], end[m.layer]);
    const size_t c = mapOpen();
    floorTiles(c, st35, selected, storyFlag(0x98) ? 0x303030 : 0, 0x7dc8);
    if (storyFlag(0x99)) floorDots(c, st35, st35Dots, selected);
    if (areaSelected(selected, m.area)) mapPlayer(c, edge);
    otClipDrawArea(c);
}

// exe 0x555c90: the second fire ruin's two floors.
void st41Map() {
    const GameState& g = gameState();
    StatusMapState& m = statusMap();
    if (!m.positioned) {
        if (g.stage == 0x41) m.tileLayer = 0xf, m.area = g.room, m.layer = 1;
        else m.tileLayer = 0x10, m.area = uint8_t(g.room + 0xf), m.layer = 0;
        mapBounds(-0x138, -0x120, 0x138, 0x120, 2);
        mapPosition(areaEdges(st41.areas[m.area]));
    }
    const MapPoint edge = areaEdges(st41.areas[m.area]);
    mapClamp();
    const uint64_t selected = m.layer == 1 ? areaRange(0, 0xf) : areaRange(0xf, 0x1f);
    const size_t c = mapOpen();
    floorTiles(c, st41, selected, storyFlag(0x156) ? 0x303030 : 0);
    if (storyFlag(0x157)) floorDots(c, st41, st41Dots, selected);
    if (areaSelected(selected, m.area)) mapPlayer(c, edge);
    otClipDrawArea(c);
}

// exe 0x556b20: the final ruin's five floors, only the explored cells.
void st43Map() {
    const GameState& g = gameState();
    StatusMapState& m = statusMap();
    if (!m.positioned) {
        switch (g.stage) {
        case 0x43: m.tileLayer = 5, m.area = g.room, m.layer = 0; break;
        case 0x58: m.tileLayer = 5, m.area = uint8_t(g.room + 5), m.layer = 1; break;
        case 0x44: m.tileLayer = 5, m.area = uint8_t(g.room + 0xa), m.layer = 2; break;
        case 0x45: m.tileLayer = 6, m.area = uint8_t(g.room + 0xf), m.layer = 3; break;
        default: m.tileLayer = 5, m.area = uint8_t(g.room + 0x15), m.layer = 4; break;
        }
        mapBounds(-0x100, -0xd0, 0x100, 0xd0, 5, 0x13);
        mapPosition(areaEdges(st43.areas[m.area]));
    }
    const MapPoint edge = areaEdges(st43.areas[m.area]);
    mapClamp();
    constexpr uint8_t first[] = {0, 5, 0xa, 0xf, 0x15}, end[] = {5, 0xa, 0xf, 0x15, 0x1a};
    const uint64_t selected = areaRange(first[m.layer], end[m.layer]);
    const size_t c = mapOpen(0xc);
    floorTiles(c, st43, selected, 0);
    if (areaSelected(selected, m.area)) mapPlayer(c, edge);
    otClipDrawArea(c);
}

// exe 0x5512f0: the three kinds of sub-ruin, each with one floor.
void st4aMap() {
    const GameState& g = gameState();
    StatusMapState& m = statusMap();
    if (!m.positioned) {
        switch (g.stage) {
        case 0x4a:
        case 0x55: m.area = 0, m.tileLayer = 7; mapBounds(-0xa0, -0x58, 0xa0, 0x58, 1); break;
        case 0x4d: m.area = 1, m.tileLayer = 9; mapBounds(-0xb8, -0xa0, 0xb8, 0xa0, 1); break;
        case 0x50: m.area = 2, m.tileLayer = 0xb; mapBounds(-0xd0, -0xf0, 0xd0, 0xf0, 1); break;
        default: return;
        }
        m.layer = 0;
        mapPosition(areaEdges(m.area == 0 ? st4a.areas[g.room] : m.area == 1 ? st4d.areas[g.room] : st50.areas[g.room]));
    }
    const MapPoint edge = areaEdges(m.area == 0 ? st4a.areas[g.room] : m.area == 1 ? st4d.areas[g.room] : st50.areas[g.room]);
    mapClamp();
    const size_t c = mapOpen();
    if (m.area == 0) floorTiles(c, st4a, areaRange(0, 7), 0x303030);
    else if (m.area == 1) floorTiles(c, st4d, areaRange(0, 9), 0x303030);
    else floorTiles(c, st50, areaRange(0, 11), 0);
    mapPlayer(c, edge);
    otClipDrawArea(c);
}

// exe 0x551d90: ST4E / ST51, two floors of explored cells.
void st51Map() {
    const GameState& g = gameState();
    StatusMapState& m = statusMap();
    if (!m.positioned) {
        if (g.stage == 0x4e) m.tileLayer = 0xa, m.area = g.room, m.layer = 0;
        else if (g.stage == 0x51) m.tileLayer = 3, m.area = uint8_t(g.room + 0xa), m.layer = 1;
        else return;
        mapBounds(-0xd0, -0xf8, 0xd0, 0xf8, 2);
        mapPosition(areaEdges(st51.areas[m.area]));
    }
    const MapPoint edge = areaEdges(st51.areas[m.area]);
    mapClamp();
    const uint64_t selected = m.layer == 0 ? areaRange(0, 10) : areaRange(10, 13);
    const size_t c = mapOpen();
    floorTiles(c, st51, selected, 0);
    if (areaSelected(selected, m.area)) mapPlayer(c, edge);
    otClipDrawArea(c);
}

// exe 0x552750: ST4F, the boss room on the second floor.
void st4fMap() {
    const GameState& g = gameState();
    StatusMapState& m = statusMap();
    if (!m.positioned) {
        m.tileLayer = 0xe;
        m.layer = uint8_t(g.room >= 0xd);
        mapBounds(-0x160, -0xb0, 0x160, 0xb0, 2);
        mapPosition(areaEdges(st4f.areas[g.room]));
    }
    const MapPoint edge = areaEdges(st4f.areas[g.room]);
    mapClamp();
    const uint64_t selected = m.layer == 0 ? areaRange(0, 13) : areaRange(13, 14);
    const size_t c = mapOpen();
    floorTiles(c, st4f, selected, 0);
    if (areaSelected(selected, g.room)) mapPlayer(c, edge);
    otClipDrawArea(c);
}

} // namespace

bool statusMapTilesDraw(RoutineId callback) {
    switch (callback) {
    case RoutineId::St12SecondRoomCallback: st12Map(); return true;
    case RoutineId::St26SecondRoomCallback: st26Map(); return true;
    case RoutineId::St2DSecondRoomCallback: st2dMap(); return true;
    case RoutineId::St0FSecondRoomCallback: st0fMap(); return true;
    case RoutineId::St35SecondRoomCallback: st35Map(); return true;
    case RoutineId::St41SecondRoomCallback: st41Map(); return true;
    case RoutineId::St43SecondRoomCallback: st43Map(); return true;
    case RoutineId::St4ASecondRoomCallback: st4aMap(); return true;
    case RoutineId::St51SecondRoomCallback: st51Map(); return true;
    case RoutineId::St4FSecondRoomCallback: st4fMap(); return true;
    default: return false;
    }
}

} // namespace od::port
