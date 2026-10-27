// The status sub-screen's map page (see status_map.hpp), one function per exe function.
#include "game/status_map.hpp"

#include <algorithm>
#include <cstdlib>
#include <string>

#include "engine/data_source.hpp"
#include "game/dat_names.hpp"
#include "game/menu_draw.hpp"
#include "game/message.hpp"
#include "game/stage_files.hpp"
#include "game/stage_state.hpp"
#include "game/game_state.hpp"
#include "game/status_controls.hpp"
#include "game/status_map_draw.hpp"
#include "game/status_map_stage.hpp"
#include "game/status_screen.hpp"
#include "game/visited_rooms.hpp"

namespace od::port {

using namespace menu_draw;

namespace {

const DataTable<ScalarTable<int8_t, 93>> textureFiles{TableId::StatusMapTextureFiles};
const DataTable<ScalarTable<uint8_t, 4>> rowStates{TableId::StatusMapRowStates};
const DataTable<RecordTable<VramRect, 8, 8>> clutRects{TableId::StatusMapClutRects};

// exe 0x5e8700's four VRAM moves: the map cluts kept out of the texture load's way, then put back at the close.
void mapClutsMove(bool restore) {
    for (int i = 3; i >= 0; --i) {
        const VramRect& src = clutRects[size_t(i + (restore ? 4 : 0))];
        const VramRect& dst = clutRects[size_t(i + (restore ? 0 : 4))];
        menuVramMove({src.x, src.y, src.w, src.h}, dst.x, dst.y);
    }
}
// the page's row i (the exe rewrites the map and route rows' cluts).
RowArt& mapRow(size_t i) {
    return patchedRecordFor<RowArt>(statusPageTables().rows[0] + RowTable::kRows + uint32_t(i) * RowArt::kBytes);
}

// exe 0x5e8700: the page's enter / rows / back states.
void mapRows(StatusScreen& s) {
    StatusMapState& m = statusMap();
    switch (s.state) {
    case 0: {
        s.switching = 0;
        cursorReset(s);
        mapClutsMove(false);
        const int8_t file = textureFiles[gameState().stage];
        if (file != -1) {
            datLoadRequest(datFileIndex("MAP" + std::to_string(file) + ".DAT"));
            s.unread12[0] = 0;
        } else {
            s.unread12[0] = 1;
        }
        mapMarker().shown = 1;
        m.clearPosition();
        m.previewRecords = 0;
        mapRow(0).clut = stage().secondRoomCallback == RoutineId::None ? 0x7f51 : 0x7f12;
        mapRow(1).clut = (uint8_t(mapRouteState()) & 2) ? 0x7f51 : 0x7f12;
        s.state = 1;
        break;
    }
    case 1:
        menuCursorStep(s, 1);
        if (menuPadPressed() & kPadConfirm) {
            s.state = rowStates[size_t(s.cursor)];
            if (mapRow(size_t(s.cursor)).clut == 0x7f51 && (s.state == 3 || s.state == 4)) s.state = 1;
        }
        break;
    case 2:
        if (s.unread12[0]) {
            mapClutsMove(true);
            levelBack(s, 1);
        }
        break;
    default: break;
    }
}

// exe 0x5e8870: the map browsed with the pad (cross back, triangle recentres, L1/R1 change floors).
void mapBrowse(StatusScreen& s) {
    StatusMapState& m = statusMap();
    uint32_t directions = 0;
    switch (s.sub) {
    case 0:
        cursorReset(s);
        s.sub = 1;
        break;
    case 1: {
        const uint16_t held = pad0().held, pressed = menuPadPressed();
        const int step = (held & kPadSquare) ? 8 : 2;
        if (held & kPadLeft) m.scrollX = int16_t(m.scrollX - step);
        if (held & kPadRight) m.scrollX = int16_t(m.scrollX + step);
        if (held & kPadUp) m.scrollY = int16_t(m.scrollY - step);
        if (held & kPadDown) m.scrollY = int16_t(m.scrollY + step);
        directions = statusMapClamp();
        if (pressed & kPadTriangle) {
            m.clearPosition();
        } else {
            if ((pressed & kPadR1) && m.layer != 0) --m.layer;
            if ((pressed & kPadL1) && m.layer != uint8_t(m.layerCount - 1)) ++m.layer;
        }
        if (!(pressed & kPadConfirm) && (pressed & kPadCross)) s.sub = 2;
        break;
    }
    case 2: levelBack(s, 2); break;
    default: break;
    }
    statusMapBrowseControlsDraw(0x80, directions);
    s.cursor = int8_t(s.cursorKept);
    statusMapCaptionDraw();
}

// exe 0x5e8e80: the visited, routable rooms other than the current room, plus "cancel route" when one is set.
void mapRouteListBuild(StatusScreen& s) {
    StatusList& list = statusList();
    list.clearWords();
    uint32_t n = 0;
    if (mapRouteState() == MapRouteState::Set) list.entries[n++] = 0xffff;
    const VisitedRoomBlock& b = visitedRooms();
    if (b.rooms.value() != 0) {
        const GameState& g = gameState();
        for (uint32_t i = 0;; ++i) {
            const uint32_t entry = b.roomEntry(i);
            if ((entry & 0xffff) == kRoomListEnd) break;
            const uint8_t stage = uint8_t(entry), room = uint8_t((entry >> 8) & 0xf);
            if ((entry & 0xf000) == 0 && (stage != g.stage || room != g.room) && mapRoomVisited(stage, room))
                list.entries[n++] = uint16_t(i);
        }
    }
    s.listRows = uint8_t(n);
    s.listVisible = 3;
}

// exe 0x5e89a0: the destination list (the map's viewport made shorter while the list is open).
void mapRoute(StatusScreen& s) {
    StatusMapState& m = statusMap();
    switch (s.sub) {
    case 0:
        m.previewRecords = 0;
        m.x = 0x58;
        m.y = 0x24;
        m.w = 0xd4;
        m.h = 0x50;
        mapRouteListBuild(s);
        cursorReset(s);
        s.sub = 1;
        break;
    case 1: {
        const int answer = oneColumnListStep(s);
        if (answer == kAnswerConfirm) s.sub = 3;
        else if (answer == kAnswerCancel) s.sub = 2;
        break;
    }
    case 3: {
        s.cursorShown = 0;
        const int16_t entry = int16_t(statusList().entries[size_t(s.listScroll) + s.listRow]);
        if (entry < 0) mapRouteReset();
        else {
            const uint32_t room = visitedRooms().roomEntry(size_t(entry));
            mapRouteTo(uint8_t(room), (room >> 8) & 0xf);
        }
        s.sub = 2;
        break;
    }
    default: break;
    }
    statusMapRouteListDraw(s, 0x10, 0x7b);
    s.cursor = int8_t(s.cursorKept);
    if (s.sub == 2) {
        m.previewRecords = 0;
        m.x = 0x58;
        m.y = 0x24;
        m.w = 0xd4;
        m.h = 0x8c;
        levelBack(s, 2);
    }
    statusMapCaptionDraw();
}

// exe 0x5e8ab0: the minimap on/off choice, using the message window's choice and answer nibble.
void mapDisplay(StatusScreen& s) {
    StatusMapState& m = statusMap();
    const uint32_t bank = stateAddress(TableId::MenuBankMapDisplay);
    switch (s.sub) {
    case 0:
        cursorReset(s);
        m.minimapChoice = uint8_t(gameOptions().minimapShown == 0);
        s.cursorKept = m.minimapChoice;
        s.cursor = int8_t(m.minimapChoice);
        s.sub = 1;
        break;
    case 1:
        menuWindowOpen(2, bank, 2);
        messageWindow(2).flags = (messageWindow(2).flags & ~(0xfu << kWindowChoiceShift)) |
                          uint32_t(m.minimapChoice) << kWindowChoiceShift;
        s.sub = 3;
        break;
    case 2: levelBack(s, 2); break;
    case 3:
        if (windowStatus(2) == 0) {
            const uint8_t answer = messageSystem().answerOf(2);
            if (answer == 0) {
                gameOptions().minimapShown = 1;
                m.minimapChoice = 0;
            } else if (answer == 1) {
                gameOptions().minimapShown = 0;
                m.minimapChoice = 1;
            }
            s.sub = 2;
        } else {
            const uint8_t choice = messageWindow(2).choice();
            if (choice <= 1) m.minimapChoice = choice;
        }
        break;
    default: break;
    }
    menuWindowOpen(0, bank, m.minimapChoice);
    s.cursor = int8_t(s.cursorKept);
}

// exe 0x5e8be0: the stage's callback when loaded, or the exe's "no map" / loading picture.
void mapDraw(StatusScreen& s) {
    const StatusMapState& m = statusMap();
    if (s.unread12[0] && stage().secondRoomCallback != RoutineId::None) {
        statusMapStageDraw();
    } else {
        const size_t c = otOpen(kOtText);
        otDrawMode(c, 7);
        otTile(c, m.x, m.y, m.w, m.h, 0x202020, kTileSemi);
        const bool ready = s.unread12[0] != 0;
        otSprite(c, m.x + (m.w >> 1) - (ready ? 0xf : 0x20), m.y + (m.h >> 1) - 4,
                 ready ? 0x7f1170c0 : 0x7f1168c0, ready ? 0x1e : 0x40, 8);
    }
    if (s.unread12[0] && (uint8_t(mapRouteState()) & 2)) {
        const size_t c = otOpen(kOtText);
        otDrawMode(c, 7);
        otTile(c, m.x, m.y, m.w, m.h, 0x202020, kTileSemi);
    }
}

} // namespace

uint32_t statusMapClamp() {
    StatusMapState& m = statusMap();
    const int midX = (int(m.minX) + m.maxX) >> 1, midY = (int(m.minY) + m.maxY) >> 1;
    const int dx = std::max(0, (std::abs(int(m.maxX) - m.minX) - m.w) >> 1);
    const int dy = std::max(0, (std::abs(int(m.maxY) - m.minY) - m.h) >> 1);
    const int loX = midX - dx, hiX = midX + dx, loY = midY - dy, hiY = midY + dy;
    uint32_t flags = 0xf;
    if (loX == hiX) {
        m.scrollX = int16_t(loX);
        flags = 3;
    } else {
        if (m.scrollX <= loX) {
            m.scrollX = int16_t(loX);
            flags = 0xb;
        }
        if (m.scrollX >= hiX) {
            m.scrollX = int16_t(hiX);
            flags &= ~8u;
        }
    }
    if (loY == hiY) {
        m.scrollY = int16_t(loY);
        flags &= ~3u;
    } else {
        if (m.scrollY <= loY) {
            m.scrollY = int16_t(loY);
            flags &= ~1u;
        }
        if (m.scrollY >= hiY) {
            m.scrollY = int16_t(hiY);
            flags &= ~2u;
        }
    }
    return flags;
}

void mapPage(StatusScreen& s) {
    switch (s.state) {
    case 0:
    case 1:
    case 2: mapRows(s); break;
    case 3: mapBrowse(s); break;
    case 4: mapRoute(s); break;
    case 5: mapDisplay(s); break;
    default: throw DataSourceError("status map: invalid state");
    }
    pageDraw(s, 0);
    if (s.unread12[0] == 0 && !(fileLoads().loading & kFileLoading)) s.unread12[0] = 1;
    if (s.state != 0 && s.state != 2) mapDraw(s);
    s.cursor = int8_t(s.cursorKept);
}

} // namespace od::port
