// The minimap callbacks of the Nino stages (see minimaps.hpp).
#include "content/nino/minimaps.hpp"

#include <array>
#include <span>

#include "game/game_state.hpp"
#include "game/loaded_data.hpp"
#include "game/menu_draw.hpp"
#include "game/minimap.hpp"
#include "game/patched_data.hpp"
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
constexpr uint32_t kSt18Colour = 0xa0a0a0;
constexpr uint8_t kSt18TownArea = 2;
constexpr uint8_t kSt18IconMovesStory = 7;
constexpr uint32_t kSt18IconFlags = 0xa;
constexpr int kSt18Shift = 6;
constexpr int16_t kSpriteListEnd = 0x7fff;

// a sprite list's entry (8 bytes; a dx 0x7fff ends the list)
struct St18Sprite {
    int16_t dx = 0, dy = 0;
    uint16_t uv = 0;  // u in the low byte, v in the high one
    uint16_t size = 0;
};
constexpr uint32_t kSt18SpriteBytes = 8;
St18Sprite st18SpriteAt(uint32_t at) {
    return {dataScalar<int16_t>(at), dataScalar<int16_t>(at + 2), dataScalar<uint16_t>(at + 4),
            dataScalar<uint16_t>(at + 6)};
}

// the town area's list (15 sprites and its end), four of whose sprites the story changes in the program's data (exe
// 0x54fc00 rewrites their texels each frame)
struct St18TownList : DataRecord {
    static constexpr uint32_t kSprites = 16;
    static constexpr uint32_t kBytes = kSprites * kSt18SpriteBytes;
    std::array<St18Sprite, kSprites> sprites{};
    template <class R> static St18TownList load(const R& r) {
        St18TownList l;
        for (uint32_t i = 0; i < kSprites; ++i)
            l.sprites[i] = {r.s16(i * 8), r.s16(i * 8 + 2), r.u16(i * 8 + 4), r.u16(i * 8 + 6)};
        return l;
    }
    void save(std::span<uint8_t> out) const {
        for (uint32_t i = 0; i < kSprites; ++i) {
            storeLe(out, i * 8, sprites[i].dx);
            storeLe(out, i * 8 + 2, sprites[i].dy);
            storeLe(out, i * 8 + 4, sprites[i].uv);
            storeLe(out, i * 8 + 6, sprites[i].size);
        }
    }
};
PatchedRecord<St18TownList> st18TownList{TableId::St18MinimapPatchedList};

// exe 0x54fc00: the town list's sprites 3, 5, 6 and 8 by the story flags 0x33 / 0x32 / 0x34
void st18TownListUpdate() {
    St18TownList& l = *st18TownList;
    l.sprites[3].uv = storyFlag(0x33) ? 0x40a0 : 0x60a0;
    l.sprites[5].uv = storyFlag(0x32) ? 0xa0f0 : 0xb0f0;
    if (storyFlag(0x34)) {
        l.sprites[6].uv = 0xd0e0;
        l.sprites[8].uv = 0xd0f0;
    } else {
        l.sprites[6].uv = 0x60c0;
        l.sprites[8].uv = 0x60d0;
    }
}

// exe 0x54fc70(packet, list, colour): the list's semi-transparent sprites at the lanes' point plus their offsets
// (through the corner lanes), in the clut of the lane 0xa2
void st18SpriteList(size_t c, uint32_t list, uint32_t colour = kSt18Colour, uint8_t code = menu_draw::kSpriteFree | 2) {
    using namespace menu_draw;
    MinimapLanes l = minimapLanes();
    auto draw = [&](const St18Sprite& e) {
        l.cornerX = int16_t(e.dx + l.x.get());
        l.cornerY = int16_t(e.dy + l.y.get());
        otSprite(c, l.cornerX.get(), l.cornerY.get(),
                 spriteUv(uint16_t(l.clut.get()), uint8_t(e.uv), uint8_t(e.uv >> 8)), e.size, e.size,
                 colour, code);
    };
    if (list == stateAddress(TableId::St18MinimapPatchedList)) {
        for (const St18Sprite& e : st18TownList->sprites) {
            if (e.dx == kSpriteListEnd) break;
            draw(e);
        }
        return;
    }
    for (uint32_t at = list;; at += kSt18SpriteBytes) {
        const St18Sprite e = st18SpriteAt(at);
        if (e.dx == kSpriteListEnd) break;
        draw(e);
    }
}

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
    if (m.area == kSt18TownArea) st18TownListUpdate();
    const size_t c = minimapPanelOpen(kSt18Mode);
    minimapPanelBackdrop(c);
    const int32_t ex = p.pos.x.whole >> kSt18Shift, ez = -(p.pos.z.whole >> kSt18Shift);
    l.x = minimapScrollClamp(int16_t(ex));
    l.y = minimapScrollClamp(int16_t(ez));
    const int16_t baseX = int16_t(m.slideX - l.x.get() + 0x24);
    const int16_t baseY = int16_t(0x3c - l.y.get());
    l.x = baseX;
    l.y = baseY;
    st18SpriteList(c, st18SpriteLists[m.area]);
    if (m.area == kSt18TownArea) {
        st18SpriteList(c, stateAddress(TableId::St18MinimapExtraList));
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

void ninoMapTownSpritesUpdate() { st18TownListUpdate(); }
void ninoMapSpriteListDraw(size_t c, uint32_t list) { st18SpriteList(c, list, menu_draw::kNeutral, menu_draw::kSpriteFree); }

void registerNinoMinimaps(ContentRegistry& r) {
    r.roomCallbacks[RoutineId::NinoRoomsSt18RoomCallback] = &st18Minimap;
    r.roomCallbacks[RoutineId::St35RoomCallback] = &st35Minimap;
}

} // namespace od::content
