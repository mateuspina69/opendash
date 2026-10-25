// The minimap callbacks of the sub-ruins (see minimaps.hpp).
#include "content/subruins/minimaps.hpp"

#include "game/game_state.hpp"
#include "game/loaded_data.hpp"
#include "game/minimap.hpp"

namespace od::content {

using namespace od::port;

namespace {

constexpr uint32_t kUnexploredDim = 0x303030;

// ---- ST4F: an area per sub-room, its explored cells from byte 0x274 on; only the explored cells -----------------
constexpr uint32_t kSt4FRooms = 14;
const DataTable<ScalarTable<uint16_t, kSt4FRooms>> st4fVisitedBases{TableId::St4FMinimapVisitedBases};
const DataTable<RecordTable<MinimapArea, kSt4FRooms, 8>> st4fAreas{TableId::St4FMinimapAreas};
constexpr uint8_t kSt4FLayer = 0xe;
constexpr uint32_t kSt4FFirstBase = 0x274;

// exe 0x552290
void st4fMinimap() {
    MinimapState& m = minimap();
    const uint8_t room = gameState().room;
    if (!m.ready) {
        m.ready = 1;
        m.visitedBase = st4fVisitedBases[room];
    }
    const MinimapArea& a = st4fAreas[room];
    minimapMarkCell(m.visitedBase, minimapPlayerCell(a));
    if (!minimapOnScreen()) return;
    MinimapTileStyle s;
    s.layer = kSt4FLayer;
    s.firstBase = kSt4FFirstBase;
    minimapAreaPanel(a, s);
}

// ---- ST4E / ST51: ST4E's sub-rooms are areas 0.. (layer 0xa), ST51's 10.. (layer 3); the explored cells from byte
// 0x238 on; only the explored cells
constexpr uint32_t kSt51Areas = 13;
const DataTable<ScalarTable<uint16_t, kSt51Areas>> st51VisitedBases{TableId::St51MinimapVisitedBases};
const DataTable<RecordTable<MinimapArea, kSt51Areas, 8>> st51Areas{TableId::St51MinimapAreas};
constexpr uint8_t kSt4EStage = 0x4e, kSt51Stage = 0x51;
constexpr uint8_t kSt4ELayer = 0xa, kSt51Layer = 3;
constexpr uint8_t kSt51FirstArea = 10;
constexpr uint32_t kSt51FirstBase = 0x238;

// exe 0x551890
void st51Minimap() {
    MinimapState& m = minimap();
    const GameState& g = gameState();
    if (!m.ready) {
        if (g.stage == kSt4EStage) {
            m.layer = kSt4ELayer;
            m.area = g.room;
        } else if (g.stage == kSt51Stage) {
            m.layer = kSt51Layer;
            m.area = uint8_t(g.room + kSt51FirstArea);
        } else {
            return; // another stage: nothing
        }
        m.ready = 1;
        m.visitedBase = st51VisitedBases[m.area];
    }
    const MinimapArea& a = st51Areas[m.area];
    minimapMarkCell(m.visitedBase, minimapPlayerCell(a));
    if (!minimapOnScreen()) return;
    MinimapTileStyle s;
    s.layer = m.layer;
    s.firstBase = kSt51FirstBase;
    minimapAreaPanel(a, s);
}

// ---- ST4A / ST55 (kind 0, layer 7), ST4D (kind 1, layer 9), ST50 (kind 2, layer 0xb): an area per sub-room of the
// kind's tables, the explored cells from the kind's first byte on; the unexplored ones dim but in kind 2
constexpr uint32_t kSubruinKinds = 3;
const DataTable<ScalarTable<uint16_t, kSubruinKinds>> subruinFirstBases{TableId::SubruinMinimapFirstBases};
const DataTable<ScalarTable<uint16_t, 7>> st4aVisitedBases{TableId::St4AMinimapVisitedBases};
const DataTable<RecordTable<MinimapArea, 7, 8>> st4aAreas{TableId::St4AMinimapAreas};
const DataTable<ScalarTable<uint16_t, 9>> st4dVisitedBases{TableId::St4DMinimapVisitedBases};
const DataTable<RecordTable<MinimapArea, 9, 8>> st4dAreas{TableId::St4DMinimapAreas};
const DataTable<ScalarTable<uint16_t, 11>> st50VisitedBases{TableId::St50MinimapVisitedBases};
const DataTable<RecordTable<MinimapArea, 11, 8>> st50Areas{TableId::St50MinimapAreas};
constexpr uint8_t kSubruinBrightKind = 2;
constexpr uint8_t kSubruinNoTileExtra = 0x81;

uint16_t subruinVisitedBase(uint8_t kind, uint8_t room) {
    if (kind == 0) return st4aVisitedBases[room];
    if (kind == 1) return st4dVisitedBases[room];
    return st50VisitedBases[room];
}
const MinimapArea& subruinArea(uint8_t kind, uint8_t area) {
    if (kind == 0) return st4aAreas[area];
    if (kind == 1) return st4dAreas[area];
    return st50Areas[area];
}
bool subruinNoTile(uint8_t t) { return minimapNoTile(t) || t == kSubruinNoTileExtra; }

// exe 0x550d50 (the kind's tables through the pointer tables 0x8b09d8 / 0x8b09e4)
void subruinMinimap() {
    MinimapState& m = minimap();
    const GameState& g = gameState();
    if (!m.ready) {
        switch (g.stage) {
        case 0x4a:
        case 0x55: m.areaExtra = 0, m.layer = 7; break;
        case 0x4d: m.areaExtra = 1, m.layer = 9; break;
        case 0x50: m.areaExtra = 2, m.layer = 0xb; break;
        default: return;
        }
        m.area = g.room;
        m.ready = 1;
        m.visitedBase = subruinVisitedBase(m.areaExtra, g.room);
    }
    const MinimapArea& a = subruinArea(m.areaExtra, m.area);
    minimapMarkCell(m.visitedBase, minimapPlayerCell(a));
    if (!minimapOnScreen()) return;
    MinimapTileStyle s;
    s.layer = m.layer;
    s.firstBase = subruinFirstBases[m.areaExtra];
    s.unexplored = m.areaExtra != kSubruinBrightKind ? kUnexploredDim : 0;
    s.noTile = subruinNoTile;
    minimapAreaPanel(a, s);
}

} // namespace

void registerSubruinsMinimaps(ContentRegistry& r) {
    r.roomCallbacks[RoutineId::St4FRoomCallback] = &st4fMinimap;
    r.roomCallbacks[RoutineId::St51RoomCallback] = &st51Minimap;
    r.roomCallbacks[RoutineId::St4ARoomCallback] = &subruinMinimap;
}

} // namespace od::content
