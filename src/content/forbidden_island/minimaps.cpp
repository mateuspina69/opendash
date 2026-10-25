// The minimap callbacks of the Forbidden Island stages (see minimaps.hpp).
#include "content/forbidden_island/minimaps.hpp"

#include "game/game_state.hpp"
#include "game/loaded_data.hpp"
#include "game/menu_draw.hpp"
#include "game/minimap.hpp"
#include "game/player/player_state.hpp"
#include "game/room_grid.hpp"

namespace od::content {

using namespace od::port;

namespace {

constexpr int kPictureShift = 9;

// ---- ST10 (picture = the sub-room), ST11 (2), ST1D (3, 4 past sub 0): a picture placed through the lanes; before
// story 2 a yellow dot where the icon goes later
constexpr uint32_t kSt10Pictures = 5;
const DataTable<RecordTable<MinimapPicture, kSt10Pictures, 8>> st10Pictures{TableId::St10MinimapPictures};
constexpr uint16_t kSt10Mode = 0x1c, kSt10Clut = 0x7c48;
constexpr uint8_t kSt10IconStory = 2;
constexpr uint32_t kSt10DotColour = 0x00ffff;
constexpr int16_t kSt10DotSide = 2;
constexpr uint32_t kSt10IconFlags = 0xe;

// exe 0x54c440: the picture chosen every frame
void st10Minimap() {
    if (!minimapOnScreen()) return;
    const GameState& g = gameState();
    const uint32_t picture = g.stage == 0x10 ? g.room : g.stage == 0x11 ? 2u : 3u + (g.room != 0 ? 1u : 0u);
    const MinimapState& m = minimap();
    const Player& p = player();
    const RoomGridState& grid = roomGrid();
    MinimapLanes l = minimapLanes();
    const size_t c = minimapPanelOpen(kSt10Mode);
    l.x = minimapScrollClamp(int16_t(p.pos.x.whole >> kPictureShift));
    l.y = minimapScrollClamp(int16_t(-(p.pos.z.whole >> kPictureShift)));
    const int16_t baseX = int16_t(m.slideX - l.x.get() + 0x24);
    const int16_t baseY = int16_t(0x3c - l.y.get());
    const MinimapPicture& pic = st10Pictures[picture];
    l.x = int16_t(baseX - ((grid.width >> 1) + pic.originX));
    l.y = int16_t(baseY + (grid.depth >> 1) - pic.originZ);
    minimapPictureSprite(c, l.x.get(), l.y.get(), kSt10Clut);
    if (g.story < kSt10IconStory) {
        if (picture == 0) {
            l.cornerX = int16_t(baseX + 3);
            l.cornerY = int16_t(baseY + 0x1a);
        } else {
            l.cornerX = int16_t(l.x.get() + 0x59);
            l.cornerY = int16_t(l.y.get() + 0xdb);
        }
        menu_draw::otTile(c, l.cornerX.get(), l.cornerY.get(), kSt10DotSide, kSt10DotSide, kSt10DotColour,
                          menu_draw::kTileSemi);
    } else if (picture == 0) {
        minimapIcon(c, int16_t(baseX + 5), int16_t(baseY + 0x1b), kSt10IconFlags);
    } else {
        minimapIcon(c, int16_t(l.x.get() + 0x5b), int16_t(l.y.get() + 0xdc), kSt10IconFlags);
    }
    minimapPlayerHeading(c, int16_t(baseX + (p.pos.x.whole >> kPictureShift)),
                         int16_t(baseY - (p.pos.z.whole >> kPictureShift)));
    minimapPanelClose(c);
}

// ---- the island rooms (ST3D's sub-rooms are areas 0.., ST3E's 4.., ST3F's 0xa..): the stage's sprite list round the
// panel's point (1 / 32 of the units), the area's offset into it; ST3F adds a quad
constexpr uint32_t kSt3EAreas = 12;
// an area's offset into its stage's sprites (8 bytes; the last two words are not read here)
struct St3EOffset : DataRecord {
    int16_t x = 0, z = 0;
    static St3EOffset load(const TableReader& r) { return {{}, r.s16(0), r.s16(2)}; }
};
const DataTable<RecordTable<St3EOffset, kSt3EAreas, 8>> st3eOffsets{TableId::St3EMinimapOffsets};
// the stages' sprite lists (by stage from 0x3d): {s16 dx, s16 dy, u8 u, u8 v, u8 w, u8 h} .., a dword -1 ends
const DataTable<ScalarTable<uint32_t, 3>> st3eSpriteLists{TableId::St3EMinimapSpriteLists};
constexpr uint8_t kSt3DStage = 0x3d, kSt3EStage = 0x3e, kSt3FStage = 0x3f;
constexpr uint16_t kSt3EMode = 0x9c, kSt3EClut = 0x7fc0;
constexpr int kSt3EShift = 5;
constexpr uint32_t kSt3EColour = 0xa0a0a0;
constexpr uint32_t kSpriteListEnd = 0xffffffff, kSpriteEntrySize = 8;

// exe 0x554c70(packet, list, x, y, colour): the list's semi-transparent sprites at (x, y) + their offsets (the
// offsets' halves added unsigned: an x past 0xffff carries into y)
void st3eSpriteList(size_t c, uint32_t list, int16_t x, int16_t y) {
    using namespace menu_draw;
    for (uint32_t e = list; dataScalar<uint32_t>(e) != kSpriteListEnd; e += kSpriteEntrySize) {
        const uint32_t d = dataScalar<uint32_t>(e);
        const uint32_t xy = ((d >> 16) + uint16_t(y)) << 16 | ((d & 0xffff) + uint16_t(x));
        const uint8_t w = dataScalar<uint8_t>(e + 6), h = dataScalar<uint8_t>(e + 7);
        otSprite(c, int16_t(xy), int16_t(xy >> 16),
                 spriteUv(kSt3EClut, dataScalar<uint8_t>(e + 4), dataScalar<uint8_t>(e + 5)), w, h, kSt3EColour,
                 uint8_t(kSpriteFree | 2));
    }
}

// exe 0x5545c0
void st3eMinimap() {
    if (!minimapOnScreen()) return;
    MinimapState& m = minimap();
    const GameState& g = gameState();
    if (!m.ready) {
        m.area = g.stage == kSt3DStage ? g.room : g.stage == kSt3EStage ? uint8_t(g.room + 4) : uint8_t(g.room + 0xa);
        ++m.ready;
    }
    const Player& p = player();
    const St3EOffset& offset = st3eOffsets[m.area];
    MinimapLanes l = minimapLanes();
    const size_t c = minimapPanelOpen(kSt3EMode);
    minimapPanelBackdrop(c);
    const int32_t ox = (p.pos.x.whole >> kSt3EShift) + offset.x;
    const int32_t oz = offset.z - (p.pos.z.whole >> kSt3EShift);
    l.x = minimapScrollClamp(int16_t(ox));
    l.y = minimapScrollClamp(int16_t(oz));
    const int16_t baseX = int16_t(m.slideX - l.x.get() + 0x24);
    const int16_t baseY = int16_t(0x3c - l.y.get());
    st3eSpriteList(c, st3eSpriteLists[g.stage - kSt3DStage], baseX, baseY);
    if (g.stage == kSt3FStage) {
        using namespace menu_draw;
        const int x0 = int16_t(baseX - 0x14), x1 = int16_t(baseX + 0x1c);
        const int y0 = int16_t(baseY - 0x13), y1 = int16_t(baseY + 0x1d);
        otQuad(c, {{x0, x1, x0, x1}}, {{y0, y0, y1, y1}}, kSt3EColour, uint8_t(kPolyFT4 | 2),
               {{0x20, 0x50, 0x20, 0x50}}, {{0x20, 0x20, 0x50, 0x50}}, kSt3EClut, kSt3EMode);
    }
    minimapPlayerHeading(c, int16_t(baseX + ox), int16_t(baseY + oz));
    minimapPanelClose(c);
}

} // namespace

void registerForbiddenIslandMinimaps(ContentRegistry& r) {
    r.roomCallbacks[RoutineId::St10RoomCallback] = &st10Minimap;
    r.roomCallbacks[RoutineId::IslandRoomsSt3eRoomCallback] = &st3eMinimap;
}

} // namespace od::content
