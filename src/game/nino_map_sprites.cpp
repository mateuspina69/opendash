// The sprite lists of the Nino stages' maps (see nino_map_sprites.hpp).
#include "game/nino_map_sprites.hpp"

#include <array>
#include <span>

#include "game/loaded_data.hpp"
#include "game/menu_draw.hpp"
#include "game/minimap.hpp"
#include "game/patched_data.hpp"
#include "game/story_flags.hpp"

namespace od::port {

namespace {

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

} // namespace

void ninoMapTownSpritesUpdate() {
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

void ninoMapSpriteList(size_t c, uint32_t list, uint32_t colour, uint8_t code) {
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

void ninoMapSpriteList(size_t c, uint32_t list) {
    ninoMapSpriteList(c, list, kNinoMapSpriteColour, uint8_t(menu_draw::kSpriteFree | 2));
}

void ninoMapSpriteListDraw(size_t c, uint32_t list) {
    ninoMapSpriteList(c, list, menu_draw::kNeutral, menu_draw::kSpriteFree);
}

} // namespace od::port
