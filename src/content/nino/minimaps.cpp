// The minimap callbacks of the Nino stages (see minimaps.hpp).
#include "content/nino/minimaps.hpp"

#include <array>
#include <span>

#include "game/game_state.hpp"
#include "game/loaded_data.hpp"
#include "game/menu_draw.hpp"
#include "game/minimap.hpp"
#include "game/nino_map_sprites.hpp"
#include "game/player/player_state.hpp"
#include "game/story_flags.hpp"

namespace od::content {

using namespace od::port;

namespace {

// ---- ST15..ST19: the area's sprites round the panel's point (1 / 64 of the units), in the area's clut ------------
// areas: ST15 0, ST16 1, ST17's sub 0 2, ST18's subs 0..2 3.., ST19's subs 0 / 1 6..; the others have no map
constexpr uint32_t kSt18Areas = 8;
constexpr uint32_t kSt18ClutStride = 4; // the areas' cluts: a u16 every 4 bytes
const DataTable<ScalarTable<uint32_t, kSt18Areas>> st18SpriteLists{TableId::St18MinimapSpriteLists};
constexpr uint16_t kSt18Mode = 0x1e;
constexpr uint8_t kSt18TownArea = 2;
constexpr uint8_t kSt18IconMovesStory = 7;
constexpr uint32_t kSt18IconFlags = 0xa;
constexpr int kSt18Shift = 6;
// exe 0x54f190
void st18Minimap() {
    if (!minimapOnScreen()) return;
    MinimapState& m = minimap();
    const GameState& g = gameState();
    if (!m.ready) {
        switch (g.stage) {
        case 0x15: m.area = 0; break;
        case 0x16: m.area = 1; break;
        case 0x17:
            if (g.room >= 1) return;
            m.area = 2;
            break;
        case 0x18:
            if (g.room >= 3) return;
            m.area = uint8_t(g.room + 3);
            break;
        case 0x19:
            if (g.room >= 2) return;
            m.area = uint8_t(g.room + 6);
            break;
        default: return;
        }
        ++m.ready;
    }
    const Player& p = player();
    MinimapLanes l = minimapLanes();
    l.clut = dataScalar<int16_t>(stateAddress(TableId::St18MinimapCluts) + m.area * kSt18ClutStride);
    if (m.area == kSt18TownArea) ninoMapTownSpritesUpdate();
    const size_t c = minimapPanelOpen(kSt18Mode);
    minimapPanelBackdrop(c);
    const int32_t ex = p.pos.x.whole >> kSt18Shift, ez = -(p.pos.z.whole >> kSt18Shift);
    l.x = minimapScrollClamp(int16_t(ex));
    l.y = minimapScrollClamp(int16_t(ez));
    const int16_t baseX = int16_t(m.slideX - l.x.get() + 0x24);
    const int16_t baseY = int16_t(0x3c - l.y.get());
    l.x = baseX;
    l.y = baseY;
    ninoMapSpriteList(c, st18SpriteLists[m.area]);
    if (m.area == kSt18TownArea) {
        ninoMapSpriteList(c, stateAddress(TableId::St18MinimapExtraList));
        if (g.story >= kSt18IconMovesStory) minimapIcon(c, int16_t(baseX - 0x13), int16_t(baseY + 0x12), kSt18IconFlags);
        else minimapIcon(c, int16_t(baseX - 0x3c), int16_t(baseY + 0x28), kSt18IconFlags);
    }
    minimapPlayerHeading(c, int16_t(baseX + ex), int16_t(baseY + ez));
    minimapPanelClose(c);
}

// ---- the ruin of ST35..ST38: ST35's sub-rooms are areas 0.. (layer 6), ST36's 6.. (0xe), ST37's 0x14.. (0x10),
// ST38's 0x24.. (0xd); the explored cells from byte 0x2f1 on
constexpr uint32_t kSt35Areas = 49;
const DataTable<ScalarTable<uint16_t, kSt35Areas>> st35VisitedBases{TableId::St35MinimapVisitedBases};
const DataTable<RecordTable<MinimapArea, kSt35Areas, 8>> st35Areas{TableId::St35MinimapAreas};
const DataTable<ScalarTable<uint32_t, kSt35Areas>> st35Dots{TableId::St35MinimapDots};
constexpr uint32_t kSt35FirstBase = 0x2f1;
constexpr uint16_t kSt35Clut = 0x7dc8;
constexpr uint32_t kUnexploredDim = 0x303030;
constexpr uint32_t kFlagSt35UnexploredShown = 0x98, kFlagSt35DotsShown = 0x99;

bool st35NoTile(uint8_t t) { return (t >= 0x70 && t <= 0x77) || t == 0xff; }

// exe 0x552c20
void st35Minimap() {
    MinimapState& m = minimap();
    const GameState& g = gameState();
    if (!m.ready) {
        switch (g.stage) {
        case 0x35: m.layer = 6, m.area = g.room; break;
        case 0x36: m.layer = 0xe, m.area = uint8_t(g.room + 6); break;
        case 0x37: m.layer = 0x10, m.area = uint8_t(g.room + 0x14); break;
        default: m.layer = 0xd, m.area = uint8_t(g.room + 0x24); break;
        }
        m.ready = 1;
        m.visitedBase = st35VisitedBases[m.area];
    }
    const MinimapArea& a = st35Areas[m.area];
    minimapMarkCell(m.visitedBase, minimapPlayerCell(a));
    if (!minimapOnScreen()) return;
    MinimapTileStyle s;
    s.layer = m.layer;
    s.firstBase = kSt35FirstBase;
    s.unexplored = storyFlag(kFlagSt35UnexploredShown) ? kUnexploredDim : 0;
    s.clut = kSt35Clut;
    s.noTile = st35NoTile;
    if (storyFlag(kFlagSt35DotsShown)) s.dots = st35Dots[m.area];
    s.dotsPaceHalved = true;
    minimapAreaPanel(a, s);
}

} // namespace

void registerNinoMinimaps(ContentRegistry& r) {
    r.roomCallbacks[RoutineId::NinoRoomsSt18RoomCallback] = &st18Minimap;
    r.roomCallbacks[RoutineId::St35RoomCallback] = &st35Minimap;
}

} // namespace od::content
