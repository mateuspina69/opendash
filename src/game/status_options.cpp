// The status sub-screen's options page (see status_options.hpp), one function per exe function; OpenDash's rows
// (the video, the language, back to the title) on the exe's.
#include "game/status_options.hpp"
#include "game/loaded_data.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <span>

#include "engine/data_source.hpp"
#include "engine/lang.hpp"
#include "engine/screen_prim.hpp"
#include "game/audio_logic.hpp"
#include "game/display.hpp"
#include "game/fade.hpp"
#include "game/fade_state.hpp"
#include "game/game_state.hpp"
#include "game/input_bindings.hpp"
#include "game/key_config.hpp"
#include "game/language_pack.hpp"
#include "game/menu_banks.hpp"
#include "game/menu_draw.hpp"
#include "game/message.hpp"
#include "game/message_state.hpp"
#include "game/player/player_input.hpp"
#include "game/player/player_state.hpp"
#include "game/status_controls.hpp"
#include "game/status_screen.hpp"
#include "game/text_script.hpp"

namespace od::port {

using namespace menu_draw;

namespace {

// +1 of the page (the handler table 0x91b7f8; OpenDash's 0xc / 0xd)
enum class OptionsState : uint8_t {
    SetUp = 0, Rows = 1, Leave = 2, Keys = 3, Camera = 4, LockOn = 5, Vibration = 6, Furigana = 7, Mono = 8, Volume = 9,
    Video = 0xa, Defaults = 0xb, Language = 0xc, ToTitle = 0xd,
};
constexpr uint8_t kLangState = uint8_t(OptionsState::Language);  // OpenDash: the language row's state
constexpr uint8_t kTitleState = uint8_t(OptionsState::ToTitle); // OpenDash: the back-to-the-title row's state
// +2 of the set-up / the leave: their steps
enum class OptionsStep : uint8_t { Prepare = 0, WaitFade = 1, Done = 2 };
// +2 of a row's own screen: set up, the cursor, back; the defaults' question (Ask), its yes (Apply)
enum class RowStep : uint8_t { Enter = 0, Cursor = 1, Back = 2, Apply = 3, Ask = 4 };

// OpenDash's region of the page's copies (the panel's entry and the page's rows pointer point at them while the
// sub-screen runs): the root and level-2 entries (+0), the captions (+0x100), the values (+0x200), the rows' help bank
// (+0x600), the key configuration's Latin banks (+0xa00 / +0xc00)
constexpr uint32_t kOpt = 0x7c9f1000;
constexpr uint32_t kOptBytes = 0x1000;
constexpr uint32_t kOptCaptions = kOpt + 0x100, kOptValues = kOpt + 0x200, kOptValueSize = 0x40, kOptHelp = kOpt + 0x600,
                   kOptLatinKeys = kOpt + 0xa00, kOptLatinLists = kOpt + 0xc00;
// the page's copies (OpenDash's region, the game's built data at kOpt): the root entry, the level-2 entries of the
// states 3..0xd, the captions' rows table, the value tables, the help banks
struct OptionsRegion {
    StateArray<uint8_t, kOptBytes> bytes{};
};
OptionsRegion& optionsRegion() {
    static OptionsRegion s;
    static const bool registered = (registerBuiltData(kOpt, std::span<uint8_t>(s.bytes.data(), s.bytes.size())), true);
    (void)registered;
    return s;
}
// the copies' writes (into the region)
void storeRowArt(uint32_t at, const RowArt& e) {
    storeData(at, e.x);
    storeData(at + 2, e.y);
    storeData(at + 4, e.uv);
    storeData(at + 6, e.clut);
    storeData(at + 8, e.w);
    storeData(at + 0xa, e.h);
}
void storeRowArtWord(uint32_t at, size_t i, uint32_t v) { storeData(at + uint32_t(i) * 4, v); }
uint32_t rowArtAt(uint32_t table, size_t i) { return table + RowTable::kRows + uint32_t(i) * RowArt::kBytes; }
// the exe's tables the copies come from: the level-2 entries, the captions' table, the value tables (moved with
// their row), the rows' states / the lock-on's and the volume's sub-rows
constexpr TableId kValueTables[9] = {
    TableId::StatusOptionsKeyConfigValues,  TableId::StatusOptionsCameraValues,    TableId::StatusOptionsLockOnFirstValues,
    TableId::StatusOptionsLockOnSecondValues, TableId::StatusOptionsVibrationValues, TableId::StatusOptionsFuriganaValues,
    TableId::StatusOptionsMonoValues,       TableId::StatusOptionsLockOnLabels,    TableId::StatusOptionsVolumeLabels};
constexpr uint8_t kValueRows[9] = {0, 1, 2, 2, 3, 4, 5, 2, 6}; // the exe row each value table belongs to
const DataTable<ScalarTable<uint8_t, 3>> lockOnRows{TableId::StatusOptionsLockOnRows};
const DataTable<ScalarTable<int8_t, 3>> lockOnModes{TableId::StatusOptionsLockOnModes};
const DataTable<ScalarTable<uint8_t, 3>> volumeRows{TableId::StatusOptionsVolumeRows};
// the value tables' indices (optionsValueTable): the key configuration, the camera, the lock-on's two buttons' modes,
// the vibration, the furigana, the mono, the lock-on's labels, the volume's labels
constexpr size_t kValueCamera = 1, kValueLockFirst = 2, kValueLockSecond = 3, kValueVibration = 4,
                 kValueFurigana = 5, kValueMono = 6, kValueLockLabels = 7, kValueVolumeLabels = 8;
// the help banks of the exe (the rows' help, the defaults' question, the volume's level help)
constexpr int16_t kVolumeLevelHelp = 2;
constexpr int kOptionsPage = 3; // the page tables' index
constexpr uint32_t kCaptionX = 0x13, kCaptionY = 0x26, kCaptionSize = 0x100030; // OpenDash's captions: 48 x 16
constexpr uint16_t kFrameBase = 0xae;                                              // the frame's height of 10 rows
// the choice tables' answers (the cursor's row -> kChoiceOff / kChoiceOn)
constexpr uint8_t kChoiceOff = 3, kChoiceOn = 4;
// the vibration's test rumble (exe 0x55cd10(0xa01, 0x41efa))
constexpr uint32_t kTestRumbleA = 0xa01, kTestRumbleB = 0x41efa;
// the fades the title's options ask for
constexpr uint8_t kOptionsFadeIn = kFadeInRequest | 2, kOptionsFadeOut = kFadeOutRequest | 2;
// the volume: 17 steps of 8 (0x7f the top)
constexpr int kVolumeStep = 8;
// the pad kinds the pad type gives (+0x2a)
constexpr uint8_t kPadTypeDigital = 0, kPadTypeAnalogA = 4, kPadTypeAnalogB = 7, kPadTypeKeep = 0xfe;

// the art: the junction's pieces, the rails, the values' cluts, the volume boxes, the overlay's corners
constexpr uint32_t kJunctionLeftArt = 0x7f122000, kJunctionMiddleArt = 0x7f122010, kJunctionRightArt = 0x7f12c000;
constexpr uint32_t kRailLeftArt = 0x7f129080, kRailRightArt = 0x7f129090;
constexpr uint32_t kVolumeBoxArt = 0x7f919000, kVolumeLevelArt = 0x7f919140;
constexpr uint32_t kVolumeSetBoxArt = 0x7f909000, kVolumeSetLevelArt = 0x7f909140;
constexpr uint32_t kVideoIconArts[3] = {0x3fdcc488, 0x3fdcc456, 0x3fdcc424};
constexpr uint32_t kCornerArts[4] = {0x7f1090c0, 0x7f1090d0, 0x7f1090e0, 0x7f1090f0};
constexpr uint16_t kOptMode = 7;
constexpr uint16_t kValueClut = 0x7f50, kLabelClut = 0x7f90, kRailClut = 0x7f12;
constexpr uint32_t kCursorBarColour = 0x101080;

// the language row's caption: tpage 7 (0xc0, 0xe0), 48 x 16 (OpenDash art, see game/menu_art); its help text
constexpr uint32_t kLangCaption = 0x7f12e0c0;

// the PC glyph codes of OpenDash's Japanese texts (font slot 0: 0x80.. hiragana)
constexpr uint16_t kLangHelpJp[] = {0xba, 0xad, 0xbb, 0xac, 0x83, 0xa6, 0xc8, 0x9e, 0x8c}; // げんごをえらびます

const char* const kLangHelpLatin[3] = {"", "Choose the game language.", "Escolha o idioma do jogo."};

// the back-to-the-title row: its caption, its help, the question's two lines (yes / no)
constexpr uint16_t kTitleCaptionJp[] = {0x8f, 0x81, 0x93, 0xa8, 0};                       // たいとる
constexpr uint16_t kTitleHelpJp[] = {0x8f, 0x81, 0x93, 0xa8, 0x95, 0x85, 0x83, 0xa7, 0x9e, 0x8c}; // たいとるにかえります
constexpr uint16_t kYesJp[] = {0x99, 0x81, 0};                                                // はい
constexpr uint16_t kNoJp[] = {0x81, 0x81, 0x83, 0};                                           // いいえ
const char* const kTitleCaptionLatin[3] = {"", "To Title", "TÃ­tulo"};
const char* const kTitleHelpLatin[3] = {"", "Return to the title screen. Unsaved progress is lost.",
                                               "Volta Ã  tela de tÃ­tulo. O progresso nÃ£o salvo Ã© perdido."};
const char* const kYesLatin[3] = {"", "Yes", "Sim"};
const char* const kNoLatin[3] = {"", "No", "NÃ£o"};

// OpenDash's texts for the rows the PS1 page lacks, in the packs' languages (EN, PT-BR)
const char* const kVideoHelpLatin[3] = {"", "Set the anti-aliasing, the screen size and the window mode.",
                                        "Ajuste o anti-aliasing, o tamanho da tela e o modo de janela."};

const char* const kKeyHelpLatin[3][3] = {
    {"", "", ""},
    {"Change the gamepad button settings.", "Change the keyboard key settings.", "Change the mouse button settings."},
    {"Altere os bot\xc3\xb5" "es do controle.", "Altere as teclas do teclado.", "Altere os bot\xc3\xb5" "es do mouse."}};

// window 2's action lists by column ('|': the right column), as the JP ones (0x8c2c30)
const char* const kKeyListsLatin[3][3][6] = {
    {{nullptr}, {nullptr}, {nullptr}},
    {{"Move|Buster", "Turn left|Special attack", "Turn right|Action, OK", "Lock-on|Change view", "Jump, cancel", nullptr},
     {"Forward|Turn left", "Back|Turn right", "Left|Special attack", "Right|Jump", "Action", nullptr},
     {"Buster|Lock-on", nullptr}},
    // (the pad's left column ends at its button boxes, x 0x82: "Girar a esquerda" is 100 px of the PS1 font)
    {{"Mover|Buster", "Girar \xc3\xa0 esq.|Ataque especial", "Girar \xc3\xa0 dir.|A\xc3\xa7\xc3\xa3o, confirmar",
      "Travar mira|Mudar vis\xc3\xa3o", "Pular, cancelar", nullptr},
     {"Frente|Girar \xc3\xa0 esquerda", "Tr\xc3\xa1s|Girar \xc3\xa0 direita", "Esquerda|Ataque especial",
      "Direita|Pular", "A\xc3\xa7\xc3\xa3o", nullptr},
     {"Buster|Travar mira", nullptr}}};

// One of the page's rows (OpenDash's layout)
struct OptRow {
    uint8_t state, exe; // exe: the row in the exe's tables (0..9), kNoExeRow = OpenDash's
};
constexpr uint8_t kNoExeRow = 0xff;
std::vector<OptRow> gRows;
uint32_t gVal[9];          // the value tables (their copies, moved with their row)
int gVolDy = 0;            // the volume row's move (its bars)
std::vector<int> gLines;   // the rows whose value is on the row (exe 0x5eeda0's underlines: exe rows 0..6)
OptionsHost gHost;
OptionsHost::Video gVideoEdit, gVideoShown; // the video row: the settings being edited, the ones applied

// a message (u16 page header, then the text, ff) of `bank` copied to the copies' offset `at`; returns the bytes written
uint32_t copyMessage(uint32_t bank, int id, uint32_t at) {
    const TextBytes from(MessageBank(bank).message(id));
    optionsRegion();
    TextBytes to(kOpt);
    uint32_t n = 0;
    while (n < 0x200 && from.mappedAt(n, 1)) {
        const uint8_t c = from.byte(n);
        to.byte(at + n) = c;
        ++n;
        if (c == 0xff) break;
        if (c == 0xfb || (c >= 0xd0 && c < 0xfb)) { // two-byte glyphs, codes: their argument bytes as they are
            const uint32_t len = c == 0xfb ? messageOpLength(TextBytes(from.at + n - 1)) : 2u;
            for (uint32_t k = 1; k < len; ++k, ++n) to.byte(at + n) = from.byte(n);
        }
    }
    return n;
}
// OpenDash: the rows of the language into the copies
void optLayout() {
    const int lang = gHost.language ? gHost.language() : 0;
    const bool jp = lang == 0;
    gRows.clear();
    for (uint8_t e = 0; e < 10; ++e) {
        if (e == 4 && !jp) continue; // ふりがな: Japanese only
        if (e == 8) gRows.push_back({kLangState, kNoExeRow});
        if (e == 9 && gameState().frontEnd != kFrontEndTitle && gHost.returnToTitle)
            gRows.push_back({kTitleState, kNoExeRow}); // in play only
        gRows.push_back({uint8_t(e < 9 ? e + 3 : 2), e});
    }
    const uint32_t n = uint32_t(gRows.size());
    optionsRegion(); // the region (built data at kOpt)
    // the root {0, 0, 0, rows, the help bank} and the level-2 entries of the states 3..0xd (0xc / 0xd as 0xa: their
    // own cursor)
    CursorEntry root;
    root.setHead(n << 24);
    root.link = kOptHelp;
    storeCursorEntry(kOpt, root);
    for (uint32_t c = 3; c <= kTitleState; ++c)
        storeCursorEntry(kOpt + 8 + (c - 3) * 8, cursorEntryAt(stateAddress(TableId::StatusOptionsLevelTwoEntries) + (c >= kLangState ? 0xa : c) * 8 - 0x10));
    CursorEntry page = pageCursorEntry(uint32_t(StatusPage::Options));
    page.link = kOpt;
    storeCursorEntry(pageCursorEntryAddress(uint32_t(StatusPage::Options)), page);
    // the captions (the page's rows table: the exe's header, OpenDash's rows) and the frame
    const RowTable captionsFrom(stateAddress(TableId::StatusOptionsCaptionRows));
    for (size_t k = 0; k < RowTable::kHeaderWords; ++k) storeData(kOptCaptions + uint32_t(k) * 4, captionsFrom.word(k));
    storeData(kOptCaptions, int16_t(n)); // the count
    for (uint32_t i = 0; i < n; ++i) {
        const uint32_t row = rowArtAt(kOptCaptions, i);
        if (gRows[i].state == kTitleState) { // no sprite: optTitleCaption writes its text
            storeData(row, int16_t(kCaptionX));
            storeRowArtWord(row, 1, 0);
            storeRowArtWord(row, 2, 0);
        } else if (gRows[i].exe == kNoExeRow) {
            storeData(row, int16_t(kCaptionX));
            storeRowArtWord(row, 1, kLangCaption);
            storeRowArtWord(row, 2, kCaptionSize);
        } else {
            storeRowArt(row, captionsFrom.row(gRows[i].exe));
        }
        storeData(row + 2, int16_t(kCaptionY + i * 0x10)); // y
    }
    StatusPageTables& pages = statusPageTables();
    pages.rows[kOptionsPage] = kOptCaptions;
    pages.frameHeights[kOptionsPage] = uint16_t(kFrameBase + (n - 10) * 0x10);
    // the value tables, moved with their row
    gLines.clear();
    for (uint32_t i = 0; i < n; ++i)
        if (gRows[i].exe <= 6) gLines.push_back(int(i));
    for (int k = 0; k < 9; ++k) {
        int row = -1;
        for (uint32_t i = 0; i < n; ++i)
            if (gRows[i].exe == kValueRows[k]) row = int(i);
        const int dy = (row - kValueRows[k]) * 0x10;
        gVal[k] = kOptValues + uint32_t(k) * kOptValueSize;
        const RowTable from(stateAddress(kValueTables[k]));
        for (size_t b = 0; b < RowTable::kCopyWords; ++b) storeData(gVal[k] + uint32_t(b) * 4, from.word(b));
        if (row < 0) storeData(gVal[k], int16_t(0)); // a hidden row: no values (the count)
        const RowTable copy(gVal[k]);
        for (int i = 0; i < copy.count && i < 3; ++i)
            storeData(rowArtAt(gVal[k], size_t(i)) + 2, int16_t(copy.row(size_t(i)).y + dy));
        if (k == 8) gVolDy = dy;
    }
    // the rows' help: the bank of the root entry -- the exe's (Japanese) or the pack's PS1 one, whose nine rows are
    // the PS1 page's (controller, view, lock-on, vibration, sound, volume, screen position, defaults, close: the PC
    // rows map onto them; the video and language rows have OpenDash's texts) -- into the copies' help bank
    const bool pack = langBank(stateAddress(TableId::MenuBankOptionsHelp)) != stateAddress(TableId::MenuBankOptionsHelp);
    static constexpr int kPs1Row[10] = {0, 1, 2, 3, -1, 4, 5, -1, 7, 8};
    const uint32_t src = langBank(stateAddress(TableId::MenuBankOptionsHelp));
    TextBytes out(kOpt);
    const uint32_t help = kOptHelp - kOpt;
    uint32_t at = help + n * 2;
    // the PS1 scripts' fb 06 cols are 2 px units (game/message): the JP header's 12 px columns x 6 for a pack's text
    auto ps1Cols = [&](uint32_t from, uint32_t to) {
        if (jp) return;
        for (uint32_t q = from; q + 7 < to; ++q)
            if (out.byte(q) == 0xfb && out.byte(q + 1) == 0x06)
                out.byte(q + 6) = uint8_t(std::min<uint32_t>(out.byte(q + 6) * 6u, 0xffu));
    };
    // OpenDash's Latin texts: PS1 glyphs broken into lines (fc) at the spaces so that each fits the window's width
    // (cols x 12 px of the JP header) with the PS1 advance (game/language_pack ps1Advance); the window's line count
    // grows to fit (the help frame is laid out from it)
    auto wrapped = [&](const char* text, uint32_t linesAt) {
        const int maxPx = int(out.byte(linesAt - 1)) * 2; // the cols byte, already in the pack's 2 px units
        const std::vector<uint8_t> g = lang::ps1Encode(text);
        int x = 0, lines = 1;
        size_t i = 0;
        while (i < g.size()) {
            size_t j = i;
            int wpx = 0;
            while (j < g.size() && g[j] != 0x4c) wpx += ps1Advance(g[j++]);
            const int sp = x > 0 ? ps1Advance(0x4c) : 0;
            if (x > 0 && x + sp + wpx > maxPx) {
                out.byte(at++) = 0xfc;
                ++lines;
                x = 0;
            } else if (x > 0) {
                out.byte(at++) = 0x4c;
                x += sp;
            }
            for (size_t k = i; k < j; ++k) out.byte(at++) = g[k];
            x += wpx;
            i = j < g.size() ? j + 1 : j;
        }
        out.byte(linesAt) = uint8_t((out.byte(linesAt) & 0xf0) | std::max(out.byte(linesAt) & 0xf, lines));
    };
    auto header = [&](uint32_t bank, int id) { // a message's page header + its fb 06 window op (10 bytes)
        const TextBytes p(MessageBank(bank).message(id));
        const uint32_t from = at;
        for (uint32_t k = 0; k < 10; ++k) out.byte(at++) = p.byte(k);
        ps1Cols(from, at);
    };
    auto setOffset = [&](uint32_t table, uint32_t i, uint32_t value) {
        storeData(kOpt + table + i * 2, uint16_t(value)); // a help bank's message offset
    };
    for (uint32_t i = 0; i < n; ++i) {
        setOffset(help, i, at - help);
        const uint8_t e = gRows[i].exe;
        if (e != kNoExeRow && (!pack || kPs1Row[e] >= 0)) {
            at += copyMessage(src, pack ? kPs1Row[e] : e, at);
            continue;
        }
        header(stateAddress(TableId::MenuBankOptionsHelp), 0);
        const bool toTitle = gRows[i].state == kTitleState;
        if (jp) {
            if (toTitle) for (const uint16_t g : kTitleHelpJp) out.byte(at++) = uint8_t(g);
            else for (const uint16_t g : kLangHelpJp) out.byte(at++) = uint8_t(g);
        } else {
            wrapped(e == 7 ? kVideoHelpLatin[lang] : toTitle ? kTitleHelpLatin[lang] : kLangHelpLatin[lang], at - 1);
        }
        out.byte(at++) = 0xff;
    }
    // the key configuration's windows in a pack's language (the PS1 has no such page): window 0 by column and window 2's
    // action lists (two columns, the right one at the JP text's cell 11 -- fb 2e 0xc0 | 11)
    setKeyConfigLatinBanks(0, 0);
    if (!jp) {
        const uint32_t keys = kOptLatinKeys - kOpt;
        at = keys + 3 * 2;
        for (int k = 0; k < 3; ++k) {
            setOffset(keys, uint32_t(k), at - keys);
            header(stateAddress(TableId::MenuKeyConfigHelpBank), k);
            wrapped(kKeyHelpLatin[lang][k], at - 1);
            out.byte(at++) = 0xff;
        }
        const uint32_t lists = kOptLatinLists - kOpt;
        at = lists + 3 * 2;
        for (int k = 0; k < 3; ++k) {
            setOffset(lists, uint32_t(k), at - lists);
            const TextBytes p(MessageBank(stateAddress(TableId::MenuKeyConfigListsBank)).message(k)); // 00 00, fb 09 00, fb 06 + 6 bytes
            const uint32_t from = at;
            for (uint32_t b = 0; b < 13; ++b) out.byte(at++) = p.byte(b);
            ps1Cols(from, at);
            for (const char* line : kKeyListsLatin[lang][k]) {
                if (!line) break;
                const std::string l = line;
                const size_t tab = l.find('|');
                for (const uint8_t g : lang::ps1Encode(l.substr(0, tab))) out.byte(at++) = g;
                if (tab != std::string::npos) {
                    out.byte(at++) = 0xfb;
                    out.byte(at++) = 0x2e;
                    out.byte(at++) = 0xc0 | 11;
                    for (const uint8_t g : lang::ps1Encode(l.substr(tab + 1))) out.byte(at++) = g;
                }
                out.byte(at++) = 0xfc;
            }
            out.byte(at - 1) = 0xff;
        }
        setKeyConfigLatinBanks(kOptLatinKeys, kOptLatinLists);
    }
}

// exe 0x5eeda0: the page's inline values -- the corner pieces, the values frame, the underlines of the rows with a
// value, the value sprites (the chosen value clut 0x7f50, the others 0x7f51; the labels 0x7f90), the volume bars
void optionsValuesDraw() {
    if (!drawing()) return;
    GameOptions& o = gameOptions();
    const uint8_t cur[7] = {o.controllerLayout, o.lookDirection, o.autoLockBuster, o.autoLockSpecial,
                            o.vibrationOff, uint8_t(1 - o.ruby), o.mono};
    size_t c = otOpen(kOtFrames);
    otDrawMode(c, kOptMode);
    artBegin(kVecOptJunction);
    otSprite(c, 0x10, 0x94, kJunctionLeftArt, 0x20, 0x10);
    otSprite(c, 0x30, 0x94, kJunctionMiddleArt, 0x10, 0x10);
    otSprite(c, 0x40, 0x94, kJunctionRightArt, 0x20, 0x10);
    artEnd();
    frameDraw(0x10, 0x1f, 0x120, 0x7d, kFrameArrow);
    c = otOpen(kOtRows);
    otDrawMode(c, kOptMode);
    for (const int r : gLines) {
        const int y = 0x2c + r * 0x10;
        labelNext(kLblOptRail);
        otQuad(c, {0x4c, 0x118, 0x4c, 0x118}, {y, y, y + 9, y + 9}, kNeutral, kPolyFT4, {0x88, 0x90, 0x88, 0x90},
               {0x90, 0x90, 0x98, 0x98}, kRailClut, kOptMode);
        labelNext(kLblNone2);
        otSprite(c, 0x44, y, kRailLeftArt, 8, 8);
        labelNext(kLblNone2);
        otSprite(c, 0x118, y, kRailRightArt, 8, 8);
    }
    for (uint32_t k = 0; k < 9; ++k) {
        const RowTable t(gVal[k]);
        for (int i = 0; i < t.count; ++i) {
            const RowArt e = t.row(size_t(i));
            const uint32_t clut = k < 7 ? kValueClut + uint32_t(i != cur[k]) : kLabelClut;
            labelNext(kLblOptValue, uint16_t(k < 7 ? (i == cur[k] ? 1 : 0) : 2), e.uv);
            otSprite(c, e.x, e.y, clut << 16 | e.uv, e.w, e.h);
        }
    }
    const int vy = 0x86 + gVolDy;
    SoundMasters& volume = soundMasters();
    labelNext(kLblVolBox);
    otSprite(c, 0x78, vy, kVolumeBoxArt, 0x40, 0x10);
    labelNext(kLblVolBox);
    otSprite(c, 0xd8, vy, kVolumeBoxArt, 0x40, 0x10);
    labelNext(kLblVolLevel);
    otSprite(c, 0x78, vy + 1, kVolumeLevelArt, ((volume.bgm + 7) >> 3) * 3 + 8, 0xe);
    labelNext(kLblVolLevel);
    otSprite(c, 0xd8, vy + 1, kVolumeLevelArt, ((volume.sfx + 7) >> 3) * 3 + 8, 0xe);
}
// exe 0x5ef210(block, x, y): the volume bar being set (clut 0x7f90 frame, the level from the cursor)
void volumeBarDraw(const StatusScreen& s, int x, int y) {
    if (!drawing()) return;
    const size_t c = otOpen(kOtRows);
    otDrawMode(c, kOptMode);
    labelNext(kLblVolBox, 1);
    otSprite(c, x, y, kVolumeSetBoxArt, 0x40, 0x10);
    labelNext(kLblVolLevel, 1);
    otSprite(c, x, y + 1, kVolumeSetLevelArt, s.cursor * 3 + 8, 0xe);
}
// exe 0x5edce0 (state 0): set up; the title fades in first
void optionsSetUp(StatusScreen& s) {
    const OptionsStep c = OptionsStep(s.sub);
    if (c == OptionsStep::Prepare) {
        cursorReset(s);
        s.testRumble = 0;
        optLayout();
        if (gameState().frontEnd == kFrontEndTitle) {
            if (gHost.fade) gHost.fade(kOptionsFadeIn, 0);
            s.sub = uint8_t(OptionsStep::WaitFade);
            return;
        }
        s.sub = uint8_t(OptionsStep::Done);
    } else if (c == OptionsStep::WaitFade) {
        if (fadeState().busy != 0) return;
        s.sub = uint8_t(OptionsStep::Done);
        return;
    } else if (c != OptionsStep::Done) {
        return;
    }
    s.switching = 0;
    s.state = uint8_t(OptionsState::Rows);
    s.sub = 0;
}
// exe 0x5edd40 (state 1): the rows; the vibration row needs a pad that vibrates (exe 0x409f80)
void optionsRows(StatusScreen& s) {
    menuCursorStep(s, 1);
    if (menuPadPressed() & kPadConfirm) {
        const int r = s.cursor;
        if (r < 0 || r >= int(gRows.size())) return;
        const uint8_t st = gRows[size_t(r)].state;
        if (st == uint8_t(OptionsState::Vibration) && !(gHost.rumble && gHost.rumble())) return;
        s.state = st;
    }
}
// exe 0x5edd90 (state 2): leave -- the panel's cursor on its "options" row (the title: a fade, then the page switch
// that closes the sub-screen)
void optionsLeave(StatusScreen& s) {
    const bool title = gameState().frontEnd == kFrontEndTitle;
    if (!title) s.sub = uint8_t(OptionsStep::Done);
    const OptionsStep c = OptionsStep(s.sub);
    if (c == OptionsStep::Prepare) {
        if (gHost.fade) gHost.fade(kOptionsFadeOut, 0);
        s.sub = uint8_t(uint8_t(c) + 1u);
    } else if (c == OptionsStep::WaitFade) {
        if (fadeState().busy == 0) s.sub = uint8_t(OptionsStep::Done);
    } else if (c == OptionsStep::Done) {
        const uint8_t page = s.page;
        s.page = uint8_t(StatusPage::Panel);
        s.state = 0;
        s.cursorKept = uint8_t(page - 3u);
        s.sub = 0;
        if (gameState().frontEnd == kFrontEndTitle) { // the title: the page switch to the closing page
            s.switching = 1;
            s.switchRow = 4;
            s.switchPage = uint8_t(StatusPage::Closing);
            s.switchState = 0;
            s.switchCountdown = 2;
        }
    }
}
// a two-choice row (states 4 / 6 / 7 / 8: exe 0x5ee070 / 0x5ee280 / 0x5ee350 / 0x5ee3f0): the option set while the
// cursor moves (the choice table's kChoiceOff / kChoiceOn), circle back to the rows; the vibration's off -> on runs a
// test rumble
void optionChoice(StatusScreen& s, uint8_t& option, bool inverted, bool vibration, uint32_t valueTable,
                  uint32_t choiceTable) {
    switch (RowStep(s.sub)) {
    case RowStep::Enter: {
        cursorReset(s);
        s.sub = uint8_t(RowStep::Cursor);
        const uint8_t v = inverted ? uint8_t(option == 0) : uint8_t(option);
        s.cursorKept = v;
        s.cursor = int8_t(v);
        break;
    }
    case RowStep::Cursor: {
        menuCursorStep(s, 2);
        const DataArray<uint8_t> choices(choiceTable);
        const uint8_t t = choices[size_t(uint32_t(int32_t(s.cursor)))];
        if (t == kChoiceOff) {
            if (vibration) { // vibration off -> on: a test rumble (exe 0x55cd10), the dual-shock flag
                if (option == 1) {
                    option = 0;
                    padVibrate(kTestRumbleA, kTestRumbleB);
                    if (s.padKind == kPadKindAnalog) s.testRumble = 1;
                }
            } else {
                option = inverted ? 1 : 0;
            }
        } else if (t == kChoiceOn) {
            option = inverted ? 0 : 1;
        }
        if (menuPadPressed() & kPadConfirm) s.sub = uint8_t(RowStep::Back);
        break;
    }
    case RowStep::Back:
        levelBack(s, 2);
        break;
    default:
        break;
    }
    rowsDraw(s, valueTable);
    s.cursor = int8_t(s.cursorKept);
}
// state 5 (exe 0x5ee110: [0x91b838 + +2 * 4], then the labels): the lock-on row -- exe 0x5ee140 picks the button (the
// special weapon's / the buster's), exe 0x5ee1a0 its mode (manual / auto)
void optionsLockOn(StatusScreen& s) {
    const uint8_t st = s.sub;
    GameOptions& o = gameOptions();
    if (st <= uint8_t(RowStep::Back)) { // exe 0x5ee140
        if (st == uint8_t(RowStep::Enter)) {
            cursorReset(s);
            s.sub = uint8_t(RowStep::Cursor);
        } else if (st == uint8_t(RowStep::Cursor)) {
            menuCursorStep(s, 2);
            if (menuPadPressed() & kPadConfirm) s.sub = lockOnRows[size_t(int32_t(s.cursor))];
        } else {
            levelBack(s, 2);
        }
    } else { // exe 0x5ee1a0
        uint8_t& option = st == kChoiceOff ? o.autoLockBuster : o.autoLockSpecial;
        const uint8_t s3 = s.step;
        if (s3 == uint8_t(RowStep::Enter)) {
            cursorReset(s);
            s.step = uint8_t(RowStep::Cursor);
            s.cursorKept = option;
            s.cursor = int8_t(option);
        } else if (s3 == uint8_t(RowStep::Cursor)) {
            menuCursorStep(s, 3);
            const int8_t t = lockOnModes[size_t(int32_t(s.cursor))];
            if (t > 2 && t < 5) option = uint8_t(t - 3);
            if (menuPadPressed() & kPadConfirm) {
                s.sub = uint8_t(RowStep::Back);
                s.step = 0;
            }
        } else if (s3 == uint8_t(RowStep::Back)) {
            levelBack(s, 3);
        }
        rowsDraw(s, st == kChoiceOff ? gVal[kValueLockFirst] : gVal[kValueLockSecond]);
        s.cursor = int8_t(s.cursorKept);
    }
    rowsDraw(s, gVal[kValueLockLabels]);
    s.cursor = int8_t(s.cursorKept);
}
// state 9 (exe 0x5ee490: [0x91b860 + +2 * 4], then the labels): the volume row -- exe 0x5ee4c0 picks the music / the
// effects, exe 0x5ee520 its level (17 steps of 8: the music's applied to the streams)
void optionsVolume(StatusScreen& s) {
    const uint8_t st = s.sub;
    SoundMasters& volume = soundMasters();
    if (st <= uint8_t(RowStep::Back)) { // exe 0x5ee4c0
        if (st == uint8_t(RowStep::Enter)) {
            cursorReset(s);
            s.sub = uint8_t(RowStep::Cursor);
        } else if (st == uint8_t(RowStep::Cursor)) {
            menuCursorStep(s, 2);
            if (menuPadPressed() & kPadConfirm) s.sub = volumeRows[size_t(int32_t(s.cursor))];
        } else {
            levelBack(s, 2);
        }
    } else { // exe 0x5ee520
        const bool music = st == kChoiceOff;
        const uint8_t s3 = s.step;
        if (s3 == uint8_t(RowStep::Enter)) {
            cursorReset(s);
            s.step = uint8_t(RowStep::Cursor);
            const int8_t v = int8_t((int16_t(music ? volume.bgm : volume.sfx) + 7) >> 3);
            s.cursorKept = uint8_t(v);
            s.cursor = v;
        } else if (s3 == uint8_t(RowStep::Cursor)) {
            menuCursorStep(s, 3);
            menuWindowOpen(0, stateAddress(TableId::MenuBankOptionsVolume), kVolumeLevelHelp);
            int v = int(int16_t(s.cursor)) << 3;
            if (v >= 0x80) v = kSoundMasterFull;
            else if (v < 0) v = 0;
            if (music) {
                volume.bgm = int8_t(v);
                soundApplyBgmMaster();
            } else {
                volume.sfx = int8_t(v);
            }
            if (menuPadPressed() & kPadConfirm) {
                s.sub = uint8_t(RowStep::Back);
                s.step = 0;
            }
        } else if (s3 == uint8_t(RowStep::Back)) {
            levelBack(s, 3);
        }
        volumeBarDraw(s, music ? 0x78 : 0xd8, 0x86 + gVolDy);
        s.cursor = int8_t(s.cursorKept);
    }
    rowsDraw(s, gVal[kValueVolumeLabels]);
    s.cursor = int8_t(s.cursorKept);
}
// the overlay of the video row (and of OpenDash's rows): the screen dimmed and lined, the box, the cursor bar on the
// line, the corner brackets (exe 0x5ee620's packets)
void overlayFrame(const StatusScreen& s, int rows, bool icons) {
    if (!drawing()) return;
    const size_t c = otOpen(kOtOverlay);
    otDrawMode(c, kOptMode);
    otTile(c, 0, 0, kScreenWidth, kScreenHeight, 0, kTileSemi);
    const DrawArea buf = displayDrawArea();
    for (int k = 0; k < 0x14; ++k) { // the grid: 0x14 vertical, 0xf horizontal lines (LINE 0x40, opaque grey)
        ScreenPrim& p = otPut(c, ScreenPrim::Quad);
        p.qx[0] = p.qx[2] = int16_t(k * 0x10);
        p.qx[1] = p.qx[3] = int16_t(k * 0x10 + 1);
        p.qy[0] = p.qy[1] = 0;
        p.qy[2] = p.qy[3] = int16_t(buf.h);
        p.colour = kNeutral;
    }
    for (int k = 0; k < 0xf; ++k) {
        ScreenPrim& p = otPut(c, ScreenPrim::Quad);
        p.qx[0] = p.qx[2] = 0;
        p.qx[1] = p.qx[3] = int16_t(buf.w);
        p.qy[0] = p.qy[1] = int16_t(k * 0x10);
        p.qy[2] = p.qy[3] = int16_t(k * 0x10 + 1);
        p.colour = kNeutral;
    }
    otTile(c, 0x28, 0x48, 0xf4, rows * 0x20, 0, kTileSemi);
    otTile(c, 0x4e, int8_t(s.step) * 0x20 + 0x50, 0xc8, 0x10, kCursorBarColour, kTileSemi);
    if (icons)
        for (int k = 0; k < 3; ++k)
            otSprite(c, 0x2c, 0x4c + k * 0x20, kVideoIconArts[k], 0x19, 0x19, kNeutral, kSpriteHalf);
    otSprite(c, 8, 8, kCornerArts[0], 0x10, 0x10);
    otSprite(c, 0x128, 8, kCornerArts[1], 0x10, 0x10);
    otSprite(c, 8, 0xd8, kCornerArts[2], 0x10, 0x10);
    otSprite(c, 0x128, 0xd8, kCornerArts[3], 0x10, 0x10);
}
// state 0xa: OpenDash's video row (the exe's place for it, its overlay and icons) -- three lines: the anti-aliasing,
// the resolution (the window's size / the exclusive full screen's mode; borderless shows the desktop's, fixed), the
// display mode (window / borderless / exclusive); left / right change the line's value, circle applies (and persists)
// what changed, cross back. The line is +3.
void optionsVideo(StatusScreen& s) {
    using DM = OptionsHost::DisplayMode;
    const uint8_t st = s.sub;
    if (st == uint8_t(RowStep::Enter)) {
        if (gHost.video) gVideoEdit = gHost.video();
        gVideoShown = gVideoEdit;
        cursorReset(s);
        s.sub = uint8_t(RowStep::Cursor);
        s.step = 0;
    } else if (st == uint8_t(RowStep::Cursor)) {
        const uint8_t p = uint8_t(menuPadPressed());
        if (p & (kPadLeft | kPadRight)) {
            soundPlayFlat(UiSound::Cursor);
            const int step = (p & kPadLeft) ? -1 : 1;
            const int8_t line = int8_t(s.step);
            if (line == 0) {
                const int n = gHost.aaCount ? gHost.aaCount() : 1;
                gVideoEdit.aa = n > 0 ? (gVideoEdit.aa + step + n) % n : 0;
            } else if (line == 1 && gVideoEdit.display != DM::Borderless && gHost.videoModes) {
                const std::vector<OptionsHost::VideoMode> modes = gHost.videoModes(gVideoEdit.display);
                OptionsHost::VideoMode& cur =
                    gVideoEdit.display == DM::Windowed ? gVideoEdit.window : gVideoEdit.exclusive;
                const int n = int(modes.size());
                if (n > 0) {
                    const auto it = std::find(modes.begin(), modes.end(), cur);
                    // a size not in the list (a window resized by hand): the step starts from the list's ends
                    const int i = it != modes.end() ? int(it - modes.begin()) : (step > 0 ? -1 : n);
                    cur = modes[size_t((i + step + n) % n)];
                }
            } else if (line == 2) {
                gVideoEdit.display = DM((int(gVideoEdit.display) + step + 3) % 3);
            }
        } else if (p & kPadUp) {
            soundPlayFlat(UiSound::Cursor);
            int8_t l = int8_t(int8_t(s.step) - 1);
            if (l < 0) l = 2;
            s.step = uint8_t(l);
        } else if (p & kPadDown) {
            soundPlayFlat(UiSound::Cursor);
            int8_t l = int8_t(int8_t(s.step) + 1);
            if (l > 2) l = 0;
            s.step = uint8_t(l);
        } else if (menuPadPressed() & kPadCross) {
            soundPlayFlat(UiSound::Cancel);
            s.sub = uint8_t(RowStep::Back);
        } else if (menuPadPressed() & kPadConfirm) {
            soundPlayFlat(UiSound::Confirm);
            if (!(gVideoEdit == gVideoShown) && gHost.videoApply) {
                gHost.videoApply(gVideoEdit);
                if (gHost.video) gVideoEdit = gHost.video(); // what the display gave (the closest mode)
                gVideoShown = gVideoEdit;
            }
        }
    } else if (st == uint8_t(RowStep::Back)) {
        levelBack(s, 2);
    }
    const bool pt = gHost.language && gHost.language() == 2;
    std::string aa = gHost.aaName ? gHost.aaName(gVideoEdit.aa) : "";
    for (char& c : aa) c = char(std::toupper(uint8_t(c)));
    while (asciiTextWidth(aa) > 0xb4 && !aa.empty()) aa.pop_back();
    OptionsHost::VideoMode res = gVideoEdit.display == DM::Windowed ? gVideoEdit.window : gVideoEdit.exclusive;
    if (gVideoEdit.display == DM::Borderless && gHost.videoModes) {
        const std::vector<OptionsHost::VideoMode> desk = gHost.videoModes(DM::Borderless);
        res = desk.empty() ? OptionsHost::VideoMode{} : desk.front();
    }
    char mode[48];
    if (res.hz > 0) std::snprintf(mode, sizeof mode, "%dx%d %dHZ", res.w, res.h, res.hz);
    else std::snprintf(mode, sizeof mode, "%dx%d", res.w, res.h);
    static constexpr const char* kDisplayEn[] = {"WINDOW", "BORDERLESS", "FULL SCREEN"};
    static constexpr const char* kDisplayPt[] = {"JANELA", "SEM BORDA", "TELA CHEIA"};
    asciiTextDraw(aa, 0x50, 0x52, true);
    asciiTextDraw(mode, 0x50, 0x72, true);
    asciiTextDraw((pt ? kDisplayPt : kDisplayEn)[int(gVideoEdit.display)], 0x50, 0x92, true);
    overlayFrame(s, 3, true);
    s.cursor = int8_t(s.cursorKept);
}
// state 0xb (exe 0x5eec80): the defaults -- the question (window 0), then (yes) the settings and the bindings back to
// their defaults
void optionsDefaults(StatusScreen& s) {
    switch (RowStep(s.sub)) {
    case RowStep::Enter:
        cursorReset(s);
        s.sub = uint8_t(RowStep::Cursor);
        break;
    case RowStep::Cursor:
        menuWindowOpen(0, stateAddress(TableId::MenuBankOptionsDefaults), 0);
        s.sub = uint8_t(RowStep::Ask);
        [[fallthrough]];
    case RowStep::Ask:
        if (windowStatus(0) == 0) {
            const uint32_t a = messageSystem().answerOf(0);
            if (a == 1) s.sub = uint8_t(RowStep::Apply);
            else if (a == 0 || a == 2) s.sub = uint8_t(RowStep::Back);
        }
        break;
    case RowStep::Apply: {
        GameOptions& o = gameOptions();
        o.controllerLayout = 0;
        o.controlOption = 0;
        o.lookDirection = 0;
        o.autoLockBuster = 0;
        o.autoLockSpecial = 0;
        o.vibrationOff = 0;
        o.ruby = 1;
        o.mono = 0;
        copyDefaultButtonMap(player()); // exe 0x55cbb0
        soundMasters().bgm = kSoundMasterFull;
        soundMasters().sfx = kSoundMasterFull;
        inputConfigDefaults();
        if (gHost.bindingsChanged) gHost.bindingsChanged();
        [[fallthrough]];
    }
    case RowStep::Back:
        levelBack(s, 2);
        break;
    default:
        break;
    }
    s.cursor = int8_t(s.cursorKept);
}
// the cursor on the row of OpenDash's state `state` (exe 0x5e96f0 put it on state - 3)
void cursorOnRow(StatusScreen& s, uint8_t state) {
    for (size_t i = 0; i < gRows.size(); ++i)
        if (gRows[i].state == state) s.cursorKept = uint8_t(i);
}
// state 0xc (OpenDash): the language row, on the video row's overlay -- one line per language (its name in its own
// script); up / down move, circle applies (and persists), cross back
void optionsLanguage(StatusScreen& s) {
    const int n = gHost.languageCount ? gHost.languageCount() : 1;
    const uint8_t st = s.sub;
    if (st == uint8_t(RowStep::Enter)) {
        cursorReset(s);
        s.sub = uint8_t(RowStep::Cursor);
        s.step = uint8_t(gHost.language ? gHost.language() : 0);
    } else if (st == uint8_t(RowStep::Cursor)) {
        const uint8_t p = uint8_t(menuPadPressed());
        if (p & kPadUp) {
            soundPlayFlat(UiSound::Cursor);
            s.step = uint8_t((int8_t(s.step) + n - 1) % n);
        } else if (p & kPadDown) {
            soundPlayFlat(UiSound::Cursor);
            s.step = uint8_t((int8_t(s.step) + 1) % n);
        } else if (menuPadPressed() & kPadCross) {
            soundPlayFlat(UiSound::Cancel);
            s.sub = uint8_t(RowStep::Back);
        } else if (menuPadPressed() & kPadConfirm) {
            soundPlayFlat(UiSound::Confirm);
            const int l = int8_t(s.step);
            if (gHost.language && l != gHost.language() && gHost.setLanguage) {
                gHost.setLanguage(l);
                optLayout(); // the rows of the new language: the cursor stays on this row
                cursorOnRow(s, kLangState);
            }
            s.sub = uint8_t(RowStep::Back);
        }
    } else if (st == uint8_t(RowStep::Back)) {
        levelBack(s, 2);
        cursorOnRow(s, kLangState);
    }
    if (drawing()) {
        static constexpr uint16_t kJp[] = {0x95, 0x9d, 0xad, 0xbb, 0}; // にほんご
        for (int k = 0; k < n && k < 3; ++k) {
            const size_t c = otOpen(kOtOverlay);
            ScreenPrim& t = otPut(c, ScreenPrim::Text);
            t.x = t.ax = 0x50;
            t.y = int16_t(0x52 + k * 0x20);
            if (k == 0) t.pc = kJp;
            else t.utf8 = k == 1 ? "English" : "Portugu\xc3\xaas";
        }
    }
    overlayFrame(s, n, false);
    s.cursor = int8_t(s.cursorKept);
}
// state 0xd (OpenDash): back to the title -- the question on the video row's overlay (yes / no, no first); up / down
// move, circle on yes runs the game's soft reset (the title: continue loads another save), on no or cross back
void optionsToTitle(StatusScreen& s) {
    const int lang = gHost.language ? gHost.language() : 0;
    const uint8_t st = s.sub;
    if (st == uint8_t(RowStep::Enter)) {
        cursorReset(s);
        s.sub = uint8_t(RowStep::Cursor);
        s.step = 1;
    } else if (st == uint8_t(RowStep::Cursor)) {
        const uint8_t p = uint8_t(menuPadPressed());
        if (p & (kPadUp | kPadDown)) {
            soundPlayFlat(UiSound::Cursor);
            s.step ^= 1u;
        } else if (menuPadPressed() & kPadCross) {
            soundPlayFlat(UiSound::Cancel);
            s.sub = uint8_t(RowStep::Back);
        } else if (menuPadPressed() & kPadConfirm) {
            if (s.step == 0 && gHost.returnToTitle) {
                soundPlayFlat(UiSound::Confirm);
                gHost.returnToTitle();
            } else {
                soundPlayFlat(UiSound::Cancel);
                s.sub = uint8_t(RowStep::Back);
            }
        }
    } else if (st == uint8_t(RowStep::Back)) {
        levelBack(s, 2);
        cursorOnRow(s, kTitleState);
    }
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
    overlayFrame(s, 2, false);
    s.cursor = int8_t(s.cursorKept);
}
// the back-to-the-title row's caption (its entry has no sprite): its text where the captions are, lit as the
// captions' cluts are (the row under the cursor or open, the others dim)
void optTitleCaption(const StatusScreen& s) {
    if (!drawing()) return;
    for (size_t i = 0; i < gRows.size(); ++i) {
        if (gRows[i].state != kTitleState) continue;
        const bool rows = int8_t(s.state) < int8_t(kFirstChildState); // the page's rows are the active level
        const bool lit = rows ? s.cursor == int(i) : s.state == kTitleState;
        const int lang = gHost.language ? gHost.language() : 0;
        const size_t c = otOpen(kOtRows);
        ScreenPrim& t = otPut(c, ScreenPrim::Text);
        t.x = t.ax = 0x16;
        t.y = int16_t(0x26 + i * 0x10 + 2);
        t.colour = lit ? kNeutral : kDim;
        if (lang == 0) t.pc = kTitleCaptionJp;
        else t.utf8 = kTitleCaptionLatin[lang];
    }
}

} // namespace

OptionsHost& optionsHost() { return gHost; }

uint32_t optionsValueTable(size_t k) { return gVal[k]; }

void optionsPage(StatusScreen& s) {
    if (gRows.empty()) optLayout();
    GameOptions& o = gameOptions();
    switch (OptionsState(s.state)) {
    case OptionsState::SetUp: optionsSetUp(s); break;
    case OptionsState::Rows: optionsRows(s); break;
    case OptionsState::Leave: optionsLeave(s); break;
    case OptionsState::Keys: keyConfigState(s); break;
    case OptionsState::Camera: optionChoice(s, o.lookDirection, false, false, gVal[kValueCamera], stateAddress(TableId::StatusOptionsCameraChoice)); break;
    case OptionsState::LockOn: optionsLockOn(s); break;
    case OptionsState::Vibration:
        optionChoice(s, o.vibrationOff, false, true, gVal[kValueVibration], stateAddress(TableId::StatusOptionsVibrationChoice));
        break;
    case OptionsState::Furigana: optionChoice(s, o.ruby, true, false, gVal[kValueFurigana], stateAddress(TableId::StatusOptionsFuriganaChoice)); break;
    case OptionsState::Mono: optionChoice(s, o.mono, false, false, gVal[kValueMono], stateAddress(TableId::StatusOptionsMonoChoice)); break;
    case OptionsState::Volume: optionsVolume(s); break;
    case OptionsState::Video: optionsVideo(s); break;
    case OptionsState::Defaults: optionsDefaults(s); break;
    case OptionsState::Language: optionsLanguage(s); break;
    case OptionsState::ToTitle: optionsToTitle(s); break;
    default: break;
    }
    switch (pad0().typeByte()) {
    case kPadTypeDigital: s.padKind = kPadKindDigital; break;
    case kPadTypeAnalogA:
    case kPadTypeAnalogB: s.padKind = kPadKindAnalog; break;
    case kPadTypeKeep: break;
    default: s.padKind = kPadKindOther; break;
    }
    optionsValuesDraw();
    pageDraw(s, kOptionsPage);
    optTitleCaption(s);
    s.cursor = int8_t(s.cursorKept);
}

} // namespace od::port
