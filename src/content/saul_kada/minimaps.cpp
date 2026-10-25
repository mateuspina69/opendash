// The minimap callbacks of the Saul Kada stages (see minimaps.hpp).
#include "content/saul_kada/minimaps.hpp"

#include "game/game_state.hpp"
#include "game/loaded_data.hpp"
#include "game/minimap.hpp"
#include "game/player/player_state.hpp"
#include "game/room_grid.hpp"
#include "game/story_flags.hpp"

namespace od::content {

using namespace od::port;

namespace {

constexpr uint32_t kUnexploredDim = 0x303030;
constexpr int kPictureShift = 9;

// ---- the ruin of ST26..ST28, ST33, ST34: ST26's sub-rooms are areas 0.. (layer 7), ST27's 7.. (9), ST28's 0x10..
// (6), ST33's 0x16 (1), ST34's 0x17.. (6); the explored cells from byte 0x68 on ------------------------------------
constexpr uint32_t kSt26Areas = 29;
const DataTable<ScalarTable<uint16_t, kSt26Areas>> st26VisitedBases{TableId::St26MinimapVisitedBases};
const DataTable<RecordTable<MinimapArea, kSt26Areas, 8>> st26Areas{TableId::St26MinimapAreas};
const DataTable<ScalarTable<uint32_t, kSt26Areas>> st26Dots{TableId::St26MinimapDots};
constexpr uint32_t kSt26FirstBase = 0x68;
constexpr uint32_t kFlagSt26UnexploredShown = 0x60, kFlagSt26DotsShown = 0x61;

bool st26NoTile(uint8_t t) { return (t >= 0x8d && t <= 0x9c) || t == 0xff; }

// standing on some cells marks a cell next to them
void st26ExtraMarks(uint8_t area, int32_t cell) {
    uint8_t* b = &extraFlags().bytes[minimap().visitedBase];
    switch (area) {
    case 0xf:
        if (cell == 0x2c) b[6] |= 0x08;
        break;
    case 0x13:
        if (cell == 0xc) b[0] |= 0x10;
        else if (cell == 0x44) b[9] |= 0x10;
        break;
    case 0x15:
        if (cell == 0xd) b[1] |= 0x10;
        break;
    default: break;
    }
}

// exe 0x54caf0
void st26Minimap() {
    MinimapState& m = minimap();
    const GameState& g = gameState();
    if (!m.ready) {
        switch (g.stage) {
        case 0x26: m.layer = 7, m.area = g.room; break;
        case 0x27: m.layer = 9, m.area = uint8_t(g.room + 7); break;
        case 0x28: m.layer = 6, m.area = uint8_t(g.room + 0x10); break;
        case 0x33: m.layer = 1, m.area = uint8_t(g.room + 0x16); break;
        default: m.layer = 6, m.area = uint8_t(g.room + 0x17); break;
        }
        m.ready = 1;
        m.visitedBase = st26VisitedBases[m.area];
    }
    const MinimapArea& a = st26Areas[m.area];
    const int32_t cell = minimapPlayerCell(a);
    minimapMarkCell(m.visitedBase, cell);
    st26ExtraMarks(m.area, cell);
    if (!minimapOnScreen()) return;
    MinimapTileStyle s;
    s.layer = m.layer;
    s.firstBase = kSt26FirstBase;
    s.unexplored = storyFlag(kFlagSt26UnexploredShown) ? kUnexploredDim : 0;
    s.noTile = st26NoTile;
    if (storyFlag(kFlagSt26DotsShown)) s.dots = st26Dots[m.area];
    minimapAreaPanel(a, s);
}

// ---- ST48: a picture per sub-room, placed through the lanes; the icon moves at story 0xd ------------------------
constexpr uint32_t kSt48Pictures = 18;
const DataTable<RecordTable<MinimapPicture, kSt48Pictures, 8>> st48Pictures{TableId::St48MinimapPictures};
constexpr uint16_t kSt48Mode = 0x1b, kSt48Clut = 0x7fc0;
constexpr uint8_t kSt48IconMovesStory = 0xd;
constexpr uint32_t kSt48FirstIconFlags = 0xf, kSt48LaterIconFlags = 0xe;

// exe 0x5539e0
void st48Minimap() {
    if (!minimapOnScreen()) return;
    MinimapState& m = minimap();
    const GameState& g = gameState();
    if (!m.ready) m.ready = 1;
    const Player& p = player();
    const RoomGridState& grid = roomGrid();
    MinimapLanes l = minimapLanes();
    const size_t c = minimapPanelOpen(kSt48Mode);
    l.x = minimapScrollClamp(int16_t(p.pos.x.whole >> kPictureShift));
    l.y = minimapScrollClamp(int16_t(-(p.pos.z.whole >> kPictureShift)));
    const int16_t baseX = int16_t(m.slideX - l.x.get() + 0x24);
    const int16_t baseY = int16_t(0x3c - l.y.get());
    const MinimapPicture& pic = st48Pictures[g.room];
    l.x = int16_t(baseX - ((grid.width >> 1) + pic.originX));
    l.y = int16_t(baseY + (grid.depth >> 1) - pic.originZ);
    minimapPictureSprite(c, l.x.get(), l.y.get(), kSt48Clut);
    if (g.story < kSt48IconMovesStory) {
        if (g.room == 0) minimapIcon(c, int16_t(baseX + 4), int16_t(baseY + 5), kSt48FirstIconFlags);
        else minimapIcon(c, int16_t(l.x.get() + 0xac), int16_t(l.y.get() + 0xbe), kSt48FirstIconFlags);
    } else {
        minimapIcon(c, int16_t(l.x.get() + 0x7d), int16_t(l.y.get() + 0x63), kSt48LaterIconFlags);
    }
    minimapPlayerHeading(c, int16_t(baseX + (p.pos.x.whole >> kPictureShift)),
                         int16_t(baseY - (p.pos.z.whole >> kPictureShift)));
    minimapPanelClose(c);
}

// ---- the ruin of ST41 / ST42: ST41's sub-rooms are areas 0.. (layer 0xf), ST42's 0xf.. (0x10); the explored cells
// from byte 0x3a7 on
constexpr uint32_t kSt41Areas = 31;
const DataTable<ScalarTable<uint16_t, kSt41Areas>> st41VisitedBases{TableId::St41MinimapVisitedBases};
const DataTable<RecordTable<MinimapArea, kSt41Areas, 8>> st41Areas{TableId::St41MinimapAreas};
const DataTable<ScalarTable<uint32_t, kSt41Areas>> st41Dots{TableId::St41MinimapDots};
constexpr uint32_t kSt41FirstBase = 0x3a7;
constexpr uint32_t kFlagSt41UnexploredShown = 0x156, kFlagSt41DotsShown = 0x157;

bool st41NoTile(uint8_t t) { return t >= 0x60; }

// exe 0x555670
void st41Minimap() {
    MinimapState& m = minimap();
    const GameState& g = gameState();
    if (!m.ready) {
        if (g.stage == 0x41) m.layer = 0xf, m.area = g.room;
        else m.layer = 0x10, m.area = uint8_t(g.room + 0xf);
        m.ready = 1;
        m.visitedBase = st41VisitedBases[m.area];
    }
    const MinimapArea& a = st41Areas[m.area];
    minimapMarkCell(m.visitedBase, minimapPlayerCell(a));
    if (!minimapOnScreen()) return;
    MinimapTileStyle s;
    s.layer = m.layer;
    s.firstBase = kSt41FirstBase;
    s.unexplored = storyFlag(kFlagSt41UnexploredShown) ? kUnexploredDim : 0;
    s.noTile = st41NoTile;
    if (storyFlag(kFlagSt41DotsShown)) s.dots = st41Dots[m.area];
    minimapAreaPanel(a, s);
}

} // namespace

void registerSaulKadaMinimaps(ContentRegistry& r) {
    r.roomCallbacks[RoutineId::St26RoomCallback] = &st26Minimap;
    r.roomCallbacks[RoutineId::St48RoomCallback] = &st48Minimap;
    r.roomCallbacks[RoutineId::St41RoomCallback] = &st41Minimap;
}

} // namespace od::content
