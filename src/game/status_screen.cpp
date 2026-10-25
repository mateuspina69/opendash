// The status sub-screen (see status_screen.hpp): the game mode 5, the sub-screen's frame, the panel (page 1), the
// closing (page 2), the page DATs, the page switch and the highlight clut's pulse; one function per exe function.
#include "game/loaded_data.hpp"
#include "game/state_array.hpp"
#include "game/status_screen.hpp"
#include "game/status_screen_state.hpp"

#include <algorithm>
#include <cstdio>

#include "engine/data_source.hpp"
#include "engine/screen_prim.hpp"
#include "game/actors.hpp"
#include "game/audio_logic.hpp"
#include "game/camera_state.hpp"
#include "game/display.hpp"
#include "game/game_state.hpp"
#include "game/input_bindings.hpp"
#include "game/items.hpp"
#include "game/menu_banks.hpp"
#include "game/menu_draw.hpp"
#include "game/message.hpp"
#include "game/message_state.hpp"
#include "game/player/player_input.hpp"
#include "game/player/player_state.hpp"
#include "game/player/player_weapons.hpp"
#include "game/stage_files.hpp"
#include "game/stage_state.hpp"
#include "game/status_controls.hpp"
#include "game/status_equipment.hpp"
#include "game/status_items.hpp"
#include "game/status_options.hpp"
#include "game/transition.hpp"

using namespace od::port::status_screen_state;

namespace od::port {

using namespace menu_draw;

namespace {

MenuVramIo gVram;
std::function<void()> gPlayerFiles;
// the equipment page's model bank swapped in (pageBankIn) and the field's kept meanwhile
uint32_t gFieldBank = 0;
bool gPageBank = false;

// the page DATs' files: kPageDatFile + k (1 SUBSCN01 map, 2 SUBSCN02 items, 3 SUBSCN03 equipment, 4 SUBSCN06 options)
constexpr uint16_t kPageDatFile = 0x3d;
// the panel's rows -> their pages (exe table 0x91a98c; 2 the closing)
const DataTable<ScalarTable<uint8_t, 5>> panelPages{TableId::StatusScreenPanelPages};
constexpr int8_t kPanelCloseRow = 6; // cross on the panel: the cursor off its rows

// ---- the VRAM the mode keeps (the host's VRAM) ----
constexpr TableId kPageDatAreaRects[2] = {TableId::StatusScreenFieldDatArea, TableId::StatusScreenPageDatArea}; // the page DATs' area the mode keeps
// the windows and the camera the mode keeps while the sub-screen runs (the exe copies them into the spare buffer)
StateArray<MessageWindow, kWindowCount>& keptWindows() {
    static StateArray<MessageWindow, kWindowCount> s{};
    return s;
}
CameraState& keptCamera() {
    static CameraState s;
    return s;
}
// the music's dip while the sub-screen runs (the SFX group's fade entry)
constexpr uint32_t kStatusMusicFade = 0x20;
constexpr int16_t kStatusMusicFadeStep = 0x444;
// the steps of the game mode 5
enum class StatusModeStep : uint8_t { WaitBuffer = 0, KeepField = 1, Run = 2, RestoreDraw = 3, RestoreField = 4 };

// exe 0x4012c0 (VRAM rect -> memory) / exe 0x401630 + 0x4016c0 (memory -> VRAM rect): the rect at `rect`, the pixels at
// `mem` (the menu's own buffer, game/status_screen MenuVramIo); the host owns the VRAM
VramArea vramArea(uint32_t rect) {
    const VramRect r = dataRecord<VramRect>(rect, VramRect::kBytes);
    return {r.x, r.y, r.w, r.h};
}
// (the exe moves the rect's bytes from / to the buffer's start: the highlight clut's rect is 8 colours of its 16)
void vramRect(uint32_t rect, std::span<uint8_t> pixels, bool toVram) {
    const VramArea a = vramArea(rect);
    if (pixels.size() < a.bytes()) throw std::logic_error("status screen: a VRAM transfer larger than its buffer");
    if (gVram) gVram(a, pixels.first(a.bytes()), toVram);
}
template <class T, size_t N> std::span<uint8_t> pixelsOf(StateArray<T, N>& a) {
    return {reinterpret_cast<uint8_t*>(a.data()), N * sizeof(T)};
}
// the page DATs' area kept (toVram false: sized to the rect) / put back
void pageAreaRect(size_t k, bool toVram) {
    std::vector<uint8_t>& px = keptPageArea().pixels[k];
    if (!toVram) px.assign(vramArea(stateAddress(kPageDatAreaRects[k])).bytes(), 0);
    vramRect(stateAddress(kPageDatAreaRects[k]), px, toVram);
}

// ---- the panel's drawing ----
// The panel's tile map (exe 0x919f48): its columns, rows and tile size (8 / 16), the map's start and the tpage, then the
// tiles' texture words, the map's bytes after them
struct PanelTileMap {
    static constexpr uint32_t kTiles = 8; // the tiles' texture words from the map's start
    PanelTileMap() : at(stateAddress(TableId::StatusScreenPanelTileMap)) {
        const DataReader r = dataAt(at, 8);
        columns = r.u8(0);
        rows = r.u8(1);
        tileSize = r.u8(2);
        mapStart = r.u16(4);
        tpage = r.u16(6);
    }
    uint32_t at = 0; // the map's table
    uint8_t columns = 0;
    uint8_t rows = 0;
    uint8_t tileSize = 0;
    uint16_t mapStart = 0; // the map's bytes from tile entry mapStart on
    uint16_t tpage = 0;

    // tile i's texture word / the map's byte i
    uint32_t tile(uint32_t i) const { return dataScalar<uint32_t>(at + kTiles + i * 4); }
    uint8_t mapByte(uint32_t i) const { return dataScalar<uint8_t>(at + kTiles + uint32_t(mapStart) * 4 + i); }
};
const DataTable<RecordTable<RowArtRecord, 5, 12>> panelRows{TableId::StatusScreenPanelRows};
// The area names (exe table 0x8c33c0: {u16 stage | room << 8, u16 first line << 8 | second}, an end byte 0xff)
struct AreaNameEntry : DataRecord {
    static constexpr uint32_t kBytes = 0x4;
    uint16_t place = 0; // +0x0
    uint16_t lines = 0; // +0x2
    template <class R> static AreaNameEntry load(const R& r) {
        AreaNameEntry v;
        v.place = r.template read<uint16_t>(0x0);
        v.lines = r.template read<uint16_t>(0x2);
        return v;
    }
};
constexpr uint8_t kAreaNamesEnd = 0xff;
constexpr int16_t kAreaSecondLines = 0x20; // the second line's message: 0x20 + its index
// the panel's art
constexpr uint32_t kStatusTitleArt = 0x7f10c890, kStatusLocationArt = 0x7f10d890;
constexpr uint32_t kGaugeCapacityArt = 0x7f105888, kGaugeLifeArt = 0x7f105880;
constexpr uint32_t kStatusIconArt = 0x7f9300c0; // + the effect's v (0x20 + k * 0x10) << 8
constexpr uint32_t kWeaponSwapArt = 0x7f1260c0, kWeaponLifterArt = 0x7f1270d0;
constexpr uint32_t kMenuBarArt = 0x7f100000, kLeftTabArt = 0x7f100080, kRightTabArt = 0x7f1000c0;
constexpr uint16_t kPanelMode = 0x20d, kTabMode = 7, kBackdropTileMode = 0x0d;
constexpr uint16_t kRowCursorClut = 0x7f13, kRowClut = 0x7f12;
constexpr uint32_t kLifeBlink = 0x17f; // the empty gauge's blink: (ph < 0x11 ? ph * 8 + 0x7f : 0x17f - ph * 8)
constexpr int kTimeX[6] = {0x74, 0x80, 0x94, 0xa0, 0xb4, 0xc0};
constexpr uint32_t kFramesPerHour = 0x34bc0, kFramesPerMinute = 0xe10, kFramesPerSecond = 0x3c;

// exe 0x5e7920: the panel's tile map and the "status" / "location" captions, in kOtFrames
void panelTileMapDraw() {
    const PanelTileMap map;
    const size_t c = otOpen(kOtFrames);
    otDrawMode(c, uint16_t(map.tpage | 0x200));
    labelNext(kLblStatusTitle);
    otSprite(c, 0x70, 0x10, kStatusTitleArt, 0x60, 0x10);
    labelNext(kLblStatusLocation);
    otSprite(c, 0x88, 0x20, kStatusLocationArt, 0x30, 0x10);
    uint32_t p = 0;
    const int cols = map.columns, rows = map.rows;
    const bool small = map.tileSize == 8;
    const int sh = small ? 3 : 4;
    artBegin(kVecStatusPanel);
    for (int r = 0; r < rows; ++r)
        for (int k = 0; k < cols; ++k) {
            const uint8_t i = map.mapByte(p++);
            if (small && i == 0) continue;
            otSprite(c, k << sh, r << sh, map.tile(i), 1 << sh, 1 << sh, kNeutral, small ? kSprite8 : kSprite16);
        }
    artEnd();
}
// exe 0x5e6db0(packed, stage, room): the area's name, two lines of the areas' bank, centred
void areaNameDraw(uint32_t packed, uint8_t st, uint8_t room) {
    uint16_t a = 0, b2 = 0;
    for (uint32_t i = 0;; ++i) {
        const AreaNameEntry e = dataRecord<AreaNameEntry>(stateAddress(TableId::StatusScreenAreaNames) + i * 4, AreaNameEntry::kBytes);
        if (uint8_t(e.place) == kAreaNamesEnd) break;
        if (e.place == (uint32_t(room) << 8 | st)) {
            a = (e.lines >> 8) & 0x1f;
            b2 = e.lines & 0xff;
            break;
        }
    }
    const uint32_t y = (packed >> 10) & 0x1ff, palette = (packed >> 19) & 0xf;
    bankTextDraw(textPlace(packed & 0x3ff, y, palette), menuBanks().areas, int16_t(a), kTextCentred);
    bankTextDraw(textPlace(packed & 0x3ff, y + 0xe, palette), menuBanks().areas, int16_t(b2 + kAreaSecondLines),
                 kTextCentred);
}
// exe 0x5e72f0: the panel (not in the title's options nor while switching): the tile map, the five rows (the cursor's
// row clut 0x7f13, else 0x7f12), the zenny (7 digits), the play time, the life gauge, the status effects, the special
// weapon and its button, the area, the weapon in use
void panelDraw(StatusScreen& s) {
    GameState& g = gameState();
    if (!drawing() || g.frontEnd == kFrontEndTitle || s.switching >= 1) return;
    panelTileMapDraw();
    const size_t c = otOpen(kOtRows);
    otDrawMode(c, kPanelMode);
    for (int k = 0; k < 5; ++k) {
        const RowArtRecord& e = panelRows[size_t(k)];
        const uint32_t clut = kRowCursorClut - uint32_t(s.cursor != k);
        labelNext(kLblStatusRow, uint16_t(k | (s.cursor == k ? 0x10 : 0)));
        rowNext(k);
        otSprite(c, e.x, e.y, clut << 16 | e.uv, e.w, e.h);
    }
    int32_t z = g.zenny, div = 1000000;
    for (int k = 0, x = 0x78; k < 7; ++k, x += 10) {
        const int32_t d = z / div;
        z %= div;
        div /= 10;
        otGlyph(c, x, 0x52, uint16_t(d + 0x30));
    }
    const uint32_t t = g.playTime;
    const uint32_t hours = t / kFramesPerHour, mins = (t % kFramesPerHour) / kFramesPerMinute,
                   secs = (t % kFramesPerMinute) / kFramesPerSecond;
    const uint16_t dig[6] = {uint16_t(hours / 10), uint16_t(hours % 10), uint16_t(mins / 10),
                             uint16_t(mins % 10), uint16_t(secs / 10), uint16_t(secs % 10)};
    for (int k = 0; k < 6; ++k)
        if (k != 0 || dig[0] != 0) otGlyph(c, kTimeX[k], 0x6a, uint16_t(dig[k] + 0x30));
    Player& p = player();
    const int maxBars = p.maxHitPoints >> 4;
    int y = 0x75;
    for (int k = 0; k < maxBars; ++k, y -= 8) {
        labelNext(kLblGaugeCell, 0);
        otSprite(c, 0x104, y, kGaugeCapacityArt, 8, 7);
    }
    const int hp = p.hitPoints;
    if (hp == 0) { // the empty gauge blinks red
        uint32_t ph = (g.frameWord() >> 1) & 0x1f;
        ph = ph < 0x11 ? ph * 8 + 0x7f : kLifeBlink - ph * 8;
        int yy = 0x75;
        for (int k = 0; k < maxBars; ++k, yy -= 8)
            otQuad(c, {0x105, 0x10b, 0x105, 0x10b}, {yy, yy, yy + 7, yy + 7}, ph & 0xffffff, kPolyF4);
    } else {
        int yy = 0x75;
        for (int k = 0; k < (hp >> 4); ++k, yy -= 8) {
            labelNext(kLblGaugeCell, 1);
            otSprite(c, 0x104, yy, kGaugeLifeArt, 8, 7);
        }
        const int part = ((hp % 16) * 7 + 0xf) / 16;
        labelNext(kLblGaugeCell, 1);
        otSprite(c, 0x104, yy + 7 - part, kGaugeLifeArt, 8, part);
    }
    for (int k = 0; k < 4; ++k)
        if (p.status & (0x11u << k))
            otSprite(c, 0x110, 0x40 + k * 0x10, kStatusIconArt | uint32_t(0x20 + k * 0x10) << 8, 0x10, 0x10);
    const uint32_t wc = p.weaponMode != 0 ? kNeutral : kDim;
    const uint16_t dim = wc != kNeutral ? 1 : 0;
    labelNext(kLblWeaponSwap, dim);
    otSprite(c, 0x68, 0x98, kWeaponSwapArt, 0x40, 0x10, wc);
    labelNext(kLblWeaponLifter, dim);
    otSprite(c, 0xa8, 0x98, kWeaponLifterArt, 0x30, 0x10, wc);
    labelNext(kLblPrompt, uint16_t(12 | dim << 8)); // the triangle
    const uint16_t key = padButtonKey(kPadTriangle); // the button bound to the triangle (exe 0x40a260)
    otGlyph(c, 0x6a, 0x9a, uint16_t(key + 0xf), wc);
    labelNext(kLblMenuBar);
    otSprite(c, 0x68, 0xd0, kMenuBarArt, 0x80, 0x10);
    areaNameDraw(textPlace(0x65, 0x2e), g.stage, g.room);
    if (p.activeSpecial >= 0) { // the weapon (a byte)
        bankTextDraw(textPlace(0x65, 0x86), menuBanks().names, itemString(p.activeSpecial), kTextNoRuby | kTextCentred);
    }
}
// exe 0x5e7ee0: the backdrop; past the panel (or while switching) the page header (tpage 7: the L / R tabs in a
// normal game, the "enter / esc" bar)
void pageHeaderDraw(StatusScreen& s) {
    backdropDraw(s.switching);
    if (int8_t(s.page) <= 2 && s.switching < 1) return;
    const size_t c = otOpen(kOtRows);
    otDrawMode(c, kTabMode);
    if (gameState().frontEnd == kFrontEndGame) {
        const int extra = screenExtra();
        labelNext(kLblPageTab, uint16_t(0 | s.page << 8));
        otSprite(c, 0x10 - extra, 0x12, kLeftTabArt, 0x40, 0x10); // the L / R tabs: at the screen's edges (widescreen)
        labelNext(kLblPageTab, uint16_t(1 | s.page << 8));
        otSprite(c, 0xf0 + extra, 0x12, kRightTabArt, 0x40, 0x10);
    }
    labelNext(kLblMenuBar);
    otSprite(c, 0x68, 0xd0, kMenuBarArt, 0x80, 0x10);
}

// a page OpenDash does not port: back to the panel's cursor (logged once per open)
void pageNotPorted(StatusScreen& s, uint8_t page) {
    std::fprintf(stderr, "status menu: page %u not ported (3 map)\n", page);
    s.page = uint8_t(StatusPage::Panel);
    s.state = uint8_t(PanelState::Rows);
    s.switching = 0;
}
// exe 0x5e6f90: page 1, the panel (its rows -> the pages); the panel drawn on every path
void panel(StatusScreen& s) {
    switch (PanelState(s.state)) {
    case PanelState::Enter:
        s.state = uint8_t(PanelState::Rows);
        [[fallthrough]];
    case PanelState::Rows:
        menuCursorStep(s, 1);
        if ((menuPadPressed() & kPadTriangle) == 0) {
            if (menuPadPressed() & kPadConfirm) {
                s.state = uint8_t(uint32_t(panelPages[size_t(int32_t(s.cursor))] != uint8_t(StatusPage::Closing)) + 2u);
                break;
            }
            if (menuPadPressed() & kPadCross) s.cursor = kPanelCloseRow;
        }
        // triangle with a special weapon: the panel's weapon switch -- not ported
        break;
    case PanelState::Close:
        // exe 0x5e7dd0: the display buffer copied over the screen for the closing frames -- the host keeps the last
        // picture itself
        s.page = uint8_t(StatusPage::Closing);
        s.state = 0;
        break;
    case PanelState::WaitLoads:
        if (fileLoads().loading & kFileLoading) break;
        s.state = uint8_t(PanelState::LoadPage);
        [[fallthrough]];
    case PanelState::LoadPage:
        pageDatLoad(s, uint8_t(s.cursor + 1));
        s.state = uint8_t(PanelState::WaitPage);
        [[fallthrough]];
    case PanelState::WaitPage:
        if (pageDatReady(s) == 0) break;
        {
            const uint8_t page = panelPages[size_t(int32_t(s.cursor))];
            s.enterPage(StatusPage(page));
            if (page == uint8_t(StatusPage::Map)) pageNotPorted(s, page);
        }
        break;
    default:
        break;
    }
    panelDraw(s);
}
// exe 0x5e7200: page 2, closing -- the player's weapon state reset when the weapon in hand or the buster's rapid level
// changed while their arm is up, the player files of the equipment requested; then, the loads done, the field's model
// bank back, the windows closed, the pace back
void closing(StatusScreen& s) {
    pageBankOut(); // OpenDash: the field's model bank back if the equipment page still had its bank in
    GameState& g = gameState();
    Player& p = player();
    if (s.state == 0 && g.frontEnd == kFrontEndTitle) s.state = 1;
    if (s.state == uint8_t(ClosingState::ResetWeapons)) {
        if (uint32_t(s.savedWeapon) != uint32_t(int32_t(p.activeSpecial)) && (p.armFlags & kArmSpecial)) weaponReset();
        if (s.savedRapidLevel != p.weaponLevels[weaponLevelIndex(kBusterLevelWeapon, WeaponStat::Rate)] &&
            (p.armFlags & kArmBuster))
            weaponReset();
        if (gPlayerFiles) gPlayerFiles();
        s.state += 1;
    } else if (s.state == uint8_t(ClosingState::Restore) && (fileLoads().loading & kFileLoading) == 0) {
        p.stageAnimMarker = 0;
        stage().bankBuffer = s.savedBankBuffer;
        statusBankWalk();
        windowCloseAll(WindowClose::Force);
        display().pace = s.savedPace;
        s.state += 1;
    }
}
// exe 0x5e7c50: the highlight clut's pulse -- the 16 kept colours scaled by k = 0x20..0x80 / 0x80 (a 0x30-frame
// triangle of the frame counter) into the pulsed copy, uploaded to the clut's rect
void highlightPulse() {
    HighlightClut& h = highlightClut();
    const uint32_t ph = gameState().frameWord() % 0x30;
    const uint32_t k = ph < 0x19 ? ph * 4 + 0x20 : (0x38 - ph) * 4;
    for (size_t i = 0; i < h.kept.size(); ++i) {
        const uint32_t c = h.kept[i], g = c >> 2 & 0xff;
        const uint32_t v = ((((((c >> 0xc & 7) ^ (c >> 7 & 0xf8)) * k >> 7 & 0xf8) << 5) |
                             ((((g ^ (c >> 7 & 0xff)) & 7) ^ g) * k >> 7 & 0xf8)) << 2) |
                           ((((uint32_t(uint8_t(c << 3))) | (c >> 2 & 7)) * k >> 10) & 0x1f) | (c & 0x8000);
        h.pulsed[i] = uint16_t(v);
    }
    vramRect(stateAddress(TableId::StatusScreenHighlightClutRect), pixelsOf(h.pulsed), true);
}
// exe 0x5e7b10: the L1 / R1 page switch (the target page, its state and row, a countdown that backs the levels out
// first) and select (close)
void pageSwitch(StatusScreen& s) {
    highlightPulse();
    const int8_t page = int8_t(s.page);
    if (page == int8_t(StatusPage::Closing)) return;
    if (s.switching > 0) {
        const int8_t k = int8_t(s.switchCountdown - 1);
        s.switchCountdown = k;
        switch (k) {
        case 0:
            s.sub = 0;
            s.step = 0;
            s.detail = 0;
            s.cursorKept = s.switchRow;
            s.page = s.switchPage;
            s.state = s.switchState;
            windowCloseAll(WindowClose::Force);
            windowLayoutVariant(WindowLayout::SubScreen);
            return;
        case 1:
        case 2:
        case 3:
        case 4: s.level(uint32_t(k)) = kLevelBack; return;
        default: return;
        }
    }
    if (gameState().frontEnd == kFrontEndTitle) return;
    if (s.switching == 0) {
        s.switching = -1;
        return;
    }
    const uint16_t pressed = menuPadPressed();
    if (page > 2 && (pressed & (kPadL1 | kPadR1))) {
        s.switching = 1;
        const int c = (pressed & kPadL1) ? page : page - 2;
        s.switchRow = uint8_t(c % 4);
        s.switchPage = uint8_t(StatusPage::Panel);
        s.switchState = uint8_t(PanelState::LoadPage);
        s.switchCountdown = 5;
    }
    if (menuPadPressed() & kPadSelect) {
        s.cursorShown = 0;
        if (page > 2) {
            s.switching = 1;
            s.switchPage = uint8_t(StatusPage::Panel);
            s.switchRow = 4;
            s.switchState = uint8_t(PanelState::Close);
            s.switchCountdown = 5;
            return;
        }
        if (page == int8_t(StatusPage::Panel)) {
            s.cursor = kPanelCloseRow;
            s.page = uint8_t(StatusPage::Panel);
            s.state = uint8_t(PanelState::Close);
        }
    }
}

} // namespace

void setMenuVram(MenuVramIo io) { gVram = std::move(io); }
void setMenuPlayerFiles(std::function<void()> f) { gPlayerFiles = std::move(f); }
void requestPlayerFiles() {
    if (gPlayerFiles) gPlayerFiles();
}

void pageBankIn(uint32_t bank) {
    if (bank == 0) return;
    StageState& st = stage();
    if (!gPageBank) gFieldBank = st.modelBank;
    gPageBank = true;
    st.modelBank = bank;
}
void pageBankOut() {
    if (!gPageBank) return;
    stage().modelBank = gFieldBank;
    gPageBank = false;
}

void pageDatLoad(StatusScreen& s, uint8_t k) {
    if ((s.pageDat & kPageDatFileMask) == k) {
        if (s.pageDat == (k | kPageDatShown)) s.pageDat = uint8_t(k | kPageDatLoaded);
        return;
    }
    stage().bankBuffer = s.savedBankBuffer;
    statusBankWalk();
    datLoadRequest(uint16_t(int8_t(k) + kPageDatFile));
    s.pageDat = uint8_t(k | kPageDatLoading);
}

int pageDatReady(StatusScreen& s) {
    const uint8_t st = s.pageDat & kPageDatStateMask;
    if (st == kPageDatLoading) {
        if (fileLoads().loading & kFileLoading) return 0;
        s.pageDat = uint8_t((s.pageDat & kPageDatFileMask) | kPageDatLoaded);
    } else if (st != kPageDatLoaded) {
        return 0;
    }
    s.pageDat = uint8_t((s.pageDat & kPageDatFileMask) | kPageDatShown);
    return 1;
}

void statusScreenOpenOptions() {
    StatusScreen& s = statusScreen();
    s.page = uint8_t(StatusPage::Panel);
    s.state = uint8_t(PanelState::WaitLoads);
    s.cursorKept = 3;
    s.cursor = 3;
}

void statusScreenOpen() {
    StatusScreen& s = statusScreen();
    s.clearBytes();
    HighlightClut& h = highlightClut();
    vramRect(stateAddress(TableId::StatusScreenHighlightClutRect), pixelsOf(h.kept), false);
    for (size_t i = 0; i < h.kept.size(); ++i) h.pulsed[i] = h.kept[i];
    Player& p = player();
    s.savedRapidLevel = p.weaponLevels[weaponLevelIndex(kBusterLevelWeapon, WeaponStat::Rate)];
    s.savedWeapon = uint8_t(p.activeSpecial);
    s.savedBankBuffer = stage().bankBuffer;
    stageClassTables().animated.pointAt(TableId::StatusScreenClassTables);
    windowLayoutVariant(WindowLayout::SubScreen);
    s.savedPace = display().pace;
    display().pace = kTitleDisplayPace;
}

int statusScreenFrame(std::vector<ScreenPrim>* draw) {
    StatusScreen& s = statusScreen();
    begin(draw);
    discard();
    pageHeaderDraw(s);
    switch (int8_t(s.page)) {
    case int8_t(StatusPage::Start):
        s.page = uint8_t(StatusPage::Panel);
        s.state = 0;
        break;
    case int8_t(StatusPage::Panel): panel(s); break;
    case int8_t(StatusPage::Closing): closing(s); break;
    case int8_t(StatusPage::Items): itemsPage(s); break;
    case int8_t(StatusPage::Equipment): equipmentPage(s); break;
    case int8_t(StatusPage::Options): optionsPage(s); break;
    default: pageNotPorted(s, s.page); break;
    }
    pageSwitch(s);
    s.cursor = int8_t(s.cursorKept);
    end();
    if (int8_t(s.page) == int8_t(StatusPage::Closing) && s.state == uint8_t(ClosingState::Closed)) {
        vramRect(stateAddress(TableId::StatusScreenHighlightClutRect), pixelsOf(highlightClut().kept), true); // the highlight clut back
        return 1;
    }
    return 0;
}

void statusModeFrame(std::vector<ScreenPrim>* draw) {
    GameState& g = gameState();
    StageClassTables& classes = stageClassTables();
    DrawEnvironments& env = drawEnvironments();
    GameLoopSaves& saves = gameLoopSaves();
    MessageSystem& messages = messageSystem();
    CameraState& camera = cameraState();
    switch (StatusModeStep(g.modeStep)) {
    case StatusModeStep::WaitBuffer:
        if (display().drawBuffer != 0) {
            setBufferMode(kBufferModeStage);
            g.modeStep += 1;
        }
        break;
    case StatusModeStep::KeepField:
        classes.fieldAnimated = classes.animated;
        saves.keepClearBackground(uint8_t(env.clearBackground));
        env.clearBackground = 1;
        applyDrawEnvironmentFlags();
        pageAreaRect(0, false);
        pageAreaRect(1, false);
        windowVoicesStop();
        keptWindows() = messageWindows();
        saves.statusWaitBits = messages.waitBits;
        saves.statusOpenBits = messages.openBits;
        keptCamera() = camera;
        messages.openBits = 0;
        messages.waitBits = 0;
        windowCloseAll(WindowClose::Force);
        padsClearTaps();
        statusScreenOpen();
        soundFadeDip(kStatusMusicFade, kStatusMusicFadeStep, 0, 0);
        g.modeStep += 1;
        break;
    case StatusModeStep::Run:
        if (statusScreenFrame(draw) != 0) g.modeStep += 1;
        // exe 0x5f0580: the host decodes the draw commands every game frame (od::dc)
        break;
    case StatusModeStep::RestoreDraw:
        classes.animated = classes.fieldAnimated;
        env.clearBackground = uint8_t(saves.statusClearBackground);
        applyDrawEnvironmentFlags();
        setBufferMode(kBufferModeNormal);
        g.modeStep += 1;
        break;
    case StatusModeStep::RestoreField:
        pageAreaRect(0, true);
        pageAreaRect(1, true);
        messageWindows() = keptWindows();
        messages.openBits = saves.statusOpenBits;
        messages.waitBits = saves.statusWaitBits;
        camera = keptCamera();
        soundFadeRestore(kStatusMusicFade, kStatusMusicFadeStep, 0);
        soundLoopState().holdLoops = 0;
        g.setMode(GameMode::Field);
        g.modeStep = uint8_t(FieldStep::FadeInWait);
        break;
    default:
        break;
    }
    playTimeTick(); // exe 0x5426e0 on every path
}

} // namespace od::port
