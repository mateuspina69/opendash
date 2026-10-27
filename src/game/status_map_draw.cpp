// The map page's drawing (see status_map_draw.hpp).
#include "game/status_map_draw.hpp"

#include <algorithm>

#include "engine/data_source.hpp"
#include "game/display.hpp"
#include "game/game_state.hpp"
#include "game/loaded_data.hpp"
#include "game/menu_banks.hpp"
#include "game/menu_draw.hpp"
#include "game/status_controls.hpp"
#include "game/status_map.hpp"
#include "game/status_screen.hpp"
#include "game/visited_rooms.hpp"

namespace od::port {

using namespace menu_draw;

namespace {

// the area names (exe table 0x8c33c0): the room key, the indices of its two name parts (the high bit omits part 2)
struct MapAreaName : DataRecord {
    static constexpr uint32_t kBytes = 4;
    uint16_t place = 0, lines = 0;
    template <class R> static MapAreaName load(const R& r) { return {{}, r.u16(0), r.u16(2)}; }
};
// the map's stage captions (exe table 0x91aca0)
const DataTable<ScalarTable<int8_t, 93>> stageNames{TableId::StatusMapStageNames};
// the four scroll arrows' three vertices (exe table 0x91ad60)
struct MapArrow : DataRecord {
    StateArray<int16_t, 3> x{}, y{};
    static MapArrow load(const TableReader& r) {
        MapArrow a;
        for (size_t i = 0; i < 3; ++i) {
            a.x[i] = r.s16(i * 4);
            a.y[i] = r.s16(i * 4 + 2);
        }
        return a;
    }
};
const DataTable<RecordTable<MapArrow, 4, 12>> browseArrows{TableId::StatusMapBrowseArrows};

// the route list's entry i as the stage / room its name belongs to (the negative entry is "cancel route")
void routeNameDraw(uint32_t packed, int i) {
    const int16_t entry = int16_t(statusList().entries[size_t(uint32_t(i))]);
    if (entry < 0) {
        statusMapAreaNameDraw(packed, 0xff, 0xff);
    } else {
        const uint32_t room = visitedRooms().roomEntry(size_t(entry));
        statusMapAreaNameDraw(packed, uint8_t(room), uint8_t((room >> 8) & 0xf));
    }
}

} // namespace

// exe 0x5e8f70: the two parts of the room's name on one line; room 0xff is the route's cancel entry
void statusMapAreaNameDraw(uint32_t packed, uint8_t stage, uint8_t room) {
    const uint32_t x = packed & 0x3ff, y = (packed >> 10) & 0x1ff, palette = (packed >> 19) & 0xf;
    if (room == 0xff) {
        bankTextDraw(textPlace(x, y, palette), menuBanks().areas, 0x1f, 0);
        return;
    }
    uint16_t first = 0, second = 0;
    bool two = true;
    for (uint32_t i = 0;; ++i) {
        const MapAreaName n = dataRecord<MapAreaName>(stateAddress(TableId::StatusScreenAreaNames) + i * 4, 4);
        if (uint8_t(n.place) == 0xff) break;
        if (n.place == (uint32_t(room) << 8 | stage)) {
            first = (n.lines >> 8) & 0x1f;
            second = n.lines & 0xff;
            two = (n.lines & 0x8000) == 0;
            break;
        }
    }
    bankTextDraw(textPlace(x, y, palette), menuBanks().areas, int16_t(first), kTextSpread);
    if (two) bankTextDraw(textPlace(x + 0x78, y, palette), menuBanks().areas, int16_t(second + 0x20), kTextSpread);
}

// exe 0x5e9080: the clipped list and its scroll animation, the cursor's preview target, the scroll bar and frame
void statusMapRouteListDraw(StatusScreen& s, int x, int y) {
    const int anim = s.scrollStep;
    int ty = y + 4;
    if (anim == 1 || anim == -2) ty -= 10;
    else if (anim == 2 || anim == -1) ty -= 5;
    const int visible = std::min(s.listRows, s.listVisible);
    const DrawArea area = displayDrawArea();
    otClipDrawArea(otOpen(kOtText));
    for (int i = 0; i <= visible; ++i, ty += 0x10) {
        const int entry = int(s.listScroll) + i - int(anim < 0);
        routeNameDraw(textPlace(uint32_t(x + 0x18), uint32_t(ty)), entry);
        if (i < visible && s.cursorShown)
            otHitRow(otOpen(kOtRows), x + 0x10, y + i * 0x10 + 3, 0xfa, 0xe, 0x100 + i * 2);
    }
    otClip(otOpen(kOtText), x + 8, area.y + y + 5, 0x10c, visible * 0x10 - 1);
    if (s.cursorShown && s.listRows != 0) {
        otTile(otOpen(kOtRows), x + 0x10, y + int(s.listRow) * 0x10 + 3, 0xfa, 0xe, barPulse(), kTileOpaque);
        const int i = int(s.listScroll) + s.listRow - int(anim < 0);
        const int16_t entry = int16_t(statusList().entries[size_t(uint32_t(i))]);
        if (entry < 0) {
            statusMap().previewRecords = 0;
        } else {
            const uint32_t room = visitedRooms().roomEntry(size_t(entry));
            statusMap().previewRecords = roomLinkRecords(uint8_t(room), (room >> 8) & 0xf);
        }
    }
    statusScrollBarDraw(s, x + 0x110, y);
    frameDraw(x, y, 0x120, 0x38, kFrameRound);
}

// exe 0x5e93e0: the stage's caption, with the floor / area name where the map has layers
void statusMapCaptionDraw() {
    const StatusMapState& m = statusMap();
    int16_t name = stageNames[gameState().stage];
    if (m.layerLabel & 0xf0) {
        bankTextDraw(0x2f820, menuBanks().areas, name, kTextSpread);
        switch (m.layerLabel & 0xf0) {
        case 0x10: name = int16_t((m.layerLabel & 0xf) + m.layer + 0x48); break;
        case 0x20: name = int16_t((m.layerLabel & 0xf) - m.layer + 0x43); break;
        case 0x30: {
            const int n = (m.layerLabel & 0xf) + m.layer;
            name = int16_t(n == 0 ? 2 : n == 1 ? 7 : 0xd);
            break;
        }
        default: break;
        }
        bankTextDraw(0x2f8a4, menuBanks().areas, int16_t(name + 0x20), kTextSpread);
    } else {
        bankTextDraw(0x2f868, menuBanks().areas, name, kTextSpread);
    }
}

// exe 0x5e94c0: the layer tabs and the scroll arrows; a held direction brightens its arrow and outline
void statusMapBrowseControlsDraw(uint32_t colour, uint32_t flags) {
    const StatusMapState& m = statusMap();
    const size_t tabs = otOpen(kOtOverlay);
    otDrawMode(tabs, 7);
    if (m.layer > 0) otSprite(tabs, 0x10c, 0x24, 0x7f104830, 0x20, 8);
    if (int(m.layer) < int(m.layerCount) - 1) otSprite(tabs, 0x58, 0x24, 0x7f104030, 0x28, 8);
    otDrawMode(tabs, 7);
    if (!flags) return;
    const uint16_t held = pad0().held;
    if (held & kPadUp) flags |= 0x10;
    if (held & kPadDown) flags |= 0x20;
    if (held & kPadLeft) flags |= 0x40;
    if (held & kPadRight) flags |= 0x80;
    const size_t arrows = otOpen(kOtOverlay);
    for (uint32_t i = 0; i < 4; ++i) {
        const uint32_t bits = flags >> i;
        if (!(bits & 1)) continue;
        const uint32_t col = bits & 0x10 ? colour | (colour >> 1) : colour;
        const MapArrow& a = browseArrows[i];
        ScreenPrim& p = otPut(arrows, ScreenPrim::Tri);
        for (size_t k = 0; k < 3; ++k) {
            p.qx[k] = a.x[k];
            p.qy[k] = a.y[k];
            p.qc[k] = col & 0xffffff;
        }
        for (size_t k = 0; k < 3; ++k) {
            ScreenPrim& line = otPut(arrows, ScreenPrim::Line);
            line.qx[0] = a.x[k];
            line.qy[0] = a.y[k];
            line.qx[1] = a.x[(k + 1) % 3];
            line.qy[1] = a.y[(k + 1) % 3];
            line.colour = ((col | 1) >> 1) & 0xffffff;
        }
    }
}

} // namespace od::port
