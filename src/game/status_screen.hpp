// The status sub-screen of dash2.exe: the game mode 5 (exe 0x541bf0: the field's state kept, the sub-screen run, the
// field given back), opened from the field play by the select button when no story menu holds the game; also the
// title's options (game/title). Its block (StatusScreen) holds the page and the levels of its cursor tree, the page
// DAT's load, the cursors, the lists and the page switch; the pages are the panel (page 1: the zenny, the time, the
// life, the area, the rows), the closing (2), the items (4, game/status_items), the equipment (5,
// game/status_equipment) and the options (6, game/status_options); the map (3) is not ported.
#pragma once

#include <cstdint>
#include <algorithm>
#include <functional>
#include <span>
#include <vector>

#include "engine/screen_prim.hpp"
#include "game/loaded_data.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"

namespace od::port {


// +0: the page (the handler table 0x91a970)
enum class StatusPage : uint8_t {
    Start = 0,     // exe 0x5e6f70: to the panel
    Panel = 1,     // exe 0x5e6f90
    Closing = 2,   // exe 0x5e7200
    Map = 3,       // exe 0x5e86a0 (not ported)
    Items = 4,     // exe 0x5e9f50
    Equipment = 5, // exe 0x5eb180
    Options = 6,   // exe 0x5edb70
};
// +1 of the panel
enum class PanelState : uint8_t {
    Enter = 0,
    Rows = 1,      // the cursor on the rows
    Close = 2,     // the closing page (exe 0x5e7dd0's copy of the display: the host keeps the last picture)
    WaitLoads = 3, // a row chosen: the loads done first
    LoadPage = 4,  // the page's DAT
    WaitPage = 0xa,
};
// +1 of the closing page
enum class ClosingState : uint8_t { ResetWeapons = 0, Restore = 1, Closed = 2 };
// +0x11: the page DAT's load (the low nibble its file: 1 map, 2 items, 3 equipment, 4 options)
constexpr uint8_t kPageDatLoading = 0x20, kPageDatLoaded = 0x30, kPageDatShown = 0x40;
constexpr uint8_t kPageDatStateMask = 0xf0, kPageDatFileMask = 0x0f;
// +0x1f: the cursor's answer
constexpr int8_t kAnswerNone = 0, kAnswerConfirm = 1, kAnswerCancel = -1;
// a page's levels (+0 page, +1 state, +2 sub, +3 step, +4 detail): a level >= kFirstChildState is a row chosen on the
// level above (the row = the value - kFirstChildState)
constexpr uint8_t kFirstChildState = 3;
constexpr uint8_t kLevelBack = 2; // a level's "back" state (the cancel writes it)

// The sub-screen's block (exe 0xa7a140, 0x40 bytes, then the turning model's pointer)
struct StatusScreen {
    uint8_t page = 0;        // +0x00: StatusPage
    uint8_t state = 0;       // +0x01: the page's levels
    uint8_t sub = 0;         // +0x02
    uint8_t step = 0;        // +0x03
    uint8_t detail = 0;      // +0x04
    uint8_t unread05 = 0;    // +0x05 (the cursor tree's level past detail; no reader traced)
    int8_t switching = 0;    // +0x06: the page switch: > 0 runs (switchCountdown), 0 none, -1 armed
    uint8_t savedPace = 0;   // +0x07
    int8_t savedRapidLevel = 0; // +0x08: the buster's fourth level at the open (a change resets the weapons)
    uint8_t savedWeapon = 0; // +0x09: the special weapon in use at the open
    uint8_t slideIn = 0;     // +0x0a: the rows slide in (2: the slide ends)
    int8_t slideX = 0;       // +0x0b: the slide's x / 16
    uint8_t manualStep = 0;  // +0x0c: the buster's manual sort: 0 the first part, 1 the one it swaps with
    StateArray<uint8_t, 3> unread0d{}; // +0x0d (no reader traced)
    uint8_t savedBankBuffer = 0; // +0x10
    uint8_t pageDat = 0;     // +0x11: kPageDat* | the file
    StateArray<uint8_t, 2> unread12{}; // +0x12
    int8_t cursor = 0;       // +0x14: the cursor's row / column
    uint8_t cursorKept = 0;  // +0x15: the cursor every frame ends on (a level back puts it on the row left)
    uint8_t cursorShown = 0; // +0x16: the lists' cursor bar is shown
    uint8_t unread17 = 0;    // +0x17
    uint8_t listRow = 0;     // +0x18: the lists' cursor: its visible row, the first row shown, the rows, the
    uint8_t listScroll = 0;  // +0x19: visible rows, the cursor / row / scroll animations
    uint8_t listRows = 0;    // +0x1a
    uint8_t listVisible = 0; // +0x1b
    int8_t cursorStep = 0;   // +0x1c
    int8_t rowStep = 0;      // +0x1d
    int8_t scrollStep = 0;   // +0x1e
    int8_t answer = 0;       // +0x1f: kAnswer*
    uint8_t switchPage = 0;  // +0x20: the page switch: the page, its state, the cursor's row, the countdown
    uint8_t switchState = 0; // +0x21
    uint8_t switchRow = 0;   // +0x22
    int8_t switchCountdown = 0; // +0x23
    StateArray<int8_t, 5> savedBusterLevels{}; // +0x24: the buster's levels before a part is tried
    uint8_t modelWeapon = 0; // +0x29: the weapon of the turning model (0xff: none yet)
    uint8_t padKind = 0;     // +0x2a: kPadKind*
    uint8_t testRumble = 0;  // +0x2b: the vibration's test rumble ran on an analog pad
    StateArray<int8_t, 5> barLength{};   // +0x2c: the stat bars: the new lengths, the old ones, the slide's step
    StateArray<int8_t, 5> barPrevious{}; // +0x31
    int16_t barSlide = 0;    // +0x36
    int16_t item = 0;        // +0x38: the item chosen (the items page) / the part chosen (the manual sort)
    uint8_t listLength = 0;  // +0x3a: the items list's length
    int8_t listKind = 0;     // +0x3b: the items list: 0 the items, 1 the key items
    uint8_t unread3c = 0;    // +0x3c
    int8_t result = 0;       // +0x3d: the use's result message
    StateArray<uint8_t, 2> unread3e{}; // +0x3e
    ActorRef<AnimatedActor> modelActor; // +0x40: the equipment page's turning model (0: none)

    StatusPage currentPage() const { return StatusPage(page); }
    // the level byte k (0 page .. 4 detail, 5 the byte past it; the cursor tree walks them)
    uint8_t& level(uint32_t k) {
        switch (k) {
        case 0: return page;
        case 1: return state;
        case 2: return sub;
        case 3: return step;
        case 4: return detail;
        case 5: return unread05;
        default: stateIndexError(k, 6);
        }
    }
    uint8_t level(uint32_t k) const { return const_cast<StatusScreen*>(this)->level(k); }
    // the levels cleared (the exe's dword store at +0), then the page p (a byte store)
    void enterPage(StatusPage p) {
        page = uint8_t(p);
        state = 0;
        sub = 0;
        step = 0;
    }
    // the block's 0x40 bytes cleared one at a time (exe 0x5e6e70; the model's pointer after them kept)
    void clearBytes() {
        const ActorRef<AnimatedActor> model = modelActor;
        *this = StatusScreen{};
        modelActor = model;
    }
    // the bars' slide cleared as two bytes (the exe's byte stores)
    void clearBarSlideBytes() { barSlide = 0; }
};
inline StatusScreen& statusScreen() {
    static StatusScreen s;
    return s;
}
// The pages' list (0xa7a020: the items page's item ids, the equipment page's values; kListEnd ends the items')
constexpr uint16_t kListEnd = 0xffff;
struct StatusList {
    StateArray<uint16_t, 0x82> entries{}; // 0xa7a020

    // an entry's low byte (the exe reads the u16 entries as bytes when it equips)
    uint8_t entryByte(size_t i) const { return uint8_t(entries[i]); }
    // the list cleared as 0x41 dwords
    void clearWords() { entries = {}; }
};
inline StatusList& statusList() {
    static StatusList s;
    return s;
}
// +0x2a: the pad's kind (the vibration row needs a pad that vibrates)
constexpr uint8_t kPadKindOther = 0, kPadKindDigital = 1, kPadKindAnalog = 2;

// A VRAM rect (exe data: {s16 x, y, w, h}): the rect of a MenuVramIo transfer
struct VramRect : DataRecord {
    static constexpr uint32_t kBytes = 0x8;
    int16_t x = 0; // +0x0
    int16_t y = 0; // +0x2
    int16_t w = 0; // +0x4
    int16_t h = 0; // +0x6
    template <class R> static VramRect load(const R& r) {
        VramRect v;
        v.x = r.template read<int16_t>(0x0);
        v.y = r.template read<int16_t>(0x2);
        v.w = r.template read<int16_t>(0x4);
        v.h = r.template read<int16_t>(0x6);
        return v;
    }
};
// The VRAM is the host's: exe 0x4012c0 (toVram false: the rect -> `pixels`) and exe 0x401630 / 0x4016c0 (toVram true:
// `pixels` -> the rect); `pixels` holds the rect's w * h 16-bit pixels, row by row. Used for the highlight clut's pulse
// and the page DATs' area the mode keeps
struct VramArea {
    int16_t x = 0, y = 0, w = 0, h = 0;
    size_t bytes() const { return size_t(std::max<int>(0, w)) * size_t(std::max<int>(0, h)) * 2; }
};
using MenuVramIo = std::function<void(const VramArea& area, std::span<uint8_t> pixels, bool toVram)>;
void setMenuVram(MenuVramIo io);
// exe 0x5d5c40 (the closing page, the development room): the host maps the body / weapon files the equipment now asks
// for (od::playerDatRequest) and rebuilds Megaman's model
void setMenuPlayerFiles(std::function<void()> f);
void requestPlayerFiles();

// exe 0x541bf0 (game mode 5): one frame; `draw` receives the sub-screen's primitives (in drawing order, 320 x 240)
// when not null
void statusModeFrame(std::vector<ScreenPrim>* draw = nullptr);
// exe 0x5e6e70: the block set up (page Start), the highlight clut kept, pace 1
void statusScreenOpen();
// exe 0x5e8580 (the title's options after exe 0x5e6e70): the sub-screen straight into the options page
void statusScreenOpenOptions();
// exe 0x5e6f00: one frame of the sub-screen; 1 = closed (the closing page's last state)
int statusScreenFrame(std::vector<ScreenPrim>* draw = nullptr);

// ---- the pieces the pages share (status_screen.cpp) ----
// exe 0x5e7d30(block, k): the page DAT k (the field's bank buffer back, the bank walked, the load queued)
void pageDatLoad(StatusScreen& s, uint8_t k);
// exe 0x5e7d90: the page DAT is there (loading -> loaded -> shown)
int pageDatReady(StatusScreen& s);
// the equipment page's model bank (game/stage_files statusBankWalk) swapped into the stage's model bank while its rows
// run, given back
void pageBankIn(uint32_t bank);
void pageBankOut();

} // namespace od::port
