// The status sub-screen's equipment page (see status_equipment.hpp), one function per exe function.
#include "game/status_equipment.hpp"

#include "engine/data_source.hpp"
#include "engine/screen_prim.hpp"
#include "game/actor_pools.hpp"
#include "game/audio_logic.hpp"
#include "game/display.hpp"
#include "game/items.hpp"
#include "game/menu_banks.hpp"
#include "game/menu_draw.hpp"
#include "game/menu_pointer.hpp"
#include "game/message.hpp"
#include "game/player/player_input.hpp"
#include "game/player/player_state.hpp"
#include "game/player/player_weapons.hpp"
#include "game/stage_files.hpp"
#include "game/stage_state.hpp"
#include "game/status_controls.hpp"
#include "game/status_screen.hpp"
#include "game/status_weapon_model.hpp"
#include "game/story_flags.hpp"

namespace od::port {

using namespace menu_draw;

namespace {

// +1 of the page (exe table 0x91b3d4): the rows (0..2), then the row chosen
enum class EquipState : uint8_t { Enter = 0, Rows = 1, Back = 2, Weapon = 3, Body = 4, Buster = 5 };
// +2 of Weapon (exe table 0x91b3f0): its rows (0..2), the hand / weapon switch, the weapons' list
enum class WeaponSub : uint8_t { Enter = 0, Rows = 1, Back = 2, Switch = 3, List = 4 };
// +3 of Weapon's Rows: the cursor, a refusal's message up
enum class WeaponRowsStep : uint8_t { Cursor = 0, Refused = 1 };
// +3 of Weapon's Switch (exe 0x5eb4b0)
enum class WeaponSwitchStep : uint8_t { Enter = 0, Choose = 1, Apply = 2, WaitMessage = 0xa, Confirmed = 0xb };
// +2 of Body: the slots' rows (0..2), then a slot's list (3 helmet, 4 armour, 5 legs)
enum class BodySub : uint8_t { Enter = 0, Rows = 1, Back = 2, FirstSlot = 3 };
// +2 of Buster: its rows (0..2), the slots, the sorts
enum class BusterSub : uint8_t { Enter = 0, Rows = 1, Back = 2, Slots = 3, Sort = 4 };
// +3 of the buster's Slots: the slot rows (0..2), then the part for a slot (3..5); +3 of Sort: the sort rows (0..2),
// the manual sort (3), the bonus sorts (4..7)
constexpr uint8_t kFirstSlotPart = 3, kManualSort = 3, kFirstBonusSort = 4;
// the lists' steps (+3 / +4): set up, the cursor, back
enum class ListStep : uint8_t { Enter = 0, Cursor = 1, Back = 2 };

// the list kinds (the lists' item id bases / ends by kind)
constexpr uint32_t kKindNone = 0, kKindSpecialWeapon = 1, kKindBusterPart = 2, kKindHelmet = 3, kKindArmour = 4,
                   kKindLegs = 5;
// the lists' flags (exe 0x5ec180 / 0x5ec380)
constexpr uint32_t kListCursorBar = 0x10000;   // the red cursor bar (and the rows as the mouse's targets)
constexpr uint32_t kListDescription = 0x20000; // the row under the cursor's description in window 3
constexpr uint32_t kListNoneRow = 0x40000;     // a "none" row (0) at the end
constexpr uint32_t kListLeaveEquipped = 0x80000; // the equipped values left out
constexpr uint32_t kListFrameChosen = 0x100000;  // the manual sort's chosen row framed
// the summary boxes' flags (exe 0x5ed1a0): & 0xf the box, kBoxAllRows (else the one row of bit kBoxRow << k), the cursor
// bar, two columns
constexpr uint32_t kBoxWeapon = 1, kBoxBuster = 2, kBoxBody = 3;
constexpr uint32_t kBoxRow = 0x100, kBoxAllRows = 0x700, kBoxCursor = 0x10000, kBoxTwoColumns = 0x80000000u;
// the stat bars' flags (exe 0x5eca80): & 0xf the source, kBarsPrevious, kBarsSpecialWeapon
constexpr uint32_t kBarsWeaponLevels = 1, kBarsPartBonus = 2, kBarsPrevious = 0x200, kBarsSpecialWeapon = 0x400;

// the exe's tables
const DataTable<ScalarTable<int16_t, 6>> listBases{TableId::StatusEquipmentListBases}; // the item id base of each list kind
const DataTable<ScalarTable<int16_t, 6>> listEnds{TableId::StatusEquipmentListEnds}; // the values' end of each kind (the buster parts: their count)
const DataTable<ScalarTable<uint8_t, 4>> equipRowStates{TableId::StatusEquipmentEquipRowStates};
const DataTable<ScalarTable<uint8_t, 3>> weaponRowSubs{TableId::StatusEquipmentWeaponRowSubs};
const DataTable<ScalarTable<uint8_t, 4>> bodyRowSubs{TableId::StatusEquipmentBodyRowSubs};
const DataTable<ScalarTable<uint8_t, 3>> busterRowSubs{TableId::StatusEquipmentBusterRowSubs};
const DataTable<ScalarTable<uint8_t, 4>> busterSlotSteps{TableId::StatusEquipmentBusterSlotSteps};
const DataTable<ScalarTable<uint8_t, 6>> busterSortSteps{TableId::StatusEquipmentBusterSortSteps};
const DataTable<ScalarTable<uint8_t, 0x10>> armourHitHeights{TableId::StatusEquipmentArmourHitHeights}; // the hit height level of each armour
// the buster parts' bonuses (8 bytes each: the five stats)
struct PartBonus : DataRecord {
    std::array<uint8_t, 5> stats{};
    static PartBonus load(const TableReader& r) {
        PartBonus rec;
        for (size_t i = 0; i < 5; ++i) rec.stats[i] = r.u8(i);
        return rec;
    }
};
const DataTable<RecordTable<PartBonus, 0x20, 8>> partBonuses{TableId::StatusEquipmentPartBonuses};
// the special weapon's stat captions (9 rows: RowArt with the texture word)
const DataTable<RecordTable<RowArtRecord, 9, 12>> statCaptions{TableId::StatusEquipmentStatCaptions};
// the summary boxes' rows (by the box's variant: {u8 x, u8 y, s16 the icon's kind}) and their tabs ({u8 u, u8 v, u16 w})
struct BoxPlace : DataRecord {
    uint8_t x = 0, y = 0;
    int16_t icon = 0;
    static BoxPlace load(const TableReader& r) {
        BoxPlace rec;
        rec.x = r.u8(0);
        rec.y = r.u8(1);
        rec.icon = r.s16(2);
        return rec;
    }
};
const DataTable<RecordTable<BoxPlace, 0x10, 4>> boxPlaces{TableId::StatusEquipmentBoxPlaces};
struct BoxTab : DataRecord {
    uint8_t u = 0, v = 0;
    uint16_t w = 0;
    static BoxTab load(const TableReader& r) {
        BoxTab rec;
        rec.u = r.u8(0);
        rec.v = r.u8(1);
        rec.w = r.u16(2);
        return rec;
    }
};
const DataTable<RecordTable<BoxTab, 0x10, 4>> boxTabs{TableId::StatusEquipmentBoxTabs};
// the art (tpage 7 / 0xd)
constexpr uint32_t kListTabArt = 0x7f1160c0, kBarArt = 0x7f1100b8, kPreviousBarArt = 0x7f1148b8;
constexpr uint32_t kInfiniteArt = 0x7f117090, kLevelEndArt = 0x7f117890, kBarsCaptionArt = 0x7f114090;
constexpr uint32_t kFlameBarrierArt = 0x7f129000, kLightBarrierArt = 0x7f129020;
constexpr uint16_t kPageMode = 7, kIconMode = 0x0d;
constexpr uint16_t kBarClut = 0x7f11;
// the summary's names when a slot is empty, the buster's two-column extra row
constexpr int16_t kNoPart = 0x101, kNoHelmet = 0x102, kNoArmour = 0x103, kNoLegs = 0x104, kBusterExtraRow = 0x59;
// the names of the parts / the body parts (their string id: the value + these)
constexpr int16_t kBusterPartNames = 0x14, kHelmetNames = 0x38, kArmourNames = 0x3c, kLegNames = 0x48;
constexpr int kEquipmentPage = 2; // the page tables' index
constexpr int8_t kMessageWindow = 1, kNoWeaponWindow = 2, kDescriptionWindow = 3;
// the weapon rows' refusals (MenuBanks::misc): a carried object / lifting, no special weapon, the player's variant
constexpr int16_t kRefusedBusy = 1, kRefusedVariant = 2, kRefusedNoWeapon = 3, kRefusedViewNoWeapon = 4;
constexpr int16_t kNoWeaponMessage = 0;
constexpr uint32_t kFirstOwnedWeapon = 3, kWeaponsEnd = 0x12; // the special weapons the switch looks for
constexpr int kBarLength = 0x38;
constexpr int8_t kMaxBusterLevel = 7;
constexpr uint8_t kManualSortHelp = 6, kManualSortSwapHelp = 7;

// the list kind's item id base
uint32_t listBase(uint32_t kind) { return uint32_t(int32_t(listBases[kind])); }
// the equipped values of a list kind: their count (-1: no such kind)
int equipped(uint32_t kind, uint8_t eq[3]) {
    Player& p = player();
    eq[0] = eq[1] = eq[2] = 0;
    switch (kind) {
    case kKindNone: return 0;
    case kKindSpecialWeapon: eq[0] = uint8_t(p.specialWeapon); return 1;
    case kKindBusterPart:
        for (size_t i = 0; i < 3; ++i) eq[i] = uint8_t(p.busterParts[i]);
        return 3;
    case kKindHelmet: eq[0] = uint8_t(p.helmetPart); return 1;
    case kKindArmour: eq[0] = uint8_t(p.armourPart); return 1;
    case kKindLegs: eq[0] = uint8_t(p.legPart); return 1;
    default: return -1;
    }
}
// whether v is one of the n equipped parts eq (the list marks it; read from the last slot down)
bool isEquipped(const uint8_t eq[3], int n, uint32_t v) {
    for (int e = n - 1; e >= 0; --e)
        if (eq[e] == v) return true;
    return false;
}
// the one-column list's entry under the cursor (the row + the scroll)
size_t listEntryUnderCursor(const StatusScreen& s) { return size_t(uint32_t(s.listScroll) + s.listRow); }

// exe 0x5ec180(block, visible, kind, flags): the list of a kind (zero-filled): the buster parts owned (in their order,
// up to their count or a 0), the others the values whose owned flag is set (from 3 for the special weapons, 1 else);
// kListLeaveEquipped, kListNoneRow; the rows / visible rows
void kindListBuild(StatusScreen& s, uint8_t vis, uint32_t kind, uint32_t flags) {
    kind &= 0xffff;
    uint8_t eq[3];
    const int ne = equipped(kind, eq);
    if (ne < 0) return;
    const int start = kind == kKindSpecialWeapon ? 3 : kind == kKindNone ? 0 : 1;
    StatusList& list = statusList();
    list.clearWords();
    uint32_t n = 0;
    Player& p = player();
    if (kind == kKindBusterPart) {
        const int cnt = listEnds[kKindBusterPart];
        for (int i = 0; i < cnt; ++i) {
            const uint8_t v = p.ownedParts[size_t(i)];
            if ((flags & kListLeaveEquipped) && isEquipped(eq, ne, v)) continue;
            if (v == 0) break;
            list.entries[n++] = v;
        }
    } else {
        const int end = listEnds[kind];
        for (int u = start; u < end; ++u) {
            if (!storyFlag(listBase(kind) + kSpecialWeaponItems + uint32_t(u))) continue;
            if ((flags & kListLeaveEquipped) && isEquipped(eq, ne, uint32_t(u))) continue;
            list.entries[n++] = uint16_t(u);
        }
    }
    if (flags & kListNoneRow) list.entries[n++] = 0;
    s.listRows = uint8_t(n);
    s.listVisible = vis;
}
// exe 0x5ec380(block, x, y, visible, kind, flags): the list in a frame -- the kind's tab, the names clipped to the rows
// while they scroll, the icon of the equipped ones (unless kListLeaveEquipped), the manual sort's chosen row framed,
// the description, the cursor bar, the scroll bar
void kindListDraw(StatusScreen& s, int x, int y, int vis, uint32_t kind, uint32_t flags) {
    kindListBuild(s, uint8_t(vis), kind, flags);
    kind &= 0xffff;
    uint8_t eq[3];
    const int ne = equipped(kind, eq);
    if (ne < 0) return;
    frameDraw(x, y, 0xb0, vis * 0x10 + 8, kFrameRound);
    if (kind > kKindSpecialWeapon && kind <= kKindLegs) {
        const size_t c = otOpen(kOtRows);
        otDrawMode(c, kPageMode);
        otSprite(c, x + 8, y - 3, kListTabArt, 0x30, 8);
    }
    int ty = y + 4;
    const int a = s.scrollStep;
    if (a != 0 && (a < 0 ? -a : a) < 3) {
        const int m = (a < 0 ? -a : a) % 3;
        if (a < 1) ty = m == 1 ? y - 1 : m == 2 ? y - 6 : ty;
        else ty = m == 1 ? y - 6 : m == 2 ? y - 1 : ty;
    }
    const DrawArea area = displayDrawArea();
    otClipDrawArea(otOpen(kOtText));
    const int rows = s.listRows < vis ? s.listRows : vis;
    const uint32_t base = listBase(kind);
    StatusList& list = statusList();
    for (int k = 0; k < rows + 1; ++k, ty += 0x10) {
        int idx = int(s.listScroll) + k;
        if (s.scrollStep < 0) --idx;
        const uint16_t v = list.entries[size_t(uint32_t(idx))];
        int id = int(base) + int16_t(v);
        if (id >= 0) bankTextDraw(textPlace(uint32_t(x + 0x18), uint32_t(ty + 2)), menuBanks().names, itemString(id),
                                  kTextNoRuby);
        if (!(flags & kListLeaveEquipped) && v != 0 && ne != 0)
            for (int e = 0; e < ne; ++e)
                if (v == eq[e]) {
                    const uint32_t icon = kind >= kKindBusterPart && kind <= kKindLegs ? kind - 1 : 0;
                    const size_t c = otOpen(kOtText);
                    otDrawMode(c, kIconMode);
                    artBegin(uint16_t(kVecItemIcon + icon));
                    otSprite(c, x + 8, ty, itemIconArt(icon), 0x10, 0x10, kNeutral, kSprite16);
                    artEnd();
                }
        if ((flags & kListFrameChosen) && s.item == idx) {
            const size_t c = otOpen(kOtText);
            otHLine(c, x + 0x17, x + 0x90, ty + 2);
            otVLine(c, x + 0x90, ty + 2, ty + 0xf);
            otVLine(c, x + 0x17, ty + 2, ty + 0xf);
            otHLine(c, x + 0x17, x + 0x90, ty + 0xf);
        }
    }
    otClip(otOpen(kOtText), x + 8, area.y + y + 5, 0x94, (rows << 4) - 1);
    if (flags & kListDescription) {
        const int id = int16_t(list.entries[listEntryUnderCursor(s)]) + int(base);
        if (id >= 0) menuWindowOpen(kDescriptionWindow, menuBanks().descs, itemString(id));
    }
    if ((flags & kListCursorBar) && s.cursorShown != 0 && s.listRows != 0)
        otTile(otOpen(kOtRows), x + 0x17, int(s.listRow) * 0x10 + 6 + y, 0x7a, 0xe, barPulse(), kTileOpaque);
    if (flags & kListCursorBar) // OpenDash: the rows as the mouse's targets
        for (int k = 0; k < rows; ++k) otHitRow(otOpen(kOtRows), x + 8, y + 5 + k * 0x10, 0x98, 0x10, kListCell + k * 2);
    statusScrollBarDraw(s, x + 0xa0, y);
}
// exe 0x5ed1a0(block, x, y, rows, flags): a summary box -- kBoxWeapon the special weapon (the hand / the weapon; the one
// not in use dim), kBoxBuster the buster's part slots (the third with its item; two columns + the extra row), kBoxBody
// the helmet / armour / legs; kBoxAllRows all rows (kBoxCursor the cursor bar and the row's description in window 3),
// else the one row whose bit kBoxRow << k is set
void summaryBoxDraw(StatusScreen& s, int x, int y, int16_t rows, uint32_t flags) {
    if (rows <= 0) return;
    const bool half = (flags & kBoxTwoColumns) != 0;
    frameDraw(x, y, half ? 0x120 : 0xb0, (half ? (rows + 1) / 2 : rows) * 0x10 + 8, kFrameRound);
    uint32_t var = 0;
    if ((flags & 0xf) == kBoxWeapon) {
        var = 4;
    } else {
        if ((flags & 0xf) == kBoxBody) var = 2;
        if ((flags & kBoxAllRows) != kBoxAllRows) ++var;
    }
    {
        const size_t c = otOpen(kOtRows);
        otDrawMode(c, kPageMode);
        const BoxTab t = boxTabs[var];
        labelNext(kLblEquipHead, uint16_t(var));
        otSprite(c, x + 8, y - 3, spriteUv(0x7f11, t.u, t.v), t.w, 8);
    }
    Player& p = player();
    int16_t ids[4] = {0, 0, 0, 0};
    uint8_t dim[4] = {0, 0, 0, 0};
    int n = 0;
    switch (flags & 0xf) {
    case kBoxWeapon:
        n = 2;
        var = 0;
        if (uint8_t(p.specialWeapon) != 0) ids[1] = p.specialWeapon;
        if (p.specialAllowed == 0) dim[1] = kTextPaletteDim;
        else dim[0] = kTextPaletteDim;
        break;
    case kBoxBuster: {
        const bool third = storyFlag(kThirdSlotItem);
        n = third ? 3 : 2;
        var = third ? 7 : 5;
        for (int k = 0; k < n; ++k) {
            const int8_t part = p.busterParts[size_t(k)];
            ids[k] = part == 0 ? kNoPart : int16_t(part + kBusterPartNames);
        }
        if (half) {
            var += 5;
            if (storyFlag(kThirdSlotItem)) {
                n = 4;
                ids[3] = kBusterExtraRow;
            }
        }
        break;
    }
    case kBoxBody:
        n = 3;
        var = 2;
        ids[0] = uint8_t(p.helmetPart) == 0 ? kNoHelmet : int16_t(p.helmetPart + kHelmetNames);
        ids[1] = uint8_t(p.armourPart) == 0 ? kNoArmour : int16_t(p.armourPart + kArmourNames);
        ids[2] = uint8_t(p.legPart) == 0 ? kNoLegs : int16_t(p.legPart + kLegNames);
        break;
    default:
        break;
    }
    auto name = [&](int k, int tx, int ty) {
        int id = ids[k];
        if (id < 0) return;
        bankTextDraw(textPlace(uint32_t(tx), uint32_t(ty), dim[k]), menuBanks().names, itemString(id), kTextNoRuby);
    };
    if ((flags & kBoxAllRows) == kBoxAllRows) {
        for (int k = 0; k < n; ++k) {
            const BoxPlace e = boxPlaces[var + uint32_t(k)];
            name(k, e.x + 0x10 + x, e.y + 2 + y);
        }
        const size_t c = otOpen(kOtRows);
        for (int k = 0; k < n; ++k)
            if ((dim[k] & kTextPaletteDim) == 0) {
                const BoxPlace e = boxPlaces[var + uint32_t(k)];
                otDrawMode(c, kIconMode);
                artBegin(uint16_t(kVecItemIcon + e.icon));
                otSprite(c, e.x + x, e.y + y, itemIconArt(uint32_t(int32_t(e.icon))), 0x10, 0x10, kNeutral, kSprite16);
                artEnd();
            }
        if (flags & kBoxCursor) {
            const int cur = s.cursor;
            const BoxPlace e = boxPlaces[var + uint32_t(cur)];
            otTile(c, e.x + 0xf + x, e.y + 1 + y, 0x7a, 0xe, barPulse(), kTileOpaque);
            const int id = ids[cur];
            if (id >= 0) menuWindowOpen(kDescriptionWindow, menuBanks().descs, itemString(id));
        }
        otDrawMode(c, kPageMode);
        return;
    }
    int k = 0;
    while (k < n && ((flags >> k) & kBoxRow) == 0) ++k;
    if (k == n) return;
    name(k, x + 0x18, y + 6);
    const size_t c = otOpen(kOtRows);
    otDrawMode(c, kIconMode);
    const BoxPlace e = boxPlaces[var + uint32_t(k)];
    artBegin(uint16_t(kVecItemIcon + e.icon));
    otSprite(c, x + 8, y + 4, itemIconArt(uint32_t(int32_t(e.icon))), 0x10, 0x10, kNeutral, kSprite16);
    artEnd();
}
// exe 0x5eca80(block, x, y, weapon, flags): the stat bars -- kBarsWeaponLevels the weapon's levels, kBarsPartBonus the
// buster part's bonuses; kBarsSpecialWeapon the special weapon (five bars of its maxima, the model box, the infinite
// energy), else four of 7; kBarsPrevious the previous buster levels under the new ones. The bars move to a new value
// over two frames (the lengths, the old lengths, the slide). No weapon: window 2 says so
void statBarsDraw(StatusScreen& s, int x, int y, int16_t w, uint32_t flags) {
    const bool weapon = (flags & kBarsSpecialWeapon) != 0;
    int8_t mx[5] = {kMaxBusterLevel, kMaxBusterLevel, kMaxBusterLevel, kMaxBusterLevel, kMaxBusterLevel};
    int n = 4;
    bool inf = false;
    Player& p = player();
    if (!weapon) {
        frameDraw(x, y, 0x68, 0x30, kFrameRound);
    } else {
        weaponModelSet(s, uint8_t(w));
        frameDraw(x, y, 0xb0, 0x38, kFrameRound);
        n = 5;
        for (size_t i = 0; i < 5; ++i) mx[i] = weaponMaxLevels[size_t(uint32_t(int32_t(w))) * 8 + i];
        x += 0x40;
        if (w != 0 && weaponStat(p, int(int32_t(w)), WeaponStat::Energy) == 0x7fff) inf = true; // exe 0x5734e0
    }
    int8_t cur[5];
    if ((flags & 0xf) == kBarsPartBonus) {
        for (size_t i = 0; i < 5; ++i) cur[i] = int8_t(partBonuses[size_t(uint32_t(int32_t(w)))].stats[i]);
    } else {
        if (w == 0) {
            menuWindowOpen(kNoWeaponWindow, menuBanks().misc, kNoWeaponMessage);
            for (size_t i = 0; i < 5; ++i) {
                s.barLength[i] = 0;
                s.barPrevious[i] = 0;
            }
            return;
        }
        for (size_t i = 0; i < 5; ++i) cur[i] = p.weaponLevels[size_t(uint32_t(int32_t(w)) * 8 + i)];
    }
    const size_t c = otOpen(kOtRows);
    otDrawMode(c, kPageMode);
    int8_t len[5];
    for (int i = 0; i < 5; ++i) len[i] = mx[i] == 0 ? 0 : int8_t((cur[i] * kBarLength) / mx[i]);
    if (inf) len[1] = int8_t(kBarLength);
    if (s.barSlide != 0) s.barSlide = int16_t(s.barSlide + 1);
    int i = 0;
    while (s.barLength[size_t(i)] == len[i])
        if (++i > 4) break;
    if (i <= 4) {
        for (size_t k = 0; k < 5; ++k) {
            s.barPrevious[k] = s.barLength[k];
            s.barLength[k] = len[k];
        }
        s.barSlide = 1;
    }
    if (uint16_t(s.barSlide) > 2) {
        s.barSlide = 0;
        for (size_t k = 0; k < 5; ++k) s.barPrevious[k] = s.barLength[k];
    }
    const int bx = x + 0x24;
    const int vv = (flags & kBarsPrevious) ? 0x50 : 0x40;
    for (int k = 0, yy = y + 8; k < n; ++k, yy += 8) {
        const int16_t nv = s.barLength[size_t(k)];
        const int16_t v = int16_t(nv - (int16_t(nv - s.barPrevious[size_t(k)]) >> (uint16_t(s.barSlide) & 0x1f)));
        otQuad(c, {bx, bx + v, bx, bx + v}, {yy, yy, yy + 8, yy + 8}, kNeutral, kPolyFT4, {0xb0, 0xb1, 0xb0, 0xb1},
               {vv, vv, vv + 7, vv + 7}, kBarClut, kPageMode);
        otSprite(c, bx + v, yy, kBarArt | uint32_t(vv) << 8, 8, 8);
        if (!(flags & kBarsPrevious) && cur[k] == mx[k]) {
            if (k == 1 && inf) otSprite(c, bx + 0x16, yy + 1, kInfiniteArt, 0x10, 8);
            else if (mx[k] != 0) otSprite(c, bx + 0x12, yy + 1, kLevelEndArt, 0x18, 8);
        }
    }
    if (flags & kBarsPrevious)
        for (int k = 0, yy = y + 8; k < 4; ++k, yy += 8) {
            const int l = mx[k] == 0 ? 0 : (s.savedBusterLevels[size_t(k)] * kBarLength) / mx[k];
            otQuad(c, {bx, bx + l, bx, bx + l}, {yy, yy, yy + 8, yy + 8}, kNeutral, kPolyFT4, {0xb0, 0xb1, 0xb0, 0xb1},
                   {0x48, 0x48, 0x4f, 0x4f}, kBarClut, kPageMode);
            otSprite(c, bx + l, yy, kPreviousBarArt, 8, 8);
        }
    if (!weapon) {
        otSprite(c, x + 4, y + 8, kBarsCaptionArt, 0x20, 0x20);
        return;
    }
    for (size_t e = 0; e < statCaptions.size(); ++e) {
        const RowArtRecord& a = statCaptions[e];
        otSprite(c, a.x + x - 0x36, a.y + y + 4, uint32_t(a.clut) << 16 | a.uv, a.w, a.h);
    }
    otSprite(c, x + 4, y + 8, kBarsCaptionArt, 0x20, 0x28);
}
// exe 0x5ed7f0(x, y): the leg key items' box (their pictures while owned)
void legItemsDraw(int x, int y) {
    frameDraw(x, y, 0x54, 0x30, kFrameRound);
    const size_t c = otOpen(kOtRows);
    otDrawMode(c, kPageMode);
    if (storyFlag(kFlameBarrierItem)) otSprite(c, x + 8, y + 8, kFlameBarrierArt, 0x20, 0x20);
    if (storyFlag(kLightBarrierItem)) otSprite(c, x + 0x2c, y + 8, kLightBarrierArt, 0x20, 0x20);
    otDrawMode(c, kPageMode);
}
// exe 0x5ed910(slots): the buster's levels = the sum of the three parts' bonuses, at most 7
void busterLevelsSum(const uint8_t slots[3]) {
    Player& p = player();
    const size_t b = weaponLevelIndex(kBusterLevelWeapon, WeaponStat::Attack);
    for (size_t k = 0; k < 5; ++k) p.weaponLevels[b + k] = 0;
    for (int s = 0; s < 3; ++s)
        for (size_t k = 0; k < 5; ++k)
            p.weaponLevels[b + k] = int8_t(uint8_t(p.weaponLevels[b + k]) + partBonuses[slots[s]].stats[k]);
    for (size_t k = 0; k < 5; ++k)
        if (p.weaponLevels[b + k] > kMaxBusterLevel) p.weaponLevels[b + k] = kMaxBusterLevel;
}
// the stat bars' state cleared
void barsReset(StatusScreen& s) {
    for (size_t i = 0; i < 5; ++i) {
        s.barLength[i] = 0;
        s.barPrevious[i] = 0;
    }
    s.clearBarSlideBytes();
}
// a confirm on a list (the cursor's answer)
bool confirmed() { return (menuPadPressed() & kPadConfirm) != 0; }
// exe 0x5eb1b0: the rows; the page's model bank (the DAT's type-1 bank of the other buffer) walked in
void equipRows(StatusScreen& s) {
    const EquipState st = EquipState(s.state);
    StageState& stg = stage();
    if (st == EquipState::Enter) {
        barsReset(s);
        s.switching = 0;
        stg.bankBuffer ^= 1u;
        pageBankIn(statusBankWalk());
        cursorReset(s);
        s.state = uint8_t(EquipState::Rows);
    } else if (st == EquipState::Rows) {
        menuCursorStep(s, 1);
        if (confirmed()) s.state = equipRowStates[size_t(int32_t(s.cursor))];
    } else if (st == EquipState::Back) {
        stg.bankBuffer = s.savedBankBuffer;
        statusBankWalk();
        pageBankOut();
        levelBack(s, 1);
    }
    summaryBoxDraw(s, 0x80, 0x26, 2, kBoxAllRows | kBoxWeapon);
    summaryBoxDraw(s, 0x80, 0x52, 3, kBoxAllRows | kBoxBody);
    summaryBoxDraw(s, 0x10, 0x8c, 3, kBoxTwoColumns | kBoxAllRows | kBoxBuster);
    frameDraw(0xc, 0xb1, 0x128, 0x27, kFrameRinged);
}
// exe 0x5eb310: the special weapon's rows (the hand / weapon switch, the list, back): the switch needs the player's
// variant 0, a special weapon owned and nothing carried / lifted; the list a special weapon
void weaponRows(StatusScreen& s) {
    Player& p = player();
    if (s.sub == uint8_t(WeaponSub::Enter)) {
        barsReset(s);
        s.switching = 0;
        cursorReset(s);
        s.sub = uint8_t(WeaponSub::Rows);
        s.modelWeapon = 0xff;
        s.modelActor = nullptr;
        // exe 0x40de70({0, 0, 0}, 0x400, 0, 0, 0): the model's view -- not written: the drawing (game/model_preview)
        // uses that camera's constants itself, the field camera globals stay the host's
    } else if (s.sub == uint8_t(WeaponSub::Rows)) {
        if (s.step == uint8_t(WeaponRowsStep::Cursor)) {
            menuCursorStep(s, 2);
            if (confirmed()) {
                const uint8_t to = weaponRowSubs[size_t(int32_t(s.cursor))];
                s.step = 0;
                s.sub = to;
                uint32_t i = kFirstOwnedWeapon;
                while (i < kWeaponsEnd && !storyFlag(i + kSpecialWeaponItems)) ++i;
                int16_t msg = -1;
                if (to == uint8_t(WeaponSub::Switch)) {
                    if (p.variant != 0) msg = kRefusedVariant;
                    else if (i == kWeaponsEnd) msg = kRefusedNoWeapon;
                    else if (p.carryFlags != 0 || p.subState == uint8_t(PlayerSub::Lift)) msg = kRefusedBusy;
                } else if (to == uint8_t(WeaponSub::List) && i == kWeaponsEnd) {
                    msg = kRefusedViewNoWeapon;
                }
                if (msg >= 0) {
                    menuWindowOpen(kMessageWindow, menuBanks().misc, msg);
                    s.sub = uint8_t(WeaponSub::Rows);
                    s.step += 1;
                }
            }
        } else if (windowStatus(kMessageWindow) == 0) {
            s.step = uint8_t(WeaponRowsStep::Cursor);
        }
    }
    summaryBoxDraw(s, 0x80, 0x36, 2, kBoxAllRows | kBoxWeapon);
    statBarsDraw(s, 0x80, 0x72, p.specialWeapon, kBarsSpecialWeapon | kBarsWeaponLevels);
    if (s.sub == uint8_t(WeaponSub::Back)) {
        levelBack(s, 2);
        if (s.modelActor != nullptr) { // the model actor freed (exe 0x5e1490)
            freeActor(*s.modelActor);
            s.modelActor = nullptr;
        }
    }
}
// exe 0x5eb4b0: the hand / special weapon switch, applied by the room's weapon mode
void weaponSwitch(StatusScreen& s) {
    Player& p = player();
    switch (WeaponSwitchStep(s.step)) {
    case WeaponSwitchStep::Enter:
        cursorReset(s);
        s.slideIn = 0;
        s.step = uint8_t(WeaponSwitchStep::Choose);
        s.cursorKept = p.specialAllowed;
        s.cursor = int8_t(p.specialAllowed);
        break;
    case WeaponSwitchStep::Choose:
        s.listScroll = 0;
        s.listRow = 0;
        menuCursorStep(s, 3);
        p.specialAllowed = s.cursor == 0 ? 0u : 1u;
        if (!confirmed()) break;
        [[fallthrough]];
    case WeaponSwitchStep::Confirmed:
        s.step = uint8_t(WeaponSwitchStep::Apply);
        break;
    case WeaponSwitchStep::Apply:
        weaponMode(p.weaponMode);
        levelBack(s, 3);
        break;
    case WeaponSwitchStep::WaitMessage:
        if (windowStatus(kMessageWindow) == 0) s.step += 1;
        break;
    default:
        break;
    }
    summaryBoxDraw(s, 0x80, 0x36, 2, kBoxCursor | kBoxAllRows | kBoxWeapon);
    statBarsDraw(s, 0x80, 0x72, p.specialWeapon, kBarsSpecialWeapon | kBarsWeaponLevels);
    s.cursor = int8_t(s.cursorKept);
}
// exe 0x5eb5d0: the special weapons owned (view only; the cursor on the equipped one)
void weaponList(StatusScreen& s) {
    Player& p = player();
    StatusList& list = statusList();
    const ListStep st = ListStep(s.step);
    if (st == ListStep::Enter) {
        cursorReset(s);
        s.slideIn = 0;
        s.step = uint8_t(ListStep::Cursor);
        kindListBuild(s, 3, kKindSpecialWeapon, kListCursorBar | kListDescription);
        const uint32_t n = s.listRows;
        for (uint32_t i = 0; i < n; ++i)
            if (int16_t(list.entries[i]) == int16_t(p.specialWeapon)) {
                const uint8_t vis = s.listVisible;
                if (int(i) < int(vis)) {
                    s.listRow = uint8_t(i);
                    s.listScroll = 0;
                } else if (int(i) < int(n) - int(vis)) {
                    s.listRow = 0;
                    s.listScroll = uint8_t(i);
                } else {
                    s.listScroll = uint8_t(s.listRows - vis);
                    s.listRow = uint8_t(i - (s.listRows - vis));
                }
                break;
            }
    } else if (st == ListStep::Cursor) {
        if (oneColumnListStep(s) == kAnswerCancel) s.step = uint8_t(ListStep::Back);
    } else if (st == ListStep::Back) {
        levelBack(s, 3);
    }
    kindListDraw(s, 0x80, 0x36, 3, kKindSpecialWeapon, kListCursorBar | kListDescription);
    statBarsDraw(s, 0x80, 0x72, int16_t(list.entries[listEntryUnderCursor(s)]), kBarsSpecialWeapon | kBarsWeaponLevels);
    s.cursor = int8_t(s.cursorKept);
}
// exe 0x5eb2b0: the special weapon (its rows, the switch, the list), its rows' table, the frame, the model
void equipWeapon(StatusScreen& s) {
    const uint8_t sub = s.sub;
    if (sub <= uint8_t(WeaponSub::Back)) weaponRows(s);
    else if (sub == uint8_t(WeaponSub::Switch)) weaponSwitch(s);
    else weaponList(s);
    rowsDraw(s, stateAddress(TableId::StatusEquipmentWeaponRows));
    frameDraw(0xc, 0xa8, 0x128, 0x30, kFrameRinged);
    // exe 0x5ed9b0(block, 2, 3): exe 0x55bbc0 the model actor updated; then its drawing into the box 2
    subScreenActorsUpdate();
    weaponModelDraw(2, 3);
    s.cursor = int8_t(s.cursorKept);
}
// exe 0x5eb740: the body part slots' rows (helmet / armour / legs, back)
void bodyRows(StatusScreen& s) {
    const BodySub st = BodySub(s.sub);
    if (st == BodySub::Enter) {
        cursorReset(s);
        s.sub = uint8_t(BodySub::Rows);
    } else if (st == BodySub::Rows) {
        s.listScroll = 0;
        s.listRow = 0;
        menuCursorStep(s, 2);
        if (confirmed()) s.sub = bodyRowSubs[size_t(int32_t(s.cursor))];
    } else if (st == BodySub::Back) {
        levelBack(s, 2);
    }
    summaryBoxDraw(s, 0x80, 0x36, 3, kBoxAllRows | kBoxBody);
    kindListDraw(s, 0x80, 0x72, 3, uint32_t(int32_t(s.cursor)) + kKindHelmet, 0);
}
// exe 0x5eb7d0: a slot's parts (+ none): the helmet, the armour (its hit height level), the legs
void bodyList(StatusScreen& s) {
    Player& p = player();
    const uint32_t k = uint8_t(s.sub - uint8_t(BodySub::FirstSlot));
    summaryBoxDraw(s, 0x80, 0x36, 1, (kBoxRow << k) | kBoxBody);
    kindListDraw(s, 0x80, 0x52, 5, k + kKindHelmet, kListCursorBar | kListDescription | kListNoneRow);
    const ListStep st = ListStep(s.step);
    if (st == ListStep::Enter) {
        cursorReset(s);
        s.slideIn = 0;
        s.step = uint8_t(ListStep::Cursor);
    } else if (st == ListStep::Cursor) {
        const int r = oneColumnListStep(s);
        if (r == kAnswerConfirm) {
            soundPlayFlatForced(uint16_t(UiSound::Confirm));
            s.cursorShown = 0;
            const uint8_t v = statusList().entryByte(listEntryUnderCursor(s));
            if (k == 0) {
                p.helmetPart = int8_t(v);
            } else if (k == 1) {
                p.armourPart = int8_t(v);
                p.hitHeightLevel = armourHitHeights[v];
            } else {
                p.legPart = int8_t(v);
            }
            levelBack(s, 3);
        } else if (r == kAnswerCancel) {
            s.step = uint8_t(ListStep::Back);
        }
    } else if (st == ListStep::Back) {
        levelBack(s, 3);
    }
    s.cursor = int8_t(s.cursorKept);
}
// exe 0x5eb6f0: the body parts (their rows, a slot's list), the rows' table, the leg key items, the frame
void equipBody(StatusScreen& s) {
    if (s.sub <= uint8_t(BodySub::Back)) bodyRows(s);
    else bodyList(s);
    rowsDraw(s, stateAddress(TableId::StatusEquipmentBodyRows));
    legItemsDraw(0x10, 0x7a);
    frameDraw(0xc, 0xa8, 0x128, 0x30, kFrameRinged);
    s.cursor = int8_t(s.cursorKept);
}
// exe 0x5eb980: the buster's rows (equip / sort, back); on entry the parts list loses the parts no longer owned and
// gains the owned ones it lacks, at its end
void busterRows(StatusScreen& s) {
    Player& p = player();
    const BusterSub st = BusterSub(s.sub);
    if (st == BusterSub::Enter) {
        int16_t n = 0;
        while (n < int16_t(kOwnedPartSlots) && p.ownedParts[size_t(n)] != 0) ++n;
        for (int16_t i = 0; i < n; ++i)
            if (!storyFlag(uint32_t(p.ownedParts[size_t(i)]) + kBusterPartItems)) {
                for (int16_t j = i; j < n - 1; ++j) p.ownedParts[size_t(j)] = p.ownedParts[size_t(j + 1)];
                --n;
                --i;
                p.ownedParts[size_t(n)] = 0;
            }
        for (uint16_t part = 1; part < kOwnedPartSlots; ++part) {
            if (!storyFlag(kBusterPartItems + part)) continue;
            int16_t i = 0;
            while (i < n && p.ownedParts[size_t(i)] != part) ++i;
            if (i == n) {
                p.ownedParts[size_t(n)] = uint8_t(part);
                ++n;
                p.ownedParts[size_t(n)] = 0;
            }
        }
        cursorReset(s);
        s.sub = uint8_t(BusterSub::Rows);
    } else if (st == BusterSub::Rows) {
        menuCursorStep(s, 2);
        if (confirmed()) s.sub = busterRowSubs[size_t(int32_t(s.cursor))];
    } else if (st == BusterSub::Back) {
        levelBack(s, 2);
    }
    summaryBoxDraw(s, 0x80, 0x36, 3, kBoxAllRows | kBoxBuster);
    kindListDraw(s, 0x80, 0x72, 3, kKindBusterPart, kListLeaveEquipped);
    statBarsDraw(s, 0x10, 0x7a, kBusterLevelWeapon, kBarsWeaponLevels);
}
// exe 0x5ebb40: the slots (the third needs its item)
void busterSlots(StatusScreen& s) {
    const ListStep st = ListStep(s.step);
    if (st == ListStep::Enter) {
        cursorReset(s);
        s.slideIn = 0;
        s.step = uint8_t(ListStep::Cursor);
    } else if (st == ListStep::Cursor) {
        const int8_t was = s.cursor;
        s.listScroll = 0;
        s.listRow = 0;
        menuCursorStep(s, 3);
        if (!storyFlag(kThirdSlotItem) && s.cursor == 2) {
            s.cursor = was == 0 ? 1 : 0;
            s.cursorKept = was == 0 ? 1u : 0u;
        }
        if (confirmed()) s.step = busterSlotSteps[size_t(int32_t(s.cursor))];
    } else if (st == ListStep::Back) {
        levelBack(s, 3);
    }
    summaryBoxDraw(s, 0x80, 0x36, 3, kBoxCursor | kBoxAllRows | kBoxBuster);
    kindListDraw(s, 0x80, 0x72, 3, kKindBusterPart, kListLeaveEquipped);
    statBarsDraw(s, 0x10, 0x7a, kBusterLevelWeapon, kBarsWeaponLevels);
}
// exe 0x5ebc10: the part for a slot (the parts not equipped + none); the buster's levels follow the cursor with the
// previous ones (kept in savedBusterLevels, given back on cancel) shown
void busterPart(StatusScreen& s) {
    Player& p = player();
    uint8_t slots[3] = {uint8_t(p.busterParts[0]), uint8_t(p.busterParts[1]), uint8_t(p.busterParts[2])};
    const uint32_t k = uint8_t(s.step - kFirstSlotPart);
    summaryBoxDraw(s, 0x80, 0x36, 1, (kBoxRow << k) | kBoxBuster);
    kindListDraw(s, 0x80, 0x52, 5, kKindBusterPart,
                 kListCursorBar | kListDescription | kListNoneRow | kListLeaveEquipped);
    const ListStep st = ListStep(s.detail);
    const size_t b = weaponLevelIndex(kBusterLevelWeapon, WeaponStat::Attack);
    StatusList& list = statusList();
    const size_t cur = listEntryUnderCursor(s);
    if (st == ListStep::Enter) {
        for (size_t i = 0; i < 5; ++i) s.savedBusterLevels[i] = p.weaponLevels[b + i];
        cursorReset(s);
        s.detail = uint8_t(ListStep::Cursor);
        slots[k] = list.entryByte(cur);
        busterLevelsSum(slots);
    } else if (st == ListStep::Cursor) {
        const int r = oneColumnListStep(s);
        slots[k] = list.entryByte(listEntryUnderCursor(s));
        busterLevelsSum(slots);
        if (r == kAnswerConfirm) {
            soundPlayFlatForced(uint16_t(UiSound::Confirm));
            s.cursorShown = 0;
            p.busterParts[k] = int8_t(slots[k]);
            levelBack(s, 4);
        } else if (r == kAnswerCancel) {
            s.detail = uint8_t(ListStep::Back);
        }
    } else if (st == ListStep::Back) {
        for (size_t i = 0; i < 5; ++i) p.weaponLevels[b + i] = s.savedBusterLevels[i];
        levelBack(s, 4);
    }
    statBarsDraw(s, 0x10, 0x7a, kBusterLevelWeapon, kBarsPrevious | kBarsWeaponLevels);
}
// exe 0x5ebdc0: the sort rows (manual, by a bonus, back; horizontal, over the parts list whose cursor runs too): a bonus
// sort orders the parts by that bonus (descending, a bubble sort)
void busterSort(StatusScreen& s) {
    Player& p = player();
    switch (s.step) {
    case uint8_t(ListStep::Enter):
        cursorReset(s);
        s.step = uint8_t(ListStep::Cursor);
        break;
    case uint8_t(ListStep::Cursor):
        oneColumnListStep(s);
        menuCursorStep(s, 3);
        if (s.answer == kAnswerConfirm) {
            const uint8_t to = busterSortSteps[size_t(int32_t(s.cursor))];
            s.detail = 0;
            s.step = to;
        }
        break;
    case uint8_t(ListStep::Back):
        levelBack(s, 3);
        break;
    case kFirstBonusSort:
    case kFirstBonusSort + 1:
    case kFirstBonusSort + 2:
    case kFirstBonusSort + 3: {
        menuCursorStep(s, 3);
        const uint32_t stat = s.step - uint32_t(kFirstBonusSort);
        int n = 0;
        while (n < int(kOwnedPartSlots) && p.ownedParts[size_t(n)] != 0) ++n;
        if (n > 1)
            for (int end = n - 1, pass = n - 1; pass != 0; --pass, --end)
                for (int j = 0; j < end; ++j) {
                    const uint8_t a = p.ownedParts[size_t(j)], c = p.ownedParts[size_t(j + 1)];
                    if (partBonuses[a].stats[stat] < partBonuses[c].stats[stat]) {
                        p.ownedParts[size_t(j + 1)] = a;
                        p.ownedParts[size_t(j)] = c;
                    }
                }
        s.step = uint8_t(ListStep::Cursor);
        break;
    }
    default:
        break;
    }
    rowsDraw(s, stateAddress(TableId::StatusEquipmentSortRows));
    kindListDraw(s, 0x80, 0x42, 6, kKindBusterPart, kListCursorBar);
    statBarsDraw(s, 0x10, 0x7a, int16_t(statusList().entries[listEntryUnderCursor(s)]), kBarsPartBonus);
    s.cursor = int8_t(s.cursorKept);
}
// exe 0x5ec000: the manual sort -- a part chosen (framed), then the one it swaps with; the help from the sort bank
void busterManual(StatusScreen& s) {
    Player& p = player();
    const ListStep st = ListStep(s.detail);
    if (st == ListStep::Enter) {
        s.slideIn = 0;
        s.manualStep = 0;
        s.item = -1;
        s.detail = uint8_t(ListStep::Cursor);
    } else if (st == ListStep::Cursor) {
        const int r = oneColumnListStep(s);
        if (s.manualStep == 0) {
            if (r != 0) {
                if (r < 1) {
                    s.detail = uint8_t(ListStep::Back);
                } else {
                    soundPlayFlatForced(uint16_t(UiSound::Confirm));
                    if (s.listRows != 0) {
                        s.item = int16_t(uint32_t(s.listScroll) + s.listRow);
                        s.manualStep += 1;
                    }
                }
            }
        } else if (s.manualStep == 1 && r != 0) {
            if (r < 1) {
                s.item = -1;
                s.manualStep = 0;
            } else {
                soundPlayFlatForced(uint16_t(UiSound::Confirm));
                const size_t a = size_t(uint32_t(int32_t(s.item)));
                const size_t c = size_t(uint32_t(int32_t(int16_t(uint16_t(s.listScroll) + s.listRow))));
                const uint8_t t = p.ownedParts[a];
                p.ownedParts[a] = p.ownedParts[c];
                p.ownedParts[c] = t;
                s.item = -1;
                s.manualStep = 0;
            }
        }
    } else if (st == ListStep::Back) {
        levelBack(s, 4);
    }
    rowsDraw(s, stateAddress(TableId::StatusEquipmentManualSortRows));
    kindListDraw(s, 0x80, 0x42, 6, kKindBusterPart, kListCursorBar | kListFrameChosen);
    statBarsDraw(s, 0x10, 0x7a, int16_t(statusList().entries[listEntryUnderCursor(s)]), kBarsPartBonus);
    menuWindowOpen(0, menuBanks().equipSort, s.manualStep != 0 ? kManualSortSwapHelp : kManualSortHelp);
    s.cursor = int8_t(s.cursorKept);
}
// exe 0x5eb940: the buster parts (its rows, the slots with a slot's part, the sorts), the rows' table, the frame
void equipBuster(StatusScreen& s) {
    const uint8_t sub = s.sub;
    if (sub <= uint8_t(BusterSub::Back)) {
        busterRows(s);
    } else if (sub == uint8_t(BusterSub::Slots)) {
        if (s.step <= 2) busterSlots(s);
        else busterPart(s);
        s.cursor = int8_t(s.cursorKept);
    } else if (s.step == kManualSort) {
        busterManual(s);
    } else {
        busterSort(s);
    }
    rowsDraw(s, stateAddress(TableId::StatusEquipmentBusterRows));
    frameDraw(0xc, 0xa8, 0x128, 0x30, kFrameRinged);
    s.cursor = int8_t(s.cursorKept);
}

} // namespace

int panelSwitchRefusal() {
    const Player& p = player();
    uint32_t i = kFirstOwnedWeapon;
    while (i < kWeaponsEnd && !storyFlag(i + kSpecialWeaponItems)) ++i;
    if (i == kWeaponsEnd) return kRefusedNoWeapon;
    if (p.variant != 0) return kRefusedVariant;
    if (p.carryFlags != 0 || p.subState == uint8_t(PlayerSub::Lift)) return kRefusedBusy;
    return -1;
}

void equipmentPage(StatusScreen& s) {
    const uint8_t st = s.state;
    if (st <= uint8_t(EquipState::Back)) equipRows(s);
    else if (st == uint8_t(EquipState::Weapon)) equipWeapon(s);
    else if (st == uint8_t(EquipState::Body)) equipBody(s);
    else equipBuster(s);
    pageDraw(s, kEquipmentPage);
    s.cursor = int8_t(s.cursorKept);
}

} // namespace od::port
