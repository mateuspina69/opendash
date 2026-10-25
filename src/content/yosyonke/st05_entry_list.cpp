// ST05's list of collected entries: script 0x74 (exe 0x53dd20) and its list menu (block 0xa59fd0).
//
// The room case 0x57c0d0 starts it on flag 0x690 (exe 0x5112b0): a list of the entries collected (the game state's
// entry log: 0x20 bytes, 0xff = none, bit 7 = not read yet) plus "back" (0x27), in window 1 (message 0x28 of the bank
// 0x8be978) under the header window 4 (0x29). The cursor exe 0x53e330 (game/list_cursor over the block); circle on an
// entry: windows 1 / 4 closed, its text (the message id & 0x7f of the bank 0x8be978) in window 3, then its "not read"
// bit cleared and back to the list; cross or "back": the windows closed, the script ends (flag 0x690 cleared, the
// camera back to its blend mode, window 0's wait bit cleared, the player drawn). The list's drawing (exe 0x53e040: the
// rows of the bank 0x8be720 (exe 0x5578c0), the hand, the scroll bar exe 0x53e5d0) is built by entryListDraw into the
// sink of setEntryListDraw.
#include "content/yosyonke/st05_entry_list.hpp"
#include "content/yosyonke/st05_entry_list_state.hpp"

#include <cstdlib>

#include "engine/data_source.hpp"
#include "game/camera_state.hpp"
#include "game/display.hpp"
#include "game/game_state.hpp"
#include "game/list_cursor.hpp"
#include "game/menu_draw.hpp"
#include "game/menu_pointer.hpp"
#include "game/message.hpp"
#include "game/message_state.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/script_engine.hpp"
#include "game/script_state.hpp"
#include "game/story.hpp"
#include "game/story_flags.hpp"

using namespace od::port::st05_entry_list_state;

namespace od::port {

namespace {

std::vector<ScreenPrim>* gEntryDraw = nullptr; // setEntryListDraw

constexpr uint32_t kFlagEntryList = 0x690; // the room case's request (cleared as the script ends)
constexpr int16_t kListWindowMessage = 0x28, kHeaderWindowMessage = 0x29;
constexpr uint8_t kBackEntry = 0x27;       // the list's last row: "back"
constexpr uint8_t kNoEntry = 0xff;         // an empty slot of the entry log
constexpr uint8_t kEntryUnread = 0x80;     // an entry not read yet
constexpr uint8_t kEntryIdMask = 0x7f;
constexpr uint8_t kListRowsShown = 6;
constexpr int8_t kListWindow = 1, kTextWindow = 3, kHeaderWindow = 4;

// The list's block (0xa59fd0): the menu's state machine, a sub-window up (the rows dimmed), the cursor (+4 .. +9) and
// the entry chosen
EntryListBlock& listBlock() { return st05_entry_list_state::entryListBlock(); }
// the list's rows (the entries from the last collected, then "back")
const DataTable<ScalarTable<int8_t, 0x10>> entryHandBob{TableId::St05EntryListHandBob}; // the hand's bob by (frame >> 1) & 0xf

// exe 0x53e040(block): the entry list in the text entry: the drawing area, the rows (up to 6 + 1 while scrolling) from y
// 0x5c (0x52 / 0x57 while a scroll runs), each the entry's name at x 0x61, palette 2 for an entry not read yet, dimmed
// while a sub-window is up; the list's drawing area; the hand (the PC font's glyph 1) bobbing, still and dim while a
// sub-window is up; the scroll bar exe 0x53e5d0 (exe 0x547ff0's packets)
void entryListDraw(const EntryListBlock& b, std::vector<ScreenPrim>* draw) {
    using namespace menu_draw;
    begin(draw);
    const bool asking = b.asking != 0;
    uint32_t y = 0x5c;
    const int8_t sc = b.list.scrollStep;
    if (sc != 0 && std::abs(int(sc)) <= 2) {
        const int r = std::abs(int(sc)) % 3;
        y = (sc > 0) == (r == 1) ? 0x52 : 0x57;
    }
    const DrawArea area = displayDrawArea();
    otClipDrawArea(otOpen(kOtText));
    const uint32_t rows = b.list.rows < 7 ? b.list.rows : kListRowsShown;
    for (uint32_t k = 0; k <= rows; ++k, y += 0x10) {
        int32_t i = int32_t(b.list.firstRow) + int32_t(k);
        if (sc < 0) --i;
        const uint8_t e = st05EntryListGlobals().entryListRows[size_t(uint32_t(i))];
        uint32_t pal = uint32_t(e >> 6) & 2;
        if (asking) pal |= kTextPaletteDim;
        bankTextDraw(textPlace(0x61, y, pal), stateAddress(TableId::St05EntryListEntryNames), int16_t(e & kEntryIdMask), kTextNoRuby);
    }
    if (!asking) // OpenDash: the visible rows as the mouse's targets
        for (uint32_t k = 0; k < rows && k < b.list.visibleRows; ++k)
            otHitRow(otOpen(kOtText), 0x52, 0x5b + int(k) * 0x10, 0x8c, 0x10, kListCell + int(k) * 2);
    otClip(otOpen(kOtText), 0x52, area.y + 0x5b, 0x8c, int(rows << 4) - 1);
    if (b.list.row != kListNoRow) {
        const int x = asking ? 0x51 : int(entryHandBob[(gameState().frameWord() >> 1) & 0xf]) + 0x51;
        const uint32_t c = asking ? 0x606060 : kNeutral;
        otGlyph(otOpen(kOtText), x, int(b.list.row) * 0x10 + 0x5c, 1, c, 0, 0x18);
    }
    listScrollBarDraw(b.list, 0xea, 0x57);
    end();
}

// script 0x74's states (the exe's table 0x8ae578; camera script 0x8ae560 (empty), steps 0x8ae568 = 0x53de30)
const DataTable<RoutineTable<3>> entryScriptStates{TableId::St05EntryListScriptStates};
// the list menu's states (the exe's table 0x8ae584)
const DataTable<RoutineTable<2>> entryMenuStates{TableId::St05EntryListMenuStates};
enum class EntryMenuState : uint8_t { List = 0, Text = 1 };
enum class ListStep : uint8_t {
    Fill = 0,       // the windows closed, the rows filled
    OpenHeader = 1, // window 4 opens
    OpenList = 2,   // window 1 opens
    WaitList = 3,   // until window 1 waits (its text up)
    Choose = 4,     // the cursor
    Close = 6,      // until window 1 is closed: the script's wrap up
};
enum class TextStep : uint8_t { CloseText = 0, OpenText = 1, WaitText = 2 };
enum class WrapUpSub : uint8_t { CloseWindows = 0, End = 1 };

// exe 0x53e2f0: the entries (from the last) into the list's rows, "back" after them
void fillEntryRows(EntryListBlock& l) {
    uint32_t n = 0;
    for (int i = 0x1f; i >= 0; --i) {
        if (gameState().entryLog[size_t(i)] != kNoEntry) {
            st05EntryListGlobals().entryListRows[n] = gameState().entryLog[size_t(i)];
            ++n;
        }
    }
    st05EntryListGlobals().entryListRows[n] = kBackEntry;
    l.list.rows = uint8_t(n + 1u);
    l.list.visibleRows = kListRowsShown;
}
void closeEntryWindows() {
    windowClose(1, WindowClose::Gentle);
    windowClose(2, WindowClose::Gentle);
    windowClose(3, WindowClose::Gentle);
    windowClose(4, WindowClose::Gentle);
}
// exe 0x53de50: the list
void entryListMenu(EntryListBlock& l) {
    switch (ListStep(l.menuStep)) {
    case ListStep::Fill:
        closeEntryWindows();
        fillEntryRows(l);
        ++l.menuStep;
        [[fallthrough]];
    case ListStep::OpenHeader:
        if (uint8_t(windowOpen(kHeaderWindow, stateAddress(TableId::St05EntryListEntryTexts), kHeaderWindowMessage)) == 0xff) break;
        ++l.menuStep;
        [[fallthrough]];
    case ListStep::OpenList:
        if (uint8_t(windowOpen(kListWindow, stateAddress(TableId::St05EntryListEntryTexts), kListWindowMessage)) == 0xff) break;
        ++l.menuStep;
        [[fallthrough]];
    case ListStep::WaitList:
        if (windowStatus(kListWindow) & kWindowStatusWaiting) ++l.menuStep;
        break;
    case ListStep::Choose: {
        const int r = int(listCursorStep(l.list)); // exe 0x53e330 (game/list_cursor)
        const uint16_t id = st05EntryListGlobals().entryListRows[uint32_t(l.list.firstRow) + l.list.row] & kEntryIdMask;
        l.entry = id;
        if (r == 0) break;
        windowClose(kListWindow, WindowClose::Gentle);
        windowClose(kHeaderWindow, WindowClose::Gentle);
        if (id != kBackEntry && r >= 1) l.enterMenuState(uint8_t(EntryMenuState::Text));
        else l.menuStep = uint8_t(ListStep::Close);
        break;
    }
    case ListStep::Close:
        if (windowStatus(kListWindow) == 0) scriptState().enterState(CutsceneState::WrapUp);
        break;
    default: break;
    }
    if (windowStatus(kListWindow) & kWindowStatusWaiting) entryListDraw(l, gEntryDraw); // exe 0x53e040(block)
}
// exe 0x53dfb0: an entry's text in window 3, then its "not read" bit cleared
void entryTextMenu(EntryListBlock& l) {
    switch (TextStep(l.menuStep)) {
    case TextStep::CloseText:
        windowClose(kTextWindow, WindowClose::Gentle);
        ++l.menuStep;
        [[fallthrough]];
    case TextStep::OpenText:
        if (uint8_t(windowOpen(kTextWindow, stateAddress(TableId::St05EntryListEntryTexts), int16_t(l.entry))) == 0xff) return;
        ++l.menuStep;
        [[fallthrough]];
    case TextStep::WaitText:
        if (windowStatus(kTextWindow) != 0) return;
        for (size_t i = 0; i < 0x20; ++i) {
            const uint8_t e = gameState().entryLog[i];
            if (e == kNoEntry) break;
            if ((e & kEntryIdMask) == l.entry) {
                gameState().entryLog[i] = uint8_t(e & uint8_t(~kEntryUnread));
                break;
            }
        }
        l.enterMenuState(uint8_t(EntryMenuState::List));
        return;
    default: return;
    }
}
// exe 0x53dd20: script 0x74
void entryListScript() {
    ScriptState& s = scriptState();
    EntryListBlock& l = listBlock();
    switch (entryScriptStates->idAt(s.state)) {
    case RoutineId::St05EntryListEntryScriptSetUp:
        scriptSequenceStart(stateAddress(TableId::St05EntryListEntryCameraScript), stateAddress(TableId::St05EntryListEntrySteps));
        scriptReplayPadOn();
        scriptSequenceTick();
        l.clear(); // exe 0x40dc40(0xa59fd0, 0xa59fd8)
        s.enterState(CutsceneState::Steps);
        return;
    case RoutineId::St05EntryListEntryScriptMenu:
        if (s.steps->id != kScriptStepEnd) { // exe 0x53de30 (called directly): the menu's state
            switch (entryMenuStates->idAt(l.menuState)) {
            case RoutineId::St05EntryListEntryMenuList: entryListMenu(l); break;
            case RoutineId::St05EntryListEntryMenuText: entryTextMenu(l); break;
            default: notPortedAt(kStoryPort, "script 0x74 menu state (table 0x8ae584 has 2)", l.menuState);
            }
        } else {
            s.enterState(CutsceneState::WrapUp);
        }
        scriptSequenceTick();
        return;
    case RoutineId::St05EntryListEntryScriptWrapUp:
        if (WrapUpSub(s.subState) == WrapUpSub::CloseWindows) {
            closeEntryWindows();
            ++s.subState;
        } else if (WrapUpSub(s.subState) != WrapUpSub::End) {
            return;
        }
        clearStoryFlag(kFlagEntryList);
        scriptReplayPadOff();
        scriptEndBlock();
        cameraState().setMode(CameraMode::Blend);
        messageSystem().waitBits &= 0xfffffffeu;
        player().flags |= kActorDrawn;
        return;
    default: notPortedAt(kStoryPort, "script 0x74 state", s.state);
    }
}


} // namespace

bool st05EntryListScriptPorted(RoutineId handler) { return handler == RoutineId::St05EntryListScript; }

bool st05EntryListScriptTick(const PlayerEnv&, RoutineId handler) {
    switch (handler) {
    case RoutineId::St05EntryListScript: entryListScript(); return true; // script 0x74
    default: return false;
    }
}

void setEntryListDraw(std::vector<ScreenPrim>* draw) { gEntryDraw = draw; }

} // namespace od::port
