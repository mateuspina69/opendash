// Roll's development room (see dev_room.hpp), one routine per exe routine (Roll's 0x543cd0.. and the Nino overlay's
// copy 0x5461b0.., 21 routines instruction-identical, over its own block and price tables; the other tables have the
// same contents).
#include "content/yosyonke/dev_room.hpp"
#include "content/yosyonke/dev_room_state.hpp"

#include <algorithm>
#include <cstdlib>

#include "engine/data_source.hpp"
#include "game/loaded_data.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/audio_logic.hpp"
#include "game/camera_state.hpp"
#include "game/display.hpp"
#include "game/event_state.hpp"
#include "game/fade_state.hpp"
#include "game/game_state.hpp"
#include "game/items.hpp"
#include "game/list_cursor.hpp"
#include "game/matrix.hpp"
#include "game/menu_banks.hpp"
#include "game/menu_draw.hpp"
#include "game/menu_pointer.hpp"
#include "game/message.hpp"
#include "game/message_state.hpp"
#include "game/model_preview.hpp"
#include "game/player/player_input.hpp"
#include "game/player/player_script_control.hpp"
#include "game/player/player_state.hpp"
#include "game/player/player_weapons.hpp"
#include "game/port_gap.hpp"
#include "game/room_scripts.hpp"
#include "game/script_engine.hpp"
#include "game/script_state.hpp"
#include "game/stage_files.hpp"
#include "game/status_screen.hpp"
#include "game/story_flags.hpp"
#include "game/transition.hpp"
#include "game/transition_state.hpp"
#include "game/window_frame.hpp"

using namespace od::port::dev_room_state;

namespace od::content {

using namespace od::port;

namespace {

std::vector<ScreenPrim>* gDraw = nullptr;

constexpr const char* kDevRoomPort = "development room"; // the area of its port-gap errors

// ---- the menu's block -----------------------------------------------------------------------------------------------

// The pages of the menu (+0)
enum class DevPage : uint8_t {
    RollMenu = 0, // Roll's opening message and her menu (exe 0x543cf0)
    Develop = 1,  // an item's recipe (exe 0x544870)
    Change = 2,   // the special weapon changed (exe 0x543f50)
    Improve = 3,  // a weapon's stat improved (exe 0x544230)
};
// +1 of Roll's menu page
enum class RollMenuState : uint8_t {
    Opening = 0,      // the model freed, the windows closed, the tool kit tier's message picked
    OpeningShow = 1,  // her message
    OpeningWait = 2,
    Caption = 3,      // the caption, then her menu
    MenuShow = 4,
    MenuAnswer = 5,
};


// A recipe (exe table 0x8aed08, 12 bytes; a result of -1 ends it): the result made of two items, its kind (1: "I can
// make X", 2: its own message, else "combine with X?") and the messages when the other item is missing
struct DevRecipe : DataRecord {
    static constexpr uint32_t kBytes = 0xc;
    int16_t result = 0; // +0x0
    int16_t itemA = 0; // +0x2
    int16_t itemB = 0; // +0x4
    uint8_t kind = 0; // +0x6
    int16_t messageB = 0; // +0x8: shown when the item under the cursor is B (its kind 2: its own message)
    int16_t messageA = 0; // +0xa
    template <class R> static DevRecipe load(const R& r) {
        DevRecipe v;
        v.result = r.template read<int16_t>(0x0);
        v.itemA = r.template read<int16_t>(0x2);
        v.itemB = r.template read<int16_t>(0x4);
        v.kind = r.template read<uint8_t>(0x6);
        v.messageB = r.template read<int16_t>(0x8);
        v.messageA = r.template read<int16_t>(0xa);
        return v;
    }
};
constexpr uint32_t kRecipeSize = 0xc;
constexpr int16_t kRecipesEnd = -1;
constexpr uint8_t kRecipeCanMake = 1, kRecipeOwnMessage = 2, kRecipeWorkshop = 3;

// The room copy that runs (its block and its price tables)
struct DevRoomCopy {
    bool nino;       // Nino's copy (its block devRoomMenus().nino)
    TableId prices;  // a table per weapon: the price of each level * 5 + stat
};
constexpr DevRoomCopy kRollCopy{false, TableId::DevRoomRollCopyPrices}, kNinoCopy{true, TableId::DevRoomNinoCopyPrices};
const DevRoomCopy* gCopy = &kRollCopy;
DevRoomMenu& menu() { return gCopy->nino ? devRoomMenus().nino : devRoomMenus().roll; }
bool ninoCopy(const DevRoomMenu& b) { return &b == &devRoomMenus().nino; }

// ---- the tables and constants ---------------------------------------------------------------------------------------

// the hand's bob (exe table 0x8aee78: x offsets by (frame >> 1) & 0xf)
const DataTable<ScalarTable<int8_t, 0x10>> devHandBob{TableId::DevRoomHandBob};
// the model actor (pool 0x20 of the stage's class table, type kDevModelType: the exe's class 0x4ec4f0)
constexpr uint8_t kDevModelType = 0x65;
constexpr int16_t kDevModelPitch = 0x200;
constexpr int kDevModelYawOffset = 0x400;
constexpr int16_t kFirstWeaponItem = 0x380, kLastWeaponItem = 0x393;
constexpr int kStatBars = 5, kDevBarLength = 0x38;
constexpr uint32_t kLastStat = 4; // the stats past it are the last one's
constexpr int16_t kInfiniteEnergy = 0x7fff;
// the panel's frame (exe 0x545850: the drawing area, the draw mode 0x20d, two G3 fans, two outline rings, 2 px corners)
constexpr uint16_t kDevFrameMode = 0x20d;
// the panel frame's colours (0xBBGGRR; FrameStyle's darker): a dark blue edge, a near-black centre, the grey and the
// white outline
constexpr uint32_t kDevFrameEdge = 0x301818, kDevFrameCentre = 0x0c0808;
constexpr uint32_t kDevFrameGreyLine = 0x808080, kDevFrameWhiteLine = 0xf8f8f8;
// the art (tpage 0xd, clut 0x7f11)
constexpr uint16_t kStatsMode = 0x0d, kStatsClut = 0x7f11;
constexpr uint32_t kNoWeaponCaptionsArt = 0x7f1180d0, kPreviewEndArt = 0x7f11b0f8;
constexpr uint32_t kInfiniteArt = 0x7f11a8d0, kLevelEndArt = 0x7f11b0d0, kCaptionArt = 0x7f1100d0;
constexpr uint32_t kModelBoxColour = 0x504028;
constexpr uint16_t kPreviewStat = 0x100; // the stats' flags: preview the stat (flags & 0xf) one level up

// the lists: the development items (kind 1) and the special weapons, their rows shown, "back" last
constexpr int kDevelopItems = 1;
constexpr uint32_t kDevelopFirst = 8, kDevelopEnd = 0x38, kDevelopBase = 0x400;
constexpr uint32_t kWeaponFirst = 3, kWeaponEnd = 0x14, kWeaponBase = 0x380;
constexpr uint8_t kDevelopRows = 6, kWeaponRows = 3;
constexpr int16_t kBackItem = 0x485;
// the windows: 0 the captions, 1 the list's frame, 2 the questions / Roll
constexpr int8_t kCaptionWindow = 0, kListWindow = 1, kRollWindow = 2;
constexpr uint8_t kWindowShowsText = 4; // windowStatus: the window's text is shown
// the messages (the room's bank)
constexpr int16_t kMsgRollMenu = 0, kMsgImproveStat = 1, kMsgIsThisOk = 2, kMsgPrice = 3, kMsgListFrame = 4;
constexpr int16_t kMsgZenny = 5, kMsgChanged = 6, kMsgImproved = 7, kMsgAtMaximum = 8, kMsgTooDear = 9;
constexpr int16_t kMsgCombine = 0xf, kMsgCanMake = 0x11, kMsgNoRecipe = 0x15, kMsgMade = 0x17, kMsgMadeWorkshop = 0x18;
constexpr int16_t kMsgMenuCaption = 0x1e, kMsgChangeCaption = 0x1f, kMsgImproveCaption = 0x20, kMsgStatCaption = 0x21;
constexpr int16_t kMsgDevelopCaption = 0x22;
// Roll's opening message by the tool kit tier (the tier's flag: seen once)
constexpr uint32_t kFlagTierSeen[3] = {0x190, 0x191, 0x192};
constexpr int16_t kMsgTierHigh = 0x1a, kMsgTierMiddleBack = 0x1b, kMsgTierMiddleFromLow = 0x1c, kMsgTierLow = 0x1d;
// the room's script record (Roll) and its sequences
constexpr uint8_t kFadeIn = 0x30, kFadeOut = 0x31;
// the room's states (exe tables 0x8aef80 / 0x8af5d0) and steps
const DataTable<RoutineTable<3>> rollStates{TableId::DevRoomRollStates};
const DataTable<RoutineTable<3>> ninoStates{TableId::DevRoomNinoStates};
constexpr RoutineId kRollStep0 = RoutineId::DevRoomRollStep0, kRollMenuStep = RoutineId::DevRoomRollMenuStep,
                    kNinoStep0 = RoutineId::DevRoomNinoStep0, kNinoMenuStep = RoutineId::DevRoomNinoMenuStep;
constexpr uint8_t kRoomEnded = 2; // the script block's state that ends the room
// the in-stage request: ST04 sub 2 (the room), sub 0 back
constexpr uint8_t kFlutterStage = 0x04, kDevRoomSub = 2, kCabinSub = 0;
constexpr int16_t kDevRoomEntryZ = 0x100;
// Nino's room: the player placed for the menu, Roll turned
constexpr int32_t kNinoMenuX = int32_t(0xff300000), kNinoMenuY = int32_t(0xf8000000), kNinoMenuZ = int32_t(0xfa000000);
constexpr int16_t kNinoRollYaw = 0x8d8, kNinoRollYawBack = 0x800;
constexpr uint8_t kRollModelType = 0x60;
constexpr uint8_t kRollRecordFlag = 0x80; // ST04's first room actor: its +0xd's flag set for the room
constexpr int32_t kMaxPrice = 9999999;
constexpr int32_t kReciprocalOf10 = 0x66666667; // 2^34 / 10 rounded up: (c * it) >> 34 is c / 10
// the two rooms' script-0x30 callbacks (ST04's / ST19's stage setup points script 0x30 at them)
constexpr RoutineId kRollDevRoomCallback = RoutineId::DevRoomRollDevRoomCallback,
                    kNinoDevRoomCallback = RoutineId::DevRoomNinoDevRoomCallback;

// ---- the list / stats panel -----------------------------------------------------------------------------------------

// exe 0x545850(x, y, w, h): the dark frame of the room's list / stats panel (kOtText)
void devFrame(int x, int y, int w, int h) {
    std::vector<ScreenPrim> f;
    FrameStyle st;
    st.corner = 2;
    st.mode = kDevFrameMode;
    st.edge = kDevFrameEdge;
    st.centre = kDevFrameCentre;
    st.outlines = {kDevFrameGreyLine, kDevFrameWhiteLine};
    framePrims(f, x, y, w, h, st);
    const size_t c = menu_draw::otOpen(menu_draw::kOtText);
    for (size_t i = 0; i < f.size(); ++i) {
        if (i == 1) menu_draw::otDrawMode(c, kDevFrameMode); // after the drawing area: the chain's e1 word
        menu_draw::otPut(c, f[i].kind) = f[i];
    }
}
// exe 0x544db0(state, x, y, n): the item / weapon list -- the names (dim in a sub-menu), the hand on its row, the
// scroll bar (exe 0x547ff0), the frame (exe 0x545850)
void devListDraw(const DevRoomMenu& b, int x, int y, int n, std::vector<ScreenPrim>* draw) {
    using namespace menu_draw;
    begin(draw);
    int ry = y + 4; // the rows' y, shifted while the list scrolls (1 / -2: 6 up, 2 / -1: 1 up)
    const int8_t sc = b.list.scrollStep;
    if (sc != 0 && std::abs(int(sc)) <= 2) ry = (sc > 0) == (std::abs(int(sc)) % 3 == 1) ? y - 6 : y - 1;
    const DrawArea area = displayDrawArea();
    otClipDrawArea(otOpen(kOtText));
    const int shown = std::min(n, int(b.list.rows));
    const bool subMenu = b.state != 0;
    const uint32_t dim = subMenu ? kTextPaletteDim : 0;
    for (int k = 0; k <= shown; ++k, ry += 0x10) {
        int32_t i = int32_t(int8_t(b.list.firstRow)) + k;
        if (sc < 0) --i;
        int16_t id = b.listEntries[size_t(uint32_t(i))];
        if (id < 0) continue;
        id = itemString(id);
        bankTextDraw(textPlace(uint32_t(x + 0x1b), uint32_t(ry), dim), menuBanks().names, id, kTextNoRuby);
    }
    if (!subMenu) // OpenDash: the visible rows as the mouse's targets (not while a sub-menu is up)
        for (int k = 0; k < shown && k < int(b.list.visibleRows); ++k)
            otHitRow(otOpen(kOtText), x, y + 4 + k * 0x10, 0x92, 0x10, kListCell + k * 2);
    otClip(otOpen(kOtText), x, area.y + y + 3, 0x92, shown << 4);
    if (b.list.row != kListNoRow) { // the hand (the PC font's glyph 1): bobbing, still and dim in a sub-menu
        const int hx = subMenu ? x + 0xb : x + 0xb + int(devHandBob[(gameState().frameWord() >> 1) & 0xf]);
        const uint32_t c = subMenu ? kDim : kNeutral;
        otGlyph(otOpen(kOtText), hx, int(b.list.row) * 0x10 + 4 + y, 1, c, 0, 0x18);
    }
    listScrollBarDraw(b.list, x + 0x9a, y);
    devFrame(x, y, 0xa8, n * 0x10 + 8);
    end();
}
// exe 0x545b80(state, weapon) (Nino's copy exe 0x548370 over its block reads its own table, same contents): the
// special weapon's turning model of the stats panel. On a change of weapon the old actor is freed and, for a weapon, a
// pool-0x20 actor made (exe 0x5e1160) turned toward the camera; every call it is put in front of the camera (the
// camera's eye + the weapon's place through the camera's angles, exe 0x5e3970)
void devWeaponModel(DevRoomMenu& b, uint32_t weapon) {
    const uint32_t w = weapon & 0xff;
    const uint32_t places = ninoCopy(b) ? stateAddress(TableId::DevRoomNinoModelPlaces) : stateAddress(TableId::DevRoomRollModelPlaces);
    CameraState& camera = cameraState();
    if (int32_t(w) != int32_t(b.modelWeapon)) {
        AnimatedActor* old = b.modelActor.get();
        if (old != nullptr && old->flags != 0) freeActor(*old);
        uint32_t address = 0;
        if (w != 0) {
            address = allocActor20(); // exe 0x5e1160
            if (address != 0) {
                AnimatedActor& a = animatedActorAt(address);
                a.flags = kActorLiveDrawn;
                a.type = kDevModelType;
                a.variant = uint8_t(w - 1u);
                a.pos.x = 0;
                a.pos.y = 0;
                a.pos.z = 0;
                a.rotation.x = kDevModelPitch;
                a.rotation.z = 0;
                a.rotation.y = int16_t((uint16_t(camera.viewAngles.y) + kDevModelYawOffset) & kAngleMask);
            }
        }
        b.modelWeapon = int8_t(w);
        b.modelActor = &animatedActorAt(address);
    }
    if (w != 0 && b.modelActor != nullptr) {
        int32_t o[3];
        const ShortVec3 place = dataShortVec3(places + w * 8); // 8 bytes a weapon
        rotateVector(camera.viewAngles, int32_t(place.x) << 4, int32_t(place.y) << 4, int32_t(place.z) << 4,
                     o); // exe 0x5e3970
        AnimatedActor& a = *b.modelActor;
        a.pos.x.whole = int16_t((o[0] >> 16) + camera.eye.x);
        a.pos.y.whole = int16_t((o[1] >> 16) + camera.eye.y);
        a.pos.z.whole = int16_t((o[2] >> 16) + camera.eye.z);
    }
}
// exe 0x5450c0(state, x, y, item, flags): the special weapon's five stat bars (its levels of its maxima, sliding),
// kPreviewStat | stat: that stat previewed one level up; the captions, the model's box, the frame. Writes the bars'
// state even without `draw`
void devStatsDraw(DevRoomMenu& b, int x, int y, int16_t item, uint16_t flags, std::vector<ScreenPrim>* draw) {
    using namespace menu_draw;
    begin(draw);
    if (item < kFirstWeaponItem || item > kLastWeaponItem) { // no weapon: the captions dimmed and the empty model box
        const size_t c = otOpen(kOtText);
        otDrawMode(c, kStatsMode);
        otSprite(c, x + 0x40, y + 8, kNoWeaponCaptionsArt, 0x20, 0x28, kDim);
        otQuad(c, {x + 6, x + 0x36, x + 6, x + 0x36}, {y + 4, y + 4, y + 0x34, y + 0x34}, kModelBoxColour, kPolyF4);
    } else {
        const int w = item - kFirstWeaponItem;
        devWeaponModel(b, uint32_t(w)); // exe 0x545b80 (Roll's) / exe 0x548370 (Nino's copy)
        const uint32_t sel = flags & 0xf;
        const bool preview = (flags & kPreviewStat) != 0;
        int8_t mx[kStatBars], cur[kStatBars];
        Player& p = player();
        for (uint32_t i = 0; i < uint32_t(kStatBars); ++i) {
            mx[i] = weaponMaxLevels[uint32_t(w) * 8 + i];
            cur[i] = p.weaponLevels[uint32_t(w) * 8 + i];
        }
        // exe 0x5734e0(player, w): the energy at the weapon's energy level; kInfiniteEnergy = infinite
        const bool inf = w != 0 && weaponStat(p, w, WeaponStat::Energy) == kInfiniteEnergy;
        int8_t lvl[kStatBars];
        std::copy(cur, cur + kStatBars, lvl);
        if (preview) {
            const int8_t v = int8_t(lvl[sel] + 1);
            lvl[sel] = mx[sel] < v ? mx[sel] : v;
        }
        uint8_t len[kStatBars];
        for (int i = 0; i < kStatBars; ++i) len[i] = mx[i] == 0 ? 0 : uint8_t((lvl[i] * kDevBarLength) / mx[i]);
        if (inf) len[1] = uint8_t(kDevBarLength);
        if (b.barSlide != 0) b.barSlide += 1u; // the bars slide: the new and the old lengths
        int same = 0;
        while (same < kStatBars && uint8_t(b.barLength[size_t(same)]) == len[same]) ++same;
        if (same != kStatBars) {
            for (size_t i = 0; i < size_t(kStatBars); ++i) {
                b.barPrevious[i] = b.barLength[i];
                b.barLength[i] = int8_t(len[i]);
            }
            b.barSlide = 1;
        }
        if (b.barSlide > 2) {
            b.barSlide = 0;
            for (size_t i = 0; i < size_t(kStatBars); ++i) b.barPrevious[i] = b.barLength[i];
        }
        const size_t c = otOpen(kOtText);
        otDrawMode(c, kStatsMode);
        const int bx = x + 0x60;
        int ry = y, ry2 = y + 0x10;
        for (uint32_t i = 0; i < uint32_t(kStatBars); ++i, ry2 += 8) {
            ry += 8;
            const int16_t nw = b.barLength[i];
            const int bar = nw - (int16_t(nw - b.barPrevious[i]) >> (b.barSlide & 0x1f));
            const int v = (preview && i == sel) ? 0xb8 : 0xa8;
            otQuad(c, {bx, bx + bar, bx, bx + bar}, {ry, ry, ry2, ry2}, kNeutral, kPolyFT4, {0xf0, 0xf1, 0xf0, 0xf1},
                   {v, v, v + 7, v + 7}, kStatsClut, kStatsMode);
            otSprite(c, bx + bar, ry, spriteUv(kStatsClut, 0xf8, uint32_t(v)), 8, 8);
            if (preview && i == sel) { // the level now, over the previewed one
                int o = mx[i] == 0 ? 0 : (cur[i] * kDevBarLength) / mx[i];
                if (inf && sel == 1) o = kDevBarLength;
                otQuad(c, {bx, bx + o, bx, bx + o}, {ry, ry, ry2, ry2}, kNeutral, kPolyFT4, {0xf0, 0xf1, 0xf0, 0xf1},
                       {0xb0, 0xb0, 0xb7, 0xb7}, kStatsClut, kStatsMode);
                otSprite(c, bx + o, ry, kPreviewEndArt, 8, 8);
            }
            if (lvl[i] == mx[i]) {
                if (i == 1 && inf) otSprite(c, bx + 0x16, ry, kInfiniteArt, 0x10, 8); // infinite energy
                else if (mx[i] != 0) otSprite(c, bx + 0x12, ry, kLevelEndArt, 0x18, 8); // the level's end
            }
            const uint32_t col = (preview && i != sel) ? kDim : kNeutral;
            otSprite(c, x + 0x40, ry, kCaptionArt | ((0x80u + i * 8) << 8), 0x20, 8, col); // the stat's caption
        }
        otQuad(c, {x + 6, x + 0x36, x + 6, x + 0x36}, {y + 4, y + 4, y + 0x34, y + 0x34}, kModelBoxColour, kPolyF4);
        // exe 0x545ca0 (Nino's exe 0x548490): the model's picture -- the depth of the point camera + the weapon's
        // place picks the field OT entry of a MoveImage (exe 0x40b710) of the 48 x 48 frame-buffer rect at the screen
        // centre to VRAM, i.e. a grab of the screen centre once the field pass drew the model; then a 48 x 48 sprite of
        // it at (x + 6, y + 4) in kOtOverlay. OpenDash (not the game's): the PS1 grab works because the OT draws the
        // model before the nearer walls; with a depth buffer the room's wall in front of the model hides it
        // (2026-10-15: the box showed only the wall). The model actor is shown here instead as the equipment page's
        // (game/model_preview: a GPU model preview of the box)
        if (b.modelActor != nullptr) {
            const AnimatedActor& a = *b.modelActor;
            if ((a.flags & a.runFlags & kActorDrawn) && a.model != 0) {
                const size_t g = otOpen(kOtOverlay);
                const int32_t centre[3] = {a.pos.x.whole, a.pos.y.whole, a.pos.z.whole};
                modelPreviewDraw(g, {&a}, x + 6 + 0x18, y + 4 + 0x18, kPreviewDevRoom, centre);
            }
        }
    }
    devFrame(x, y, 0xa8, 0x37);
    end();
}

// ---- the menu's helpers ---------------------------------------------------------------------------------------------

int say(int8_t win, int16_t id) { return windowOpen(win, stateAddress(TableId::DevRoomBank), id); } // the room's bank
void close(int8_t win) { windowClose(win, WindowClose::Gentle); }
uint32_t answer(int8_t win) { return messageSystem().answerOf(uint32_t(win)); }
int16_t underCursor(const DevRoomMenu& m) {
    return m.listEntries[size_t(uint32_t(int32_t(int8_t(m.list.firstRow)) + int32_t(m.list.row)))];
}
// the item descriptions (the menu's description bank, id - kItemStringBase) in Roll's window while the list's frame
// shows
void describe(const DevRoomMenu& m, int16_t id) {
    if (m.list.row == kListNoRow || !(windowStatus(kListWindow) & kWindowShowsText) || id < 0) return;
    windowOpen(kRollWindow, menuBanks().descs, itemString(id));
}

// exe 0x544d20(state, kind): the list of the owned ids (their flags): the development items (kDevelopRows shown) or
// the special weapons (kWeaponRows); "back" last; the rows = count + 1
void buildList(DevRoomMenu& m, int kind) {
    uint32_t i, end, base;
    if (kind == kDevelopItems) {
        i = kDevelopFirst;
        end = kDevelopEnd;
        base = kDevelopBase;
        m.list.visibleRows = kDevelopRows;
    } else {
        i = kWeaponFirst;
        end = kWeaponEnd;
        base = kWeaponBase;
        m.list.visibleRows = kWeaponRows;
    }
    int n = 0;
    for (; i < end; ++i)
        if (storyFlag(i + base)) m.listEntries[size_t(n++)] = int16_t(i + base);
    m.listEntries[size_t(n)] = kBackItem;
    m.list.rows = uint8_t(n + 1);
}

// exe 0x547200(state): the room's own list cursor (up / down, L1 / R1 a page; unlike the shop's exe 0x53e330 it pulls
// the first row back to the end of the list when the list shrank); 1 confirmed (circle / start), -1 cancelled
// (cross), 0 otherwise
int cursor(DevRoomMenu& m) {
    ListCursor& l = m.list;
    const int8_t c8 = l.rowStep;
    if (c8 != 0) {
        l.rowStep = int8_t(c8 < 1 ? c8 + 1 : c8 - 1);
        return 0;
    }
    const int8_t c9 = l.scrollStep;
    if (c9 != 0) {
        const int a = c9 < 0 ? -c9 : c9;
        if (a > 2) {
            if (c9 < 1) {
                l.scrollStep = int8_t(c9 + 3);
                --l.firstRow;
            } else {
                l.scrollStep = int8_t(c9 - 3);
                ++l.firstRow;
            }
            return 0;
        }
        if (c9 < 1) {
            l.scrollStep = int8_t(c9 + 1);
            if (int8_t(c9 + 1) == 0) --l.firstRow;
        } else {
            l.scrollStep = int8_t(c9 - 1);
            if (int8_t(c9 - 1) == 0) ++l.firstRow;
        }
        return 0;
    }
    const PadState& pad = pad0();
    if (pad.pressed & kPadCross) {
        soundPlayFlat(UiSound::Cancel);
        return -1;
    }
    const uint8_t rows = l.rows;
    if (rows == 0) {
        l.row = kListNoRow;
        return 0;
    }
    const uint8_t vis = l.visibleRows;
    if (int(rows) < int(int8_t(l.firstRow)) + int(vis)) {
        if (int8_t(l.firstRow) < 1) {
            if (rows <= l.row) l.row = uint8_t(rows - 1u);
        } else {
            const int8_t f = int8_t(rows - vis);
            l.firstRow = uint8_t(f);
            if (f < 0) {
                l.firstRow = 0;
                const uint8_t r = uint8_t(l.row - f);
                l.row = r;
                if (rows <= r) l.row = uint8_t(rows - 1u);
            }
        }
    }
    const uint8_t row0 = l.row;
    const uint16_t u8row = row0;
    const int16_t s9 = int8_t(l.firstRow);
    uint16_t r = u8row;
    int16_t sc = s9;
    const uint16_t held = pad.held, rep = pad.repeated;
    if (!(held & kPadR1) || !(held & kPadL1)) {
        if (rep & kPadR1) {
            if (int32_t(int16_t(u8row)) == int32_t(vis) - 1) sc = int16_t(s9 - 1 + vis);
            else r = uint16_t(vis - 1);
        }
        if (rep & kPadL1) {
            if (r == 0) sc = int16_t(sc + (1 - vis));
            else r = 0;
        }
    }
    if (rep & kPadUp) r = uint16_t(r - 1);
    if (rep & kPadDown) r = uint16_t(r + 1);
    MenuPointer& pt = menuPointer();
    if (pt.moved && pt.row >= kListCell) { // OpenDash: the mouse's row (game/menu_pointer MenuPointer)
        r = uint16_t((pt.row - kListCell) >> 1);
        sc = s9;
        pt.moved = false;
    }
    if (sc < 0) {
        r = 0;
        sc = 0;
    } else if (int16_t(r) < 0) {
        if (int32_t(sc) + int16_t(r) < 0) sc = 0;
        else sc = int16_t(sc - 1);
        r = 0;
    }
    if (vis < rows) {
        if (int32_t(sc) + int16_t(r) < int32_t(rows)) {
            if (int16_t(vis) <= int16_t(r)) {
                r = uint16_t(vis - 1);
                sc = int16_t(sc + 1);
            }
        } else {
            r = uint16_t(vis - 1);
            sc = int16_t((rows - r) - 1);
        }
    } else {
        sc = 0;
        if (int16_t(rows) <= int16_t(r)) r = uint16_t(rows - 1);
    }
    if (r == u8row) {
        if (sc == s9) {
            const bool clicked = pt.click && pt.row == kListCell + int(u8row) * 2; // OpenDash: a click on its row
            pt.click = false;
            if (!(pad.pressed & kPadConfirm) && !clicked) return 0;
            soundPlayFlat(UiSound::Confirm);
            return 1;
        }
        int8_t c = int8_t(int8_t(sc) - int8_t(l.firstRow));
        const int a = c < 0 ? -c : c;
        c = int8_t(a < 2 ? c * 2 : c * 3);
        l.scrollStep = c;
    } else {
        l.row = uint8_t(r);
        l.rowStep = int8_t(uint8_t(uint8_t(r) - row0));
    }
    soundPlayFlat(UiSound::Cursor);
    return 0;
}

// ---- the pages ------------------------------------------------------------------------------------------------------

// exe 0x543cf0: Roll's opening message (the tool kit tier changed: its flag), the caption and her menu (develop /
// change / improve / cancel) -> pages 1..3 or the room's end
void rollMenuPage(DevRoomMenu& m) {
    ScriptState& s = scriptState();
    switch (RollMenuState(m.state)) {
    case RollMenuState::Opening: {
        if (m.modelActor != nullptr) { // the weapon model of the stats panel freed (exe 0x511120: a pool-0x20 actor)
            freeActor(*m.modelActor);
            m.modelActor = nullptr;
        }
        close(kCaptionWindow);
        close(kRollWindow);
        m.openingMessage = -1;
        const int8_t tier = gameState().toolKitTier;
        if (tier == int8_t(BalanceTier::High)) {
            if (!storyFlag(kFlagTierSeen[0])) m.openingMessage = kMsgTierHigh;
        } else if (tier == int8_t(BalanceTier::Middle)) {
            if (!storyFlag(kFlagTierSeen[1])) {
                if (!storyFlag(kFlagTierSeen[0]))
                    m.openingMessage = storyFlag(kFlagTierSeen[2]) ? kMsgTierMiddleFromLow : 0;
                else m.openingMessage = kMsgTierMiddleBack;
            }
        } else if (tier == int8_t(BalanceTier::Low) && !storyFlag(kFlagTierSeen[2])) {
            m.openingMessage = kMsgTierLow;
        }
        if (m.openingMessage < 0) {
            m.state = uint8_t(RollMenuState::Caption);
            return;
        }
        for (uint32_t f : kFlagTierSeen) clearStoryFlag(f);
        if (uint8_t(tier) <= uint8_t(BalanceTier::Low)) setStoryFlag(kFlagTierSeen[uint8_t(tier)]);
        m.state = uint8_t(m.openingMessage != 0 ? RollMenuState::OpeningShow : RollMenuState::Caption);
        return;
    }
    case RollMenuState::OpeningShow:
        if (say(kRollWindow, m.openingMessage) == -1) return;
        m.state = uint8_t(RollMenuState::OpeningWait);
        [[fallthrough]];
    case RollMenuState::OpeningWait:
        if (windowStatus(kRollWindow) == 0) m.state = uint8_t(RollMenuState::Caption);
        return;
    case RollMenuState::Caption:
        if (say(kCaptionWindow, kMsgMenuCaption) == -1) return;
        m.state = uint8_t(RollMenuState::MenuShow);
        [[fallthrough]];
    case RollMenuState::MenuShow:
        if (say(kRollWindow, kMsgRollMenu) == -1) return;
        m.state = uint8_t(RollMenuState::MenuAnswer);
        [[fallthrough]];
    case RollMenuState::MenuAnswer:
        if (windowStatus(kRollWindow) != 0) return;
        switch (answer(kRollWindow)) {
        case 0: m.enterPage(DevPage::Develop); close(kCaptionWindow); return;
        case 1: m.enterPage(DevPage::Change); close(kCaptionWindow); return;
        case 2: m.enterPage(DevPage::Improve); close(kCaptionWindow); return;
        case 3:
        case 4: s.enterState(kRoomEnded); break;
        default: break;
        }
        close(kCaptionWindow);
        return;
    default: return;
    }
}

// exe 0x5448e0: the development items' list (the list's frame message, the caption): an item -> the recipe
void developList(DevRoomMenu& m) {
    auto pick = [&]() {
        buildList(m, kDevelopItems);
        const int r = cursor(m);
        const int16_t id = underCursor(m);
        m.itemUnder = id;
        if (r == 0) return;
        if (id != kBackItem && r > 0) {
            m.state = 1;
            m.step = 0;
        } else {
            m.step = 10;
        }
    };
    switch (m.step) {
    case 0:
        close(kListWindow);
        close(kCaptionWindow);
        m.list.row = 0;
        m.list.firstRow = 0;
        m.clearCursorAnimations();
        buildList(m, kDevelopItems);
        cursor(m);
        if (m.list.rows == 0) m.list.row = kListNoRow;
        m.itemUnder = underCursor(m);
        m.step = 1;
        [[fallthrough]];
    case 1:
        if (say(kListWindow, kMsgListFrame) == -1) return;
        m.step = 2;
        [[fallthrough]];
    case 2:
        if (say(kCaptionWindow, kMsgDevelopCaption) == -1) return;
        m.step = 3;
        [[fallthrough]];
    case 3: pick(); return;
    case 10:
        close(kCaptionWindow);
        close(kListWindow);
        m.enterPage(DevPage::RollMenu);
        return;
    default: return;
    }
}

// exe 0x544a20: the recipe of the item -> Roll's question (its kind), yes: the result's flag set, both items' flags
// cleared; the other item missing: its message; none: kMsgNoRecipe
void developRecipe(DevRoomMenu& m) {
    auto&& item2 = windowRecord(kRollWindow);
    switch (m.step) {
    case 0: {
        for (uint32_t at = stateAddress(TableId::DevRoomRecipes); dataRecord<DevRecipe>(at, DevRecipe::kBytes).result != kRecipesEnd; at += kRecipeSize) {
            const DevRecipe e = dataRecord<DevRecipe>(at, DevRecipe::kBytes);
            int16_t msg;
            if (m.itemUnder == e.itemA) {
                m.recipeOther = e.itemB;
                msg = e.messageA;
            } else if (m.itemUnder == e.itemB) {
                m.recipeOther = e.itemA;
                msg = e.messageB;
            } else {
                continue;
            }
            m.recipeMessage = msg;
            m.recipeKind = e.kind;
            m.recipeResult = e.result;
            if (m.recipeKind == kRecipeOwnMessage) m.recipeMessage = e.messageB;
            if (m.recipeOther != -1 && !storyFlag(uint32_t(int32_t(m.recipeOther)))) {
                m.step = 0x28;
                return;
            }
            m.step = 10;
            return;
        }
        m.step = 0x32;
        return;
    }
    case 10:
        close(kRollWindow);
        close(kRollWindow);
        m.step = 0xb;
        [[fallthrough]];
    case 0xb: {
        int16_t id;
        if (m.recipeKind == kRecipeCanMake) {
            item2.itemName = uint16_t(m.recipeResult);
            id = kMsgCanMake;
        } else if (m.recipeKind == kRecipeOwnMessage) {
            item2.itemName = uint16_t(m.recipeResult);
            id = m.recipeMessage;
        } else {
            item2.itemName = uint16_t(m.recipeOther);
            id = kMsgCombine;
        }
        if (say(kRollWindow, id) == -1) return;
        m.step = m.recipeKind == kRecipeOwnMessage ? 0xe : 0xc;
        return;
    }
    case 0xc: {
        if (windowStatus(kRollWindow) != 0) return;
        const uint32_t a = answer(kRollWindow);
        if (a == 0) m.step = 0xd;
        else if (a <= 2) m.step = 0x1e;
        return;
    }
    case 0xd: {
        const int16_t id =
            (m.recipeKind == kRecipeCanMake || m.recipeKind == kRecipeWorkshop) ? kMsgMadeWorkshop : kMsgMade;
        item2.itemName = uint16_t(m.recipeResult);
        if (say(kRollWindow, id) == -1) return;
        m.step = 0xe;
        [[fallthrough]];
    }
    case 0xe:
        if (windowStatus(kRollWindow) != 0) return;
        setStoryFlag(uint32_t(int32_t(m.recipeResult)));
        clearStoryFlag(uint32_t(int32_t(m.itemUnder)));
        if (m.recipeOther >= 0) clearStoryFlag(uint32_t(int32_t(m.recipeOther)));
        m.step = 0x1e;
        return;
    case 0x1e:
        m.state = 0;
        m.step = 3;
        return;
    case 0x28:
        close(kRollWindow);
        m.step = 0x29;
        [[fallthrough]];
    case 0x29:
        item2.itemName = uint16_t(m.recipeOther);
        if (say(kRollWindow, m.recipeMessage) == -1) return;
        m.step = 0x2a;
        [[fallthrough]];
    case 0x2a:
        if (windowStatus(kRollWindow) == 0) m.step = 0x1e;
        return;
    case 0x32:
        close(kRollWindow);
        m.step = 0x33;
        [[fallthrough]];
    case 0x33:
        if (say(kRollWindow, kMsgNoRecipe) == -1) return;
        m.step = 0x34;
        [[fallthrough]];
    case 0x34:
        if (windowStatus(kRollWindow) == 0) m.step = 0x1e;
        return;
    default: return;
    }
}

// exe 0x544870: page 1, develop (exe 0x5448e0 the list, exe 0x544a20 the recipe), the list (16, 0x40) 6 rows
void develop(DevRoomMenu& m) {
    switch (m.state) {
    case 0: developList(m); break;
    case 1: developRecipe(m); break;
    default: notPortedAt(kDevRoomPort, "development state", m.state);
    }
    devListDraw(m, 0x10, 0x40, kDevelopRows, gDraw);
    describe(m, m.itemUnder);
}

// the weapons' list of pages 2 / 3 (exe 0x543fd0 / 0x544260): its caption (page 2: before the list's frame, page 3
// after)
void weaponList(DevRoomMenu& m, int16_t caption, bool captionFirst) {
    switch (m.step) {
    case 0:
        close(kCaptionWindow);
        close(kRollWindow);
        close(kListWindow);
        buildList(m, 0);
        cursor(m);
        if (m.list.rows == 0) m.list.row = kListNoRow;
        m.list.row = 0;
        m.list.firstRow = 0;
        m.clearCursorAnimations();
        m.modelWeapon = -1;
        m.weaponItem = m.listEntries[0];
        m.step = 1;
        [[fallthrough]];
    case 1:
        if ((captionFirst ? say(kCaptionWindow, caption) : say(kListWindow, kMsgListFrame)) == -1) return;
        m.step = 2;
        [[fallthrough]];
    case 2:
        if ((captionFirst ? say(kListWindow, kMsgListFrame) : say(kCaptionWindow, caption)) == -1) return;
        m.step = 3;
        [[fallthrough]];
    case 3: {
        buildList(m, 0);
        const int r = cursor(m);
        const int16_t id = underCursor(m);
        m.weaponItem = id;
        if (r == 0) return;
        if (id != kBackItem && r > 0) {
            m.state = 1;
            m.step = 0;
        } else {
            m.step = 10;
        }
        return;
    }
    case 10:
        close(kCaptionWindow);
        close(kListWindow);
        m.enterPage(DevPage::RollMenu);
        return;
    default: return;
    }
}

// exe 0x544110: "is this OK?" -> the weapon chosen (its item - the first weapon item), Roll's message
void changeConfirm(DevRoomMenu& m) {
    switch (m.step) {
    case 0:
        close(kRollWindow);
        m.step = 1;
        [[fallthrough]];
    case 1:
        windowRecord(kRollWindow).itemName = uint16_t(m.weaponItem);
        if (say(kRollWindow, kMsgIsThisOk) == -1) return;
        m.step = 2;
        [[fallthrough]];
    case 2: {
        if (windowStatus(kRollWindow) != 0) return;
        const uint32_t a = answer(kRollWindow);
        if (a == 0) {
            m.step = 10;
        } else if (a < 3) {
            m.state = 0;
            m.step = 3;
        }
        return;
    }
    case 10:
        close(kRollWindow);
        m.chosenWeapon = int8_t(uint8_t(uint16_t(m.weaponItem) - uint16_t(kFirstWeaponItem)));
        m.step = 0xb;
        [[fallthrough]];
    case 0xb:
        if (say(kRollWindow, kMsgChanged) == -1) return;
        m.step = 0xc;
        [[fallthrough]];
    case 0xc:
        if (windowStatus(kRollWindow) != 0) return;
        close(kCaptionWindow);
        close(kListWindow);
        m.enterPage(DevPage::RollMenu);
        return;
    default: return;
    }
}

// exe 0x543f50: page 2, change weapon (exe 0x543fd0 the list, exe 0x544110 confirm); the list (16, 0x38) 3 rows, the
// stats (16, 0x75)
void change(DevRoomMenu& m) {
    switch (m.state) {
    case 0: weaponList(m, kMsgChangeCaption, true); break;
    case 1: changeConfirm(m); break;
    default: notPortedAt(kDevRoomPort, "change weapon state", m.state);
    }
    devListDraw(m, 0x10, 0x38, kWeaponRows, gDraw);
    devStatsDraw(m, 0x10, 0x75, m.weaponItem, 0, gDraw);
    if (m.list.row != kListNoRow) describe(m, m.weaponItem);
}

// exe 0x5443e0: the stat to improve (Roll's question with its choice kept in the stat: the bars preview it)
void improveStat(DevRoomMenu& m) {
    auto&& roll = windowRecord(kRollWindow);
    switch (m.step) {
    case 0:
        m.stat = 0;
        m.step = 1;
        [[fallthrough]];
    case 1:
        close(kCaptionWindow);
        close(kRollWindow);
        m.step = 2;
        [[fallthrough]];
    case 2: {
        const int r = say(kRollWindow, kMsgImproveStat);
        roll.flags = (uint32_t(uint8_t(m.stat)) & 0xf) << kWindowChoiceShift |
                     (uint32_t(roll.flags) & ~(0xfu << kWindowChoiceShift));
        if (r == -1) break;
        m.step = 3;
        [[fallthrough]];
    }
    case 3:
        if (say(kCaptionWindow, kMsgStatCaption) == -1) break;
        m.step = 4;
        [[fallthrough]];
    case 4:
        m.stat = int8_t(roll.choice());
        if (windowStatus(kRollWindow) & kWindowShowsText) {
            const uint32_t a = answer(kRollWindow);
            if (a <= kLastStat) {
                m.state = 2;
                m.stat = int8_t(a);
                m.step = 0;
            } else if (a <= 6) {
                m.step = 10;
            }
        }
        break;
    case 10:
        close(kCaptionWindow);
        close(kRollWindow);
        m.state = 0;
        m.step = 2;
        break;
    default: break;
    }
    devStatsDraw(m, 0x10, 0x75, m.weaponItem, uint16_t(int16_t(m.stat) | kPreviewStat), gDraw);
}

// exe 0x544540: the price (the zenny, then the question with the price) -> paid: the stat's level up, kMsgImproved;
// too dear: kMsgTooDear; at the maximum: kMsgAtMaximum
void improvePay(DevRoomMenu& m) {
    const int w = m.weaponItem - kFirstWeaponItem;
    int16_t msg = 0;
    uint8_t c = 0;
    GameState& g = gameState();
    switch (m.step) {
    case 0:
        switch (m.subStep) {
        case 0: {
            close(kRollWindow);
            close(kCaptionWindow);
            const int32_t price = devUpgradePrice(w, m.stat);
            m.price = price;
            if (price < 0) {
                m.enterStep(4); // at the maximum
                goto draw;
            }
            m.subStep = 1;
            break;
        }
        case 1: break;
        case 2: goto ask;
        case 3: goto answered;
        default: goto draw;
        }
        if (say(kCaptionWindow, kMsgZenny) == -1) goto draw;
        ++m.subStep;
    ask:
        windowRecord(kRollWindow).number = m.price; // fb 21's number
        if (say(kRollWindow, kMsgPrice) == -1) goto draw;
        ++m.subStep;
    answered: {
        if (windowStatus(kRollWindow) != 0) goto draw;
        const uint32_t a = answer(kRollWindow);
        if (a == 0) {
            m.enterStep(uint8_t(uint32_t(g.zenny < m.price) + 2u)); // 2 paid, 3 too dear
            goto draw;
        }
        if (a > 2) goto draw;
        goto back;
    }
    case 1:
        close(kCaptionWindow);
        close(kRollWindow);
        m.state = 1;
        m.enterStep(1);
        goto draw;
    case 2:
        c = m.subStep;
        if (c == 0) {
            close(kRollWindow);
            g.zenny = g.zenny - m.price;
            const uint32_t stat = uint8_t(m.stat) < kLastStat ? uint8_t(m.stat) : kLastStat;
            ++player().weaponLevels[uint32_t(w) * 8 + stat];
            m.subStep = uint8_t(c + 1u);
            goto draw;
        }
        if (c == 1) {
            msg = kMsgImproved;
            goto say;
        }
        if (c != 2) goto draw;
        goto wait;
    case 3:
        c = m.subStep;
        if (c == 0) {
            close(kRollWindow);
            m.subStep = 1;
        } else if (c != 1) {
            if (c != 2) goto draw;
            goto wait;
        }
        msg = kMsgTooDear;
    say:
        if (say(kRollWindow, msg) == -1) goto draw;
        ++m.subStep;
        goto wait;
    case 4:
        c = m.subStep;
        if (c == 0) {
            close(kRollWindow);
            m.subStep = 1;
            c = 1;
        }
        if (c == 1) {
            if (say(kRollWindow, kMsgAtMaximum) == -1) goto draw;
            m.subStep = 2;
        } else if (c != 2) {
            goto draw;
        }
        goto wait;
    default: goto draw;
    }
wait:
    if (windowStatus(kRollWindow) != 0) goto draw;
back:
    m.enterStep(1);
draw:
    if (m.step > 1) devStatsDraw(m, 0x10, 0x75, m.weaponItem, 0, gDraw);
    else devStatsDraw(m, 0x10, 0x75, m.weaponItem, uint16_t(int16_t(m.stat) | kPreviewStat), gDraw);
}

// exe 0x544230: page 3, improve weapon (exe 0x544260 the list, exe 0x5443e0 the stat, exe 0x544540 the price); the
// list (16, 0x38) 3 rows
void improve(DevRoomMenu& m) {
    switch (m.state) {
    case 0:
        weaponList(m, kMsgImproveCaption, false);
        devStatsDraw(m, 0x10, 0x75, m.weaponItem, 0, gDraw);
        if (m.list.row != kListNoRow) describe(m, m.weaponItem);
        break;
    case 1: improveStat(m); break;
    case 2: improvePay(m); break;
    default: notPortedAt(kDevRoomPort, "improve weapon state", m.state);
    }
    devListDraw(m, 0x10, 0x38, kWeaponRows, gDraw);
}

// exe 0x543cd0 (the menu step): the page's routine
void menuStep() {
    DevRoomMenu& m = menu();
    switch (DevPage(m.page)) {
    case DevPage::RollMenu: rollMenuPage(m); return;
    case DevPage::Develop: develop(m); return;
    case DevPage::Change: change(m); return;
    case DevPage::Improve: improve(m); return;
    default: notPortedAt(kDevRoomPort, "development room page", m.page);
    }
}

// ---- the room's script ----------------------------------------------------------------------------------------------

// exe 0x546160 (Roll's step 0): Roll's record (the room's script actor), the player under the replay pad and hidden,
// then the next step once the fade is over
void rollStep0() {
    ScriptState& s = scriptState();
    if (s.stepPhase == 0) {
        scriptActorSpawnRecord(stateAddress(TableId::DevRoomRollRecord));
        Player& p = player();
        p.inputMode = kInputReplay;
        p.setDrawn(false);
        ++s.stepPhase;
    }
    if (fadeState().busy == 0) {
        s.stepPhase = 0;
        scriptSequenceSkip(1);
    }
}
// exe 0x5488a0 (Nino's step 0): Roll (the room's second actor) turned toward the menu, the player hidden, then the next
// step once the fade is over
void ninoStep0() {
    ScriptState& s = scriptState();
    if (s.stepPhase == 0) {
        actorAt(roomScriptState().actors[1]).rotation.y = kNinoRollYaw;
        player().setDrawn(false);
        ++s.stepPhase;
    }
    if (fadeState().busy == 0) {
        s.stepPhase = 0;
        scriptSequenceSkip(1);
    }
}
// the step runner (exe 0x53f090): the steps' end ends the room, else the step's routine; the sequence's tick
void stepRunner(RoutineId step0, RoutineId menuRoutine) {
    ScriptState& s = scriptState();
    if (s.steps->id == kScriptStepEnd) {
        s.enterState(kRoomEnded);
    } else {
        const RoutineRef routine = stateRoutine(s.steps->routine);
        if (routine.is(step0)) {
            if (step0 == kRollStep0) rollStep0();
            else ninoStep0();
        } else if (routine.is(menuRoutine)) {
            menuStep();
        } else if (routine) {
            notPortedAt(kDevRoomPort, "development room step", routine.pointerValue());
        }
    }
    scriptSequenceTick();
}
// the set-up's common part: the windows closed, the sub-screen's layout, the player's input and immunity off, no
// event, the replay pad, the block unskippable, the menu cleared, the special weapon and the field position kept
void setUpMenu(DevRoomMenu& m, int8_t specialWeapon) {
    ScriptState& s = scriptState();
    windowCloseAll(WindowClose::Force);
    windowLayoutVariant(WindowLayout::SubScreenShop);
    Player& p = player();
    p.inputMode = 0;
    p.immunity = 0;
    eventState().active = 0;
    scriptReplayPadOn();
    s.flags |= kScriptUnskippable;
    m.clearBlock();
    m.chosenWeapon = specialWeapon;
    m.fieldPoint.x = p.pos.x.whole;
    m.fieldPoint.z = p.pos.z.whole;
    m.modelWeapon = -1;
    m.modelActor = nullptr;
    m.fieldPoint.y = p.pos.y.whole;
    m.fieldYaw = p.rotation.y;
}

// exe 0x545e60(block): Roll's development room in the Flutter (states [0x8aef80 + state * 4])
void rollDevRoom() {
    gCopy = &kRollCopy;
    DevRoomMenu& m = menu();
    ScriptState& s = scriptState();
    TransitionRequest& t = transitionRequest();
    Player& p = player();
    switch (rollStates->idAt(s.state)) {
    case RoutineId::DevRoomRollSetUp: // to sub 2
        switch (s.subState) {
        case 0:
            setUpMenu(m, p.specialWeapon);
            actorHeaderAt(roomScriptState().actors[0]).params[1] |= kRollRecordFlag;
            s.subState = 1;
            [[fallthrough]];
        case 1:
            if (p.playerState() != PlayerState::Playing) return; // exe 0x572300: scripted once it is free
            playerToScripted();
            requestTransition(kTransitionAsked, kFlutterStage, kDevRoomSub, 0, 0, kDevRoomEntryZ, 0, kNoFade, kNoFade);
            s.subState = 2;
            [[fallthrough]];
        case 2:
            if (t.state == kTransitionAccepted) {
                t.state = kTransitionScriptDone;
                s.subState = 3;
            }
            return;
        case 3:
            if (t.state != kTransitionNone) return;
            scriptFade(kFadeIn, 0);
            scriptSequenceStart(stateAddress(TableId::DevRoomRollCamera), stateAddress(TableId::DevRoomRollSteps));
            s.state = 1;
            s.subState = 0;
            s.stepPhase = 0;
            s.stepSubPhase = 0;
            return;
        default: return;
        }
    case RoutineId::ScriptSequenceState: stepRunner(kRollStep0, kRollMenuStep); return;
    case RoutineId::DevRoomRollEnd: // the weapon chosen, back to sub 0
        switch (s.subState) {
        case 0:
            clearStoryFlag(kFlagDevRoomAsked);
            p.specialWeapon = m.chosenWeapon;
            weaponMode(p.weaponMode);
            setupSpecialGauge(true);
            requestPlayerFiles();
            s.subState = 1;
            return;
        case 1:
            if (fileLoads().loading & kFileLoading) return;
            p.stageAnimMarker = 0;
            setupSpecialGauge(true);
            requestTransition(kTransitionAsked, kFlutterStage, kCabinSub, m.fieldPoint.x, m.fieldPoint.y,
                              m.fieldPoint.z, m.fieldYaw, kNoFade, kNoFade);
            windowClose(kListWindow, WindowClose::Gentle);
            windowLayoutVariant(WindowLayout::Dialogue);
            s.subState = 2;
            [[fallthrough]];
        case 2:
            if (t.state == kTransitionAccepted) {
                t.state = kTransitionScriptDone;
                s.subState = 3;
            }
            return;
        case 3:
            if (t.state != kTransitionNone) return;
            scriptFade(kFadeOut, 0);
            p.setDrawn(true);
            scriptReplayPadOff();
            scriptEndBlock();
            return;
        default: return;
        }
    default: notPortedAt(kDevRoomPort, "development room state", s.state);
    }
}

// exe 0x548650(block): Nino's development room (states [0x8af5d0 + state * 4]). Unlike the Flutter's it stays in the
// room (no in-stage request): the player is placed for the menu and put back afterwards
void ninoDevRoom() {
    gCopy = &kNinoCopy;
    DevRoomMenu& m = menu();
    ScriptState& s = scriptState();
    RoomScriptState& room = roomScriptState();
    Player& p = player();
    switch (ninoStates->idAt(s.state)) {
    case RoutineId::DevRoomNinoSetUp: { // the set-up
        const uint8_t sub = s.subState;
        if (sub == 0) {
            // exe 0x5e7dd0: the display buffer copied over the screen for the closing frames (the host keeps the last
            // picture itself, as game/status_screen)
            s.subState = 1;
        } else if (sub != 1) {
            if (sub != 2) return;
        }
        if (sub <= 1) {
            scriptFade(kFadeIn, 0);
            setUpMenu(m, p.specialWeapon);
            if (room.actors[1] != 0 && actorHeaderAt(room.actors[1]).type != kRollModelType) room.actors[1] = 0;
            ++s.subState;
        }
        if (p.playerState() != PlayerState::Playing) return; // exe 0x572300: scripted once it is free
        playerToScripted();
        p.pos.x = kNinoMenuX;
        p.pos.y = kNinoMenuY;
        p.pos.z = kNinoMenuZ;
        p.rotation.y = 0;
        playerScriptAnim(PlayerAnim::Idle, 0, true); // exe 0x5723d0
        scriptSequenceStart(stateAddress(TableId::DevRoomNinoCamera), stateAddress(TableId::DevRoomNinoSteps));
        s.state = 1;
        s.subState = 0;
        s.stepPhase = 0;
        s.stepSubPhase = 0;
        return;
    }
    case RoutineId::ScriptSequenceState: stepRunner(kNinoStep0, kNinoMenuStep); return;
    case RoutineId::DevRoomNinoEnd: // the weapon chosen, the player back where he was
        switch (s.subState) {
        case 0:
            clearStoryFlag(kFlagDevRoomAsked);
            p.specialWeapon = m.chosenWeapon;
            weaponMode(p.weaponMode);
            setupSpecialGauge(true);
            requestPlayerFiles();
            s.subState = 1;
            return;
        case 1:
            if (fileLoads().loading & kFileLoading) return;
            p.stageAnimMarker = 0;
            setupSpecialGauge(true);
            windowCloseAll(WindowClose::Gentle);
            windowLayoutVariant(WindowLayout::Dialogue);
            p.pos.x = int32_t(uint32_t(int32_t(m.fieldPoint.x)) << 16);
            p.pos.y = int32_t(uint32_t(int32_t(m.fieldPoint.y)) << 16);
            p.pos.z = int32_t(uint32_t(int32_t(m.fieldPoint.z)) << 16);
            p.rotation.y = m.fieldYaw;
            s.subState = 2;
            return;
        case 2:
            scriptFade(kFadeOut, 0);
            p.setDrawn(true);
            actorAt(room.actors[1]).rotation.y = kNinoRollYawBack;
            playerBackToPlay();
            scriptReplayPadOff();
            scriptEndBlock();
            return;
        default: return;
        }
    default: notPortedAt(kDevRoomPort, "Nino development room state", s.state);
    }
}

} // namespace

void setDevRoomDraw(std::vector<ScreenPrim>* draw) { gDraw = draw; }

int32_t devUpgradePrice(int w, int stat) {
    const uint32_t k = uint32_t(stat < int(kLastStat) ? stat : int(kLastStat));
    const int8_t lvl = player().weaponLevels[uint32_t(w) * 8 + k], mx = weaponMaxLevels[uint32_t(w) * 8 + k];
    if (mx <= lvl) return -1;
    const DataArray<uint32_t> priceTables(stateAddress(gCopy->prices));
    const DataArray<int32_t> prices(priceTables[uint32_t(w)]);
    int32_t c = prices[uint32_t(stat) + uint32_t(int32_t(lvl)) * 5];
    const int8_t tier = gameState().toolKitTier;
    if (tier == int8_t(BalanceTier::High) || tier == int8_t(BalanceTier::Low)) { // c / -10 or c / 10 by the
        // reciprocal multiply (truncated toward 0): the high dword of c * kReciprocalOf10, >> 2, plus 1 when negative
        const int32_t mul = tier == int8_t(BalanceTier::High) ? -kReciprocalOf10 : kReciprocalOf10;
        const int32_t h = int32_t((int64_t(mul) * int64_t(c)) >> 32);
        c += (h >> 2) - (h >> 31);
    }
    if (c > kMaxPrice) c = kMaxPrice;
    return c;
}

void registerDevRoom(ContentRegistry& r) {
    r.script30Callbacks[kRollDevRoomCallback] = &rollDevRoom;
    r.script30Callbacks[kNinoDevRoomCallback] = &ninoDevRoom;
}

} // namespace od::content
