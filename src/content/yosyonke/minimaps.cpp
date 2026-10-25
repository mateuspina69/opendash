// The minimap callbacks of the Yosyonke stages (see minimaps.hpp).
#include "content/yosyonke/minimaps.hpp"

#include "game/game_state.hpp"
#include "game/geometry_scratch.hpp"
#include "game/loaded_data.hpp"
#include "game/minimap.hpp"
#include "game/player/player_state.hpp"
#include "game/room_grid.hpp"
#include "game/story_flags.hpp"

namespace od::content {

using namespace od::port;

namespace {

constexpr uint32_t kUnexploredDim = 0x303030;
constexpr int kPanelShift = 6, kPictureShift = 9;

// ---- the abandoned mine (ST0F): an area per sub-room, its explored cells from byte 0x1bc on ----------------------
constexpr uint32_t kSt0FRooms = 13;
const DataTable<ScalarTable<uint16_t, kSt0FRooms>> st0fVisitedBases{TableId::St0FMinimapVisitedBases};
const DataTable<RecordTable<MinimapArea, kSt0FRooms, 8>> st0fAreas{TableId::St0FMinimapAreas};
constexpr uint8_t kSt0FLayer = 0xd;
constexpr uint32_t kSt0FFirstBase = 0x1bc;
// sub 0: standing on cell 0x2f marks the three cells before it (byte 5's bits 4..6) too
constexpr int32_t kSt0FHallCell = 0x2f;
constexpr uint32_t kSt0FHallByte = 5;
constexpr uint8_t kSt0FHallBits = 0x70;
// the subs past 4 show only the explored cells
constexpr uint8_t kSt0FLastDimRoom = 4;

// exe 0x54fd10
void st0fMinimap() {
    MinimapState& m = minimap();
    const GameState& g = gameState();
    if (!m.ready) {
        m.layer = kSt0FLayer;
        m.area = g.room;
        m.ready = 1;
        m.visitedBase = st0fVisitedBases[g.room];
    }
    const MinimapArea& a = st0fAreas[m.area];
    const int32_t cell = minimapPlayerCell(a);
    minimapMarkCell(m.visitedBase, cell);
    if (g.room == 0 && cell == kSt0FHallCell) {
        uint8_t& b = extraFlags().bytes[m.visitedBase + kSt0FHallByte];
        b = uint8_t(b | kSt0FHallBits);
    }
    if (!minimapOnScreen()) return;
    MinimapTileStyle s;
    s.layer = m.layer;
    s.firstBase = kSt0FFirstBase;
    s.unexplored = g.room > kSt0FLastDimRoom ? 0 : kUnexploredDim;
    minimapAreaPanel(a, s);
}

// ---- the ruin under the town (ST0D / ST0E): a picture per sub-room, placed from the grid's middle ----------------
constexpr uint32_t kSt0DPictures = 18;
const DataTable<RecordTable<MinimapPicture, kSt0DPictures, 8>> st0dPictures{TableId::St0DMinimapPictures};
constexpr uint8_t kSt0DStage = 0xd, kSt0EStage = 0xe;
constexpr uint8_t kSt0ENoMapRoom = 2;
constexpr uint16_t kSt0DMode = 0x1b, kSt0DClut = 0x3fc0;
constexpr int32_t kSt0DCentreBias = 0x40;
constexpr uint32_t kSt0DIconFlags = 0xc; // a small opaque icon, column 0

// exe 0x54e6d0: the picture chosen every frame (ST0D: 0; else the sub-room + 1; none in ST0E's sub 2)
void st0dMinimap() {
    if (!minimapOnScreen()) return;
    const GameState& g = gameState();
    uint32_t pictureIndex;
    if (g.stage == kSt0EStage) {
        if (g.room == kSt0ENoMapRoom) return;
        pictureIndex = g.room + 1u;
    } else {
        pictureIndex = g.stage == kSt0DStage ? 0u : g.room + 1u;
    }
    const MinimapPicture& pic = st0dPictures[pictureIndex];
    const MinimapState& m = minimap();
    const Player& p = player();
    const RoomGridState& grid = roomGrid();
    MinimapLanes l = minimapLanes();
    const size_t c = minimapPanelOpen(kSt0DMode);
    // the grid's middle cell less 0x40 (pixels of 512 units)
    const int32_t cx = ((grid.lastX + grid.originX) >> 1) - kSt0DCentreBias;
    const int32_t cz = ((grid.lastZ + grid.originZ) >> 1) - kSt0DCentreBias;
    l.x = minimapScrollClamp(int16_t((p.pos.x.whole >> kPictureShift) - cx));
    l.y = minimapScrollClamp(int16_t(cz - (p.pos.z.whole >> kPictureShift)));
    const int16_t baseX = int16_t(m.slideX - l.x.get() + 0x24);
    const int16_t baseY = int16_t(0x3c - l.y.get());
    l.x = int16_t(cx - (grid.width >> 1) - pic.originX);
    l.y = int16_t((grid.depth >> 1) - pic.originZ + cz);
    l.cornerY = int16_t(l.y.get() + baseY);
    l.cornerX = int16_t(baseX + l.x.get());
    minimapPictureSprite(c, l.cornerX.get(), l.cornerY.get(), kSt0DClut);
    minimapIcon(c, int16_t(baseX + l.x.get() + 0x1f), int16_t(baseY + l.y.get() + 0x88), kSt0DIconFlags);
    minimapPlayerHeading(c, int16_t(baseX + (p.pos.x.whole >> kPictureShift) - cx),
                         int16_t(baseY - (p.pos.z.whole >> kPictureShift) + cz));
    minimapPanelClose(c);
}

// ---- the towns (ST08 / 09 / 0B / 20 / 29 / 2A / 2B / 30 / 31 / 3B / 3C): a picture per area, its texture by area;
// the ruin towns (areaExtra 1 / 2) show the entrance, those of the second kind only before story 0xd
constexpr uint32_t kTownAreas = 22;
const DataTable<RecordTable<MinimapPicture, kTownAreas, 8>> townPictures{TableId::TownMinimapPictures};
// {clut, tpage} by area
const DataTable<ScalarTable<uint32_t, kTownAreas>> townTextures{TableId::TownMinimapTextures};
constexpr uint8_t kTownLateStory = 0xd;
constexpr uint8_t kTownLateEntranceKind = 2; // the kind whose entrance goes at kTownLateStory
constexpr int32_t kTownCentreBias = 0x200;
constexpr uint8_t kTownGateArea = 0, kTownLateIconArea = 0x14;
constexpr uint32_t kTownGateIconFlags = 8, kTownLateIconFlags = 0xa;

// exe 0x54ed50 (the towns' callback, the town room functions')
void townMinimap() {
    if (!minimapOnScreen()) return;
    MinimapState& m = minimap();
    const GameState& g = gameState();
    if (!m.ready) {
        m.areaExtra = 0;
        uint8_t kind = 0;
        switch (g.stage) {
        case 0x08: m.area = 0; break;
        case 0x09: m.area = 1; break;
        case 0x0b: m.area = 2; break;
        case 0x20: m.area = 8; break;
        case 0x30: m.area = uint8_t(g.room + 3), kind = 1; break;
        case 0x31: m.area = uint8_t(g.room + 6), kind = 1; break;
        case 0x29: m.area = uint8_t(g.room + 9), kind = 2; break;
        case 0x2a: m.area = uint8_t(g.room + 0xb), kind = 2; break;
        case 0x2b: m.area = uint8_t(g.room + 0xd), kind = 2; break;
        case 0x3b: m.area = uint8_t(g.room + 0xf), kind = 2; break;
        case 0x3c: m.area = uint8_t(g.room + 0x11), kind = 2; break;
        default: return;
        }
        m.areaExtra = kind;
        if (kind == kTownLateEntranceKind && g.story >= kTownLateStory) m.areaExtra = 0;
        ++m.ready;
    }
    const Player& p = player();
    const RoomGridState& grid = roomGrid();
    MinimapLanes l = minimapLanes();
    // the area's texture word through the scratch (its clut, its tpage)
    const uint32_t texture = townTextures[m.area];
    Lane16 clut = geometryScratch().lane(0xb8), tpage = geometryScratch().lane(0xba);
    clut = int16_t(uint16_t(texture));
    tpage = int16_t(uint16_t(texture >> 16));
    const size_t c = minimapPanelOpen(uint16_t(tpage.get()));
    // the grid's middle in pixels of 64 units (even) less 0x200
    const int32_t cx = (((grid.lastX + grid.originX) << 2) & ~7) - kTownCentreBias;
    const int32_t cz = (((grid.lastZ + grid.originZ) << 2) & ~7) - kTownCentreBias;
    const int32_t px = p.pos.x.whole >> kPanelShift, pz = p.pos.z.whole >> kPanelShift;
    l.x = minimapScrollClamp(int16_t(px - cx));
    l.y = minimapScrollClamp(int16_t(cz - pz));
    const int16_t baseX = int16_t(m.slideX - l.x.get() + 0x24);
    const int16_t baseY = int16_t(0x3c - l.y.get());
    const MinimapPicture& pic = townPictures[m.area];
    l.x = int16_t(cx - (grid.width >> 1) - pic.originX + baseX);
    l.y = int16_t((grid.depth >> 1) - pic.originZ + cz + baseY);
    minimapPictureSprite(c, l.x.get(), l.y.get(), uint16_t(clut.get()));
    if (m.area == kTownGateArea)
        minimapIcon(c, int16_t(baseX - cx - 0x1d), int16_t(cz + baseY - 6), kTownGateIconFlags);
    else if (m.area == kTownLateIconArea && g.story >= kTownLateStory)
        minimapIcon(c, int16_t(baseX - cx + 9), int16_t(cz + baseY + 2), kTownLateIconFlags);
    if (m.areaExtra != 0)
        minimapEntranceMarker(c, int16_t((m.entryX >> kPanelShift) - cx + baseX),
                              int16_t(cz - (m.entryZ >> kPanelShift) + baseY));
    minimapPlayerHeading(c, int16_t(baseX - cx + px), int16_t(baseY - pz + cz));
    minimapPanelClose(c);
}

// ---- the ruin of ST2D..ST2F: ST2D's sub-rooms are areas 0.. (layer 0xa; sub 9 has no map), ST2E's 9.. (0xc),
// ST2F's 0x15.. (9); the explored cells from byte 0x12b on
constexpr uint32_t kSt2DAreas = 30;
const DataTable<ScalarTable<uint16_t, kSt2DAreas>> st2dVisitedBases{TableId::St2DMinimapVisitedBases};
const DataTable<RecordTable<MinimapArea, kSt2DAreas, 8>> st2dAreas{TableId::St2DMinimapAreas};
const DataTable<ScalarTable<uint32_t, kSt2DAreas>> st2dDots{TableId::St2DMinimapDots};
constexpr uint8_t kSt2DNoMapRoom = 9;
constexpr uint32_t kSt2DFirstBase = 0x12b;
constexpr uint32_t kFlagSt2DUnexploredShown = 0x70, kFlagSt2DDotsShown = 0x71;

bool st2dNoTile(uint8_t t) { return (t >= 0xa8 && t <= 0xaf) || t == 0xff; }

// exe 0x54d940
void st2dMinimap() {
    MinimapState& m = minimap();
    const GameState& g = gameState();
    if (g.stage == 0x2d && g.room == kSt2DNoMapRoom) return;
    if (!m.ready) {
        if (g.stage == 0x2d) m.layer = 0xa, m.area = g.room;
        else if (g.stage == 0x2e) m.layer = 0xc, m.area = uint8_t(g.room + 9);
        else m.layer = 9, m.area = uint8_t(g.room + 0x15);
        m.ready = 1;
        m.visitedBase = st2dVisitedBases[m.area];
    }
    const MinimapArea& a = st2dAreas[m.area];
    minimapMarkCell(m.visitedBase, minimapPlayerCell(a));
    if (!minimapOnScreen()) return;
    MinimapTileStyle s;
    s.layer = m.layer;
    s.firstBase = kSt2DFirstBase;
    s.unexplored = storyFlag(kFlagSt2DUnexploredShown) ? kUnexploredDim : 0;
    s.noTile = st2dNoTile;
    if (storyFlag(kFlagSt2DDotsShown)) s.dots = st2dDots[m.area];
    minimapAreaPanel(a, s);
}

} // namespace

void registerYosyonkeMinimaps(ContentRegistry& r) {
    r.roomCallbacks[RoutineId::St0FRoomCallback] = &st0fMinimap;
    r.roomCallbacks[RoutineId::St0DRoomCallback] = &st0dMinimap;
    r.roomCallbacks[RoutineId::TownRoomCallback] = &townMinimap;
    r.roomCallbacks[RoutineId::St2DRoomCallback] = &st2dMinimap;
}

} // namespace od::content
