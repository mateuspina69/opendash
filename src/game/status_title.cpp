// OpenDash's "To Title" of the status sub-screen (see status_title.hpp).
#include "game/status_title.hpp"

#include <span>

#include "game/audio_logic.hpp"
#include "game/game_state.hpp"
#include "game/loaded_data.hpp"
#include "game/menu_banks.hpp"
#include "game/menu_draw.hpp"
#include "game/menu_help.hpp"
#include "game/state_array.hpp"
#include "game/status_controls.hpp"
#include "game/status_options.hpp"
#include "game/status_screen.hpp"

namespace od::port {

using namespace menu_draw;

namespace {

// the question's steps (+2 of the panel while it runs): set up, the cursor, back to the panel's rows
enum class QuestionStep : uint8_t { Enter = 0, Cursor = 1, Back = 2 };
constexpr uint8_t kAnswerYes = 0, kAnswerNo = 1; // +3: the line the cursor is on

// OpenDash's region of the panel's help bank (the panel's cursor entry points at it while the sub-screen runs): u16
// offsets by row, then the messages
constexpr uint32_t kPanelHelp = 0x7c9f2000;
constexpr uint32_t kPanelHelpBytes = 0x400;
struct PanelHelpRegion {
    StateArray<uint8_t, kPanelHelpBytes> bytes{};
};
PanelHelpRegion& panelHelpRegion() {
    static PanelHelpRegion s;
    static const bool registered = (registerBuiltData(kPanelHelp, std::span<uint8_t>(s.bytes.data(), s.bytes.size())), true);
    (void)registered;
    return s;
}

// the help of the row, the question's two lines (yes / no), in the languages (0 Japanese glyph codes, 1 English, 2
// Portuguese; UTF-8)
constexpr uint16_t kHelpJp[] = {0x8f, 0x81, 0x93, 0xa8, 0x95, 0x85, 0x83, 0xa7, 0x9e, 0x8c}; // たいとるにかえります
constexpr uint16_t kYesJp[] = {0x99, 0x81, 0};                                               // はい
constexpr uint16_t kNoJp[] = {0x81, 0x81, 0x83, 0};                                          // いいえ
const char* const kHelpLatin[3] = {"", "Return to the title screen. Unsaved progress is lost.",
                                   "Volta \xc3\xa0 tela de t\xc3\xadtulo. O progresso n\xc3\xa3o salvo \xc3\xa9 perdido."};
const char* const kYesLatin[3] = {"", "Yes", "Sim"};
const char* const kNoLatin[3] = {"", "No", "N\xc3\xa3o"};

int language() {
    const OptionsHost& host = optionsHost();
    return host.language ? host.language() : 0;
}

} // namespace

bool panelHasTitle() { return gameState().frontEnd != kFrontEndTitle && optionsHost().returnToTitle; }

int panelRowCount() { return panelHasTitle() ? 6 : 5; }

int panelExeRow(int k) {
    if (!panelHasTitle()) return k;
    if (k == kPanelTitleRow) return -1;
    return k > kPanelTitleRow ? k - 1 : k;
}

void panelTitleSetUp() {
    const uint32_t root = stateAddress(TableId::StatusControlsCursorTree); // the panel's entry: its rows, its help bank
    CursorEntry entry = dataRecord<CursorEntry>(root, CursorEntry::kBytes);
    if (panelHasTitle()) {
        const uint32_t exeRows = entry.end;
        const uint32_t exeBank = stateAddress(TableId::MenuBankPanelHelp);
        const int lang = language();
        panelHelpRegion();
        HelpWriter help(kPanelHelp, lang == 0, optionsSourceHeadersPs1());
        const uint32_t rows = exeRows + 1;
        help.at = rows * 2;
        for (uint32_t k = 0; k < rows; ++k) {
            help.setOffset(0, k, help.at);
            const int exe = panelExeRow(int(k));
            if (exe >= 0) {
                help.copy(langBank(exeBank), exe);
                continue;
            }
            help.header(exeBank, 0);
            if (lang == 0) help.glyphs(kHelpJp, uint32_t(std::size(kHelpJp)));
            else help.wrapped(kHelpLatin[lang], help.at - 1);
            help.end();
        }
        entry.end = uint8_t(rows);
        entry.link = kPanelHelp;
    }
    storeCursorEntry(root, entry);
}

void panelTitleQuestion(StatusScreen& s) {
    const int lang = language();
    const uint8_t st = s.sub;
    if (st == uint8_t(QuestionStep::Enter)) {
        s.sub = uint8_t(QuestionStep::Cursor);
        s.step = kAnswerNo;
    } else if (st == uint8_t(QuestionStep::Cursor)) {
        const uint8_t p = uint8_t(menuPadPressed());
        if (p & (kPadUp | kPadDown)) {
            soundPlayFlat(UiSound::Cursor);
            s.step ^= 1u;
        } else if (menuPadPressed() & kPadCross) {
            soundPlayFlat(UiSound::Cancel);
            s.sub = uint8_t(QuestionStep::Back);
        } else if (menuPadPressed() & kPadConfirm) {
            if (s.step == kAnswerYes && optionsHost().returnToTitle) {
                soundPlayFlat(UiSound::Confirm);
                optionsHost().returnToTitle();
            } else {
                soundPlayFlat(UiSound::Cancel);
                s.sub = uint8_t(QuestionStep::Back);
            }
        }
    }
    menuWindowOpen(0, kPanelHelp, kPanelTitleRow); // the row's help stays (the cursor's step opens it on the rows)
    if (drawing()) {
        for (int k = 0; k < 2; ++k) {
            const size_t c = otOpen(kOtOverlay);
            ScreenPrim& t = otPut(c, ScreenPrim::Text);
            t.x = t.ax = 0x50;
            t.y = int16_t(0x52 + k * 0x20);
            if (lang == 0) t.pc = k == 0 ? kYesJp : kNoJp;
            else t.utf8 = k == 0 ? kYesLatin[lang] : kNoLatin[lang];
        }
    }
    optionsOverlay(s, 2, false);
    if (s.sub == uint8_t(QuestionStep::Back)) { // the panel's rows, the cursor on this row
        s.state = uint8_t(PanelState::Rows);
        s.sub = 0;
        s.step = 0;
    }
}

} // namespace od::port
