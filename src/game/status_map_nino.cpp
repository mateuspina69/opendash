// The Nino map page's callback (see status_map_nino.hpp).
#include "game/status_map_nino.hpp"

#include "content/nino/minimaps.hpp"
#include "engine/data_source.hpp"
#include "game/display.hpp"
#include "game/game_state.hpp"
#include "game/loaded_data.hpp"
#include "game/menu_draw.hpp"
#include "game/minimap.hpp"
#include "game/player/player_state.hpp"
#include "game/status_map.hpp"
#include "game/status_map_stage.hpp"
#include "game/visited_rooms.hpp"

namespace od::port {

using namespace menu_draw;

namespace {

// the areas' positions (exe table 0x8b06f0): the sprite origin, a fixed player point, whether that point is used
struct NinoMapPosition : DataRecord {
    int16_t x = 0, y = 0, playerX = 0, playerY = 0;
    uint32_t fixed = 0;
    static NinoMapPosition load(const TableReader& r) { return {{}, r.s16(0), r.s16(2), r.s16(4), r.s16(6), r.u32(8)}; }
};
const DataTable<RecordTable<NinoMapPosition, 15, 12>> positions{TableId::StatusMapSt15Positions};
const DataTable<ScalarTable<uint32_t, 8>> spriteLists{TableId::St18MinimapSpriteLists};
// the two layers' common sprites (exe tables 0x8b05e0 / 0x8b0668), including their end entries
struct NinoMapSprite : DataRecord {
    int16_t x = 0, y = 0;
    uint16_t uv = 0, size = 0;
    static NinoMapSprite load(const TableReader& r) { return {{}, r.s16(0), r.s16(2), r.u16(4), r.u16(6)}; }
};
const DataTable<RecordTable<NinoMapSprite, 17, 8>> firstSprites{TableId::StatusMapSt15FirstSprites};
const DataTable<RecordTable<NinoMapSprite, 9, 8>> secondSprites{TableId::StatusMapSt15SecondSprites};

// the floor the player is on (the sprite map's floors change after story 9)
uint8_t playerLayer() {
    const GameState& g = gameState();
    switch (g.stage) {
    case 0x15:
    case 0x16:
    case 0x17: return g.story < 0xa ? 2 : 0;
    case 0x18: return uint8_t(g.room == 2);
    case 0x19: return uint8_t((g.story < 0xa ? 2 : 0) + (g.room != 0));
    default: return g.story < 0xa ? 3 : 1;
    }
}
// the floor a route / preview destination's link records belong to
uint8_t targetLayer(uint32_t records) {
    const uint32_t links = visitedRooms().linkRecords;
    if (records == links) return gameState().story < 0xa ? 2 : 0;
    if (records == links + 0x48) return 1;
    return gameState().story < 0xa ? 3 : 1;
}

} // namespace

// exe 0x54f490: the island's sprite map, the floor selected, the player / Flutter and the route / preview destinations
void statusMapNinoDraw() {
    StatusMapState& m = statusMap();
    const GameState& g = gameState();
    if (!m.positioned) {
        switch (g.stage) {
        case 0x15: m.area = 0; break;
        case 0x16: m.area = 1; break;
        case 0x17: m.area = 2; break;
        case 0x18: m.area = uint8_t(g.room + 3); break;
        case 0x19: m.area = uint8_t(g.room + 6); break;
        case 0x1a: m.area = uint8_t(g.room + 8); break;
        case 0x1b: m.area = uint8_t(g.room + 0xa); break;
        default: return;
        }
        if (g.story >= 0xa && g.stage == 0x18) return;
        m.layer = playerLayer();
        m.layerCount = g.story >= 0xa ? 2 : 4;
        m.layerLabel = g.story >= 0xa ? 0x22 : 0x24;
        m.previousLayer = 0xff;
        m.minX = -0xa0;
        m.minY = -0x50;
        m.maxX = 0xa0;
        m.maxY = 0xb0;
    }
    const NinoMapPosition& a = positions[m.area];
    const int16_t px = int16_t(a.x + (a.fixed ? a.playerX : player().pos.x.whole >> 6));
    const int16_t py = int16_t(a.y + (a.fixed ? a.playerY : -(player().pos.z.whole >> 6)));
    if (!m.positioned) {
        m.scrollX = px;
        m.scrollY = py;
        m.positioned = 1;
    }
    if (m.area == 2) od::content::ninoMapTownSpritesUpdate();
    if (m.previewRecords) {
        m.scrollX = dataScalar<int8_t>(m.previewRecords + 2);
        m.scrollY = dataScalar<int8_t>(m.previewRecords + 3);
        m.layer = targetLayer(m.previewRecords);
    }
    statusMapClamp();
    m.previousLayer = m.layer;
    const uint32_t mask = g.story >= 0xa ? (m.layer == 0 ? 0x47 : 0x80)
                          : m.layer == 0 ? 8 : m.layer == 1 ? 0x20 : m.layer == 2 ? 0x47 : 0x80;
    const size_t c = otOpen(kOtOverlay);
    otClip(c, m.x, displayDrawArea().y + m.y, m.w, m.h);
    otDrawMode(c, 0x88);
    const int16_t x = int16_t(m.x + (m.w >> 1) - m.scrollX);
    const int16_t y = int16_t(m.y + (m.h >> 1) - m.scrollY);
    MinimapLanes l = minimapLanes();
    l.x = x;
    l.y = y;
    l.clut = 0x7fc0;
    for (int row = -0x50; row < 0xb0; row += 0x10)
        for (int col = -0xa0; col < 0xa0; col += 0x20) {
            l.cornerX = int16_t(x + col);
            l.cornerY = int16_t(y + row);
            otSprite(c, l.cornerX.get(), l.cornerY.get(), 0x7fc0c0e0, 0x20, 0x10);
        }
    if (g.story >= 0xa || m.layer != 0) od::content::ninoMapSpriteListDraw(c, firstSprites.stateAddress());
    if ((g.story >= 0xa && m.layer == 0) || (g.story < 0xa && m.layer == 2))
        od::content::ninoMapSpriteListDraw(c, secondSprites.stateAddress());
    for (uint32_t i = 0; i < 8; ++i) {
        if (!(mask & (1u << i))) continue;
        l.x = int16_t(x + positions[i].x);
        l.y = int16_t(y + positions[i].y);
        od::content::ninoMapSpriteListDraw(c, spriteLists[i]);
    }
    if ((g.story >= 0xa && m.layer == 0) || (g.story >= 7 && m.layer == 2))
        minimapIcon(c, int16_t(x - 0x13), int16_t(y + 0x7a), 2);
    else if (g.story < 7 && m.layer == 2)
        minimapIcon(c, int16_t(x - 0x3c), int16_t(y + 0x90), 2);
    if (m.layer == playerLayer()) minimapPlayerHeading(c, int16_t(x + px), int16_t(y + py));
    const uint32_t route = mapMarker().records;
    if (mapRouteState() == MapRouteState::Set && route && m.layer == targetLayer(route))
        statusMapRouteIconDraw(c, int16_t(x + dataScalar<int8_t>(route + 2)),
                               int16_t(y + dataScalar<int8_t>(route + 3)), true);
    if (m.previewRecords)
        statusMapRouteIconDraw(c, int16_t(x + dataScalar<int8_t>(m.previewRecords + 2)),
                               int16_t(y + dataScalar<int8_t>(m.previewRecords + 3)), false);
    otClipDrawArea(c);
}

} // namespace od::port
