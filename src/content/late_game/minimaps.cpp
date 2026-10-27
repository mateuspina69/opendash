// The minimap callbacks of the late-game stages (see minimaps.hpp).
#include "content/late_game/minimaps.hpp"

#include "game/actor_pools.hpp"
#include "game/game_state.hpp"
#include "game/loaded_data.hpp"
#include "game/menu_draw.hpp"
#include "game/minimap.hpp"
#include "game/player/player_state.hpp"
#include "game/room_grid.hpp"
#include "game/story_flags.hpp"

namespace od::content {

using namespace od::port;

namespace {

constexpr int kPanelShift = 6, kPictureShift = 9;

// ---- ST40: a picture per sub-room (sub 1: its own, unscrolled by the grid, 1 / 64 of the units) ------------------
constexpr uint32_t kSt40Pictures = 15;
const DataTable<RecordTable<MinimapPicture, kSt40Pictures, 8>> st40Pictures{TableId::St40MinimapPictures};
constexpr uint8_t kSt40OwnPictureArea = 1;
constexpr uint16_t kSt40Mode = 0x1a, kSt40Clut = 0x7fc0;
constexpr uint16_t kSt40OwnMode = 0x1b, kSt40OwnClut = 0x7f80;
constexpr int16_t kSt40OwnPictureHalf = 0x80;

// exe 0x554d70: an area's picture from the grid's middle, no backdrop
void st40Picture() {
    const MinimapState& m = minimap();
    const Player& p = player();
    const RoomGridState& grid = roomGrid();
    MinimapLanes l = minimapLanes();
    const size_t c = minimapPanelOpen(kSt40Mode);
    l.x = minimapScrollClamp(int16_t(p.pos.x.whole >> kPictureShift));
    l.y = minimapScrollClamp(int16_t(-(p.pos.z.whole >> kPictureShift)));
    const int16_t baseX = int16_t(m.slideX - l.x.get() + 0x24);
    const int16_t baseY = int16_t(0x3c - l.y.get());
    const MinimapPicture& pic = st40Pictures[m.area];
    l.x = int16_t(baseX - (grid.width >> 1) - pic.originX);
    l.y = int16_t((grid.depth >> 1) - pic.originZ + baseY);
    minimapPictureSprite(c, l.x.get(), l.y.get(), kSt40Clut);
    minimapPlayerHeading(c, int16_t(baseX + (p.pos.x.whole >> kPictureShift)),
                         int16_t(baseY - (p.pos.z.whole >> kPictureShift)));
    minimapPanelClose(c);
}

// exe 0x554f90: sub 1's picture centred on the panel's point, over the backdrop
void st40OwnPicture() {
    const MinimapState& m = minimap();
    const Player& p = player();
    MinimapLanes l = minimapLanes();
    const size_t c = minimapPanelOpen(kSt40OwnMode);
    minimapPanelBackdrop(c);
    const int32_t dx = p.pos.x.whole >> kPanelShift, dz = -(p.pos.z.whole >> kPanelShift);
    l.x = minimapScrollClamp(int16_t(dx));
    l.y = minimapScrollClamp(int16_t(dz));
    const int16_t baseX = int16_t(m.slideX - l.x.get() + 0x24);
    const int16_t baseY = int16_t(0x3c - l.y.get());
    minimapPictureSprite(c, int16_t(baseX - kSt40OwnPictureHalf), int16_t(baseY - kSt40OwnPictureHalf), kSt40OwnClut);
    minimapPlayerHeading(c, int16_t(baseX + dx), int16_t(baseY + dz));
    minimapPanelClose(c);
}

// exe 0x554d20
void st40Minimap() {
    if (!minimapOnScreen()) return;
    MinimapState& m = minimap();
    if (!m.ready) {
        m.area = gameState().room;
        ++m.ready;
    }
    if (m.area == kSt40OwnPictureArea) st40OwnPicture();
    else st40Picture();
}

// ---- the ruin of ST43 (areas 0..), ST58 (5..), ST44 (0xa..), ST45 (0xf.., layer 6) and ST4C (0x15..): layer 5 but
// ST45's; the explored cells from byte 0x431 on, only they are drawn
constexpr uint32_t kSt43Areas = 26;
const DataTable<ScalarTable<uint16_t, kSt43Areas>> st43VisitedBases{TableId::St43MinimapVisitedBases};
const DataTable<RecordTable<MinimapArea, kSt43Areas, 8>> st43Areas{TableId::St43MinimapAreas};
constexpr uint32_t kSt43FirstBase = 0x431;
constexpr uint16_t kSt43Mode = 0xc;
constexpr uint8_t kSt43ShiftedArea = 2; // its tiles 8 px left
constexpr int16_t kSt43Shift = -8;

bool st43NoTile(uint8_t t) { return (t >= 0x70 && t <= 0x72) || t == 0xff; }

// standing on some cells marks other cells of the area
void st43ExtraMarks(uint8_t area, int32_t cell) {
    const uint32_t base = minimap().visitedBase;
    uint8_t* b = &extraFlags().bytes[base];
    auto markRange = [&](int32_t first, int32_t last) {
        for (int32_t i = first; i <= last; ++i) minimapMarkCell(base, i);
    };
    switch (area) {
    case 0:
        if (cell == 7) markRange(5, 9);
        else if (cell == 0xc) markRange(0xa, 0xe);
        else if (cell == 0x11) markRange(0xf, 0x1d);
        break;
    case 8:
    case 0xb:
    case 0x11:
        switch (cell) {
        case 0xf: b[0] |= 2, b[1] |= 1, b[2] |= 0x40, b[3] |= 0x20; break;
        case 0x10: b[0] |= 4, b[1] |= 2, b[2] |= 0x80, b[3] |= 0x40; break;
        case 0x11: b[0] |= 8, b[1] |= 4, b[3] |= 0x81; break;
        case 0x12: b[0] |= 0x10, b[1] |= 8, b[3] |= 2, b[4] |= 1; break;
        case 0x13: b[0] |= 0x20, b[1] |= 0x10, b[3] |= 4, b[4] |= 2; break;
        default: break;
        }
        break;
    case 0x16: // (no area of the tables reaches it)
        switch (cell) {
        case 9: b[1] |= 0xc; break;
        case 0xf: b[2] |= 3; break;
        case 0x13: b[0] |= 0x80, b[1] |= 0x20, b[3] |= 0x82; break;
        case 0x14: b[1] |= 0x41, b[3] |= 4, b[4] |= 1; break;
        case 0x15: b[2] |= 0xc0, b[3] |= 0x38, b[4] |= 0xe; break;
        default: break;
        }
        break;
    default: break;
    }
}

// exe 0x556360
void st43Minimap() {
    MinimapState& m = minimap();
    const GameState& g = gameState();
    if (!m.ready) {
        switch (g.stage) {
        case 0x43: m.layer = 5, m.area = g.room; break;
        case 0x44: m.layer = 5, m.area = uint8_t(g.room + 0xa); break;
        case 0x45: m.layer = 6, m.area = uint8_t(g.room + 0xf); break;
        case 0x58: m.layer = 5, m.area = uint8_t(g.room + 5); break;
        default: m.layer = 5, m.area = uint8_t(g.room + 0x15); break;
        }
        m.ready = 1;
        m.visitedBase = st43VisitedBases[m.area];
    }
    const MinimapArea& a = st43Areas[m.area];
    const int32_t cell = minimapPlayerCell(a);
    minimapMarkCell(m.visitedBase, cell);
    st43ExtraMarks(m.area, cell);
    if (!minimapOnScreen()) return;
    MinimapTileStyle s;
    s.layer = m.layer;
    s.firstBase = kSt43FirstBase;
    s.mode = kSt43Mode;
    s.noTile = st43NoTile;
    if (m.area == kSt43ShiftedArea) s.originShiftX = kSt43Shift;
    minimapAreaPanel(a, s);
}

// ---- ST59..ST5B: a fixed picture at the panel's slide (no drawing area of its own), the enemies' red dots ---------
constexpr uint32_t kSt59Areas = 3;
// an area's picture: its corner (x from the panel's slide) and the map's origin in it
struct St59Picture : DataRecord {
    int16_t x = 0, y = 0, originX = 0, originZ = 0;
    static St59Picture load(const TableReader& r) { return {{}, r.s16(0), r.s16(2), r.s16(4), r.s16(6)}; }
};
const DataTable<RecordTable<St59Picture, kSt59Areas, 8>> st59Pictures{TableId::St59MinimapPictures};
constexpr uint8_t kSt59FirstStage = 0x59;
constexpr uint16_t kSt59Mode = 0x1b, kSt59Clut = 0x3fc0;
constexpr int16_t kSt59PictureSize = 0x60;
constexpr uint8_t kSt59PictureTexel = 0x10;
constexpr uint8_t kSt59DotType = 0x62; // the statics the map shows
constexpr int16_t kSt59DotSide = 2;

// exe 0x557250(block, record, packet): a red pulsing 2 x 2 dot per live static of type 0x62
void st59Dots(size_t c, const St59Picture& pic) {
    MinimapLanes l = minimapLanes();
    const uint32_t colour = minimapPulse(uint8_t(gameState().frame) & 0x1f);
    for (uint32_t i = 0; i < StaticPool::size(); ++i) {
        const Actor& a = actorAt(kStaticPool + i * StaticPool::stride());
        if (!(a.flags & kActorLive) || a.type != kSt59DotType) continue;
        l.cornerX = int16_t((a.pos.x.whole >> kPictureShift) + pic.originX + l.x.get() - 1);
        l.cornerY = int16_t(pic.originZ - (a.pos.z.whole >> kPictureShift) + l.y.get() - 1);
        menu_draw::otTile(c, l.cornerX.get(), l.cornerY.get(), kSt59DotSide, kSt59DotSide, colour,
                          menu_draw::kTileOpaque);
    }
}

// exe 0x557110
void st59Minimap() {
    if (!minimapOnScreen()) return;
    MinimapState& m = minimap();
    if (!m.ready) {
        m.area = uint8_t(gameState().stage - kSt59FirstStage);
        ++m.ready;
    }
    using namespace menu_draw;
    const Player& p = player();
    const St59Picture& pic = st59Pictures[m.area];
    MinimapLanes l = minimapLanes();
    const size_t c = otOpen(kOtOverlay);
    otDrawMode(c, kSt59Mode);
    l.x = int16_t(pic.x + m.slideX);
    l.y = pic.y;
    otSprite(c, l.x.get(), l.y.get(), spriteUv(kSt59Clut, kSt59PictureTexel, kSt59PictureTexel), kSt59PictureSize,
             kSt59PictureSize, kNeutral, kSpriteFree);
    st59Dots(c, pic);
    minimapPlayerHeading(c, int16_t(pic.originX + (p.pos.x.whole >> kPictureShift) + l.x.get()),
                         int16_t(pic.originZ - (p.pos.z.whole >> kPictureShift) + l.y.get()));
}

} // namespace

void registerLateGameMinimaps(ContentRegistry& r) {
    r.roomCallbacks[RoutineId::St40RoomCallback] = &st40Minimap;
    r.roomCallbacks[RoutineId::St43RoomCallback] = &st43Minimap;
    r.roomCallbacks[RoutineId::St59RoomCallback] = &st59Minimap;
}

} // namespace od::content
