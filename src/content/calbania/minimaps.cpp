// The minimap callbacks of the Calbania stages (see minimaps.hpp).
#include "content/calbania/minimaps.hpp"

#include "game/game_state.hpp"
#include "game/loaded_data.hpp"
#include "game/minimap.hpp"
#include "game/player/player_state.hpp"
#include "game/room_grid.hpp"

namespace od::content {

using namespace od::port;

namespace {

constexpr int kPictureShift = 9;
constexpr uint32_t kIconFlags = 0xe; // a small semi-transparent icon, column 2

// ---- the square (ST1F, ST21..ST23): a picture per stage, its tpage by area; the picture's corner and the icon
// through the scratch's lanes
constexpr uint32_t kSquareAreas = 11;
const DataTable<RecordTable<MinimapPicture, kSquareAreas, 8>> squarePictures{TableId::CalbaniaSquareMinimapPictures};
const DataTable<ScalarTable<uint32_t, 4>> squareModes{TableId::CalbaniaSquareMinimapModes};
constexpr uint16_t kSquareClut = 0x3fc0;
// the icon's place: before story 0x11 at the panel's point in area 3, else (0xb3, 0xb7) into the picture; from it on
// at the panel's point in area 0, else (0x84, 0x4b) into the picture
constexpr uint8_t kSquareIconMovesStory = 0x11;
constexpr uint8_t kSquareFirstIconArea = 3, kSquareLaterIconArea = 0;

// exe 0x54ab10
void squareMinimap() {
    if (!minimapOnScreen()) return;
    MinimapState& m = minimap();
    const GameState& g = gameState();
    if (!m.ready) {
        switch (g.stage) {
        case 0x1f: m.area = 0; break;
        case 0x21: m.area = 1; break;
        case 0x22: m.area = 2; break;
        case 0x23: m.area = 3; break;
        default: return;
        }
        ++m.ready;
    }
    const Player& p = player();
    const RoomGridState& grid = roomGrid();
    MinimapLanes l = minimapLanes();
    const size_t c = minimapPanelOpen(uint16_t(squareModes[m.area]));
    l.x = minimapScrollClamp(int16_t(p.pos.x.whole >> kPictureShift));
    l.y = minimapScrollClamp(int16_t(-(p.pos.z.whole >> kPictureShift)));
    const int16_t baseX = int16_t(m.slideX - l.x.get() + 0x24);
    const int16_t baseY = int16_t(0x3c - l.y.get());
    const MinimapPicture& pic = squarePictures[m.area];
    l.x = int16_t(-((grid.width >> 1) + pic.originX));
    l.y = int16_t((grid.depth >> 1) - pic.originZ);
    l.cornerY = int16_t(l.y.get() + baseY);
    l.cornerX = int16_t(baseX + l.x.get());
    minimapPictureSprite(c, l.cornerX.get(), l.cornerY.get(), kSquareClut);
    int16_t x, y;
    if (g.story < kSquareIconMovesStory) {
        if (m.area == kSquareFirstIconArea) {
            x = int16_t(baseX + 1);
            y = int16_t(baseY - 8);
        } else {
            x = int16_t(baseX + l.x.get() + 0xb3);
            y = int16_t(baseY + l.y.get() + 0xb7);
        }
    } else if (m.area == kSquareLaterIconArea) {
        x = int16_t(baseX + 1);
        y = int16_t(baseY + 6);
    } else {
        x = int16_t(baseX + l.x.get() + 0x84);
        y = int16_t(baseY + l.y.get() + 0x4b);
    }
    minimapIcon(c, x, y, kIconFlags);
    minimapPlayerHeading(c, int16_t(baseX + (p.pos.x.whole >> kPictureShift)),
                         int16_t(baseY - (p.pos.z.whole >> kPictureShift)));
    minimapPanelClose(c);
}

// ---- Pokte (ST24's sub-rooms are areas 0.., ST25's 3..): a picture per area, placed through the lanes ---------
constexpr uint32_t kPokteAreas = 11;
const DataTable<RecordTable<MinimapPicture, kPokteAreas, 8>> poktePictures{TableId::PokteMinimapPictures};
constexpr uint8_t kPokteFirstStage = 0x24, kPokteSecondStageArea = 3;
constexpr uint16_t kPokteMode = 0x1c, kPokteClut = 0x7fc0;

// exe 0x550740
void pokteMinimap() {
    if (!minimapOnScreen()) return;
    MinimapState& m = minimap();
    const GameState& g = gameState();
    if (!m.ready) {
        m.area = g.stage == kPokteFirstStage ? g.room : uint8_t(g.room + kPokteSecondStageArea);
        ++m.ready;
    }
    const Player& p = player();
    const RoomGridState& grid = roomGrid();
    MinimapLanes l = minimapLanes();
    const size_t c = minimapPanelOpen(kPokteMode);
    l.x = minimapScrollClamp(int16_t(p.pos.x.whole >> kPictureShift));
    l.y = minimapScrollClamp(int16_t(-(p.pos.z.whole >> kPictureShift)));
    const int16_t baseX = int16_t(m.slideX - l.x.get() + 0x24);
    const int16_t baseY = int16_t(0x3c - l.y.get());
    const MinimapPicture& pic = poktePictures[m.area];
    l.x = int16_t(baseX - ((grid.width >> 1) + pic.originX));
    l.y = int16_t(baseY + (grid.depth >> 1) - pic.originZ);
    minimapPictureSprite(c, l.x.get(), l.y.get(), kPokteClut);
    if (m.area == 0) minimapIcon(c, int16_t(baseX + 0xd), int16_t(baseY - 9), kIconFlags);
    else minimapIcon(c, int16_t(l.x.get() + 0x95), int16_t(l.y.get() + 0x9c), kIconFlags);
    minimapPlayerHeading(c, int16_t(baseX + (p.pos.x.whole >> kPictureShift)),
                         int16_t(baseY - (p.pos.z.whole >> kPictureShift)));
    minimapPanelClose(c);
}

} // namespace

void registerCalbaniaMinimaps(ContentRegistry& r) {
    r.roomCallbacks[RoutineId::CalbaniaRoomsSquareRoomCallback] = &squareMinimap;
    r.roomCallbacks[RoutineId::CalbaniaRoomsPokteRoomCallback] = &pokteMinimap;
}

} // namespace od::content
