// The status sub-screen's items page (see status_items.hpp), one function per exe function.
#include "game/status_items.hpp"
#include "game/patched_data.hpp"

#include <algorithm>
#include <array>
#include <span>

#include "engine/data_source.hpp"
#include "engine/screen_prim.hpp"
#include "game/display.hpp"
#include "game/hit_points.hpp"
#include "game/items.hpp"
#include "game/language_pack.hpp"
#include "game/menu_banks.hpp"
#include "game/menu_draw.hpp"
#include "game/menu_pointer.hpp"
#include "game/message.hpp"
#include "game/message_state.hpp"
#include "game/player/player_input.hpp"
#include "game/player/player_state.hpp"
#include "game/rewards.hpp"
#include "game/shop.hpp"
#include "game/status_controls.hpp"
#include "game/status_screen.hpp"
#include "game/story_flags.hpp"

namespace od::port {

using namespace menu_draw;

namespace {

// +1 of the page: the rows (0..2), then the list of the row chosen (3 the items, 4 the key items)
enum class ItemsState : uint8_t { Enter = 0, Rows = 1, Back = 2, Items = 3, KeyItems = 4 };
// +2 of the list
enum class ItemsListStep : uint8_t { SetUp = 0, Cursor = 1, Back = 2, StartUse = 10, Use = 11, Rebuild = 12 };
// the rows -> their states, their lists (exe tables)
const DataTable<ScalarTable<uint8_t, 3>> itemRowStates{TableId::StatusItemsItemRowStates};
const DataTable<ScalarTable<uint8_t, 3>> itemRowLists{TableId::StatusItemsItemRowLists};
constexpr uint8_t kItemsList = 0, kKeyItemsList = 1;
constexpr uint32_t kUsableItemCount = 0x52, kKeyItemCount = 0x30;
constexpr uint8_t kListVisibleRows = 7;
constexpr int kItemsPage = 1; // the page tables' index

// the use's window and its messages (MenuBanks::use)
constexpr int8_t kUseWindow = 1, kDescriptionWindow = 3;
enum class UseMessage : int8_t {
    AskLife = 0, AlreadyFull = 1, NowFull = 2, Recovered = 3, CanisterEmpty = 4,
    AskCure = 8, NothingToCure = 9, Cured = 10, NoCureLeft = 0xd,
    AskEnergy = 0x10, EnergyFull = 0x11, EnergyRefilled = 0x12,
    AskRoll = 0x14, GiveA = 0x15, GiveB = 0x16, GiveC = 0x17, CannotUse = 0x18, WorksWhileHeld = 0x19,
    AskLetter = 0x1e, FirstLetter = 0x1f,
};
// the item classes (ItemEntry::useClass: the handlers of the exe table 0x91ade0)
constexpr int kClassNone = 0, kClassLife = 1, kClassCure = 2, kClassEnergy = 3, kClassLetter = 8;
constexpr int kClassAskRoll = 4, kClassGiveA = 5, kClassGiveB = 6, kClassGiveC = 7;
// the life items: the full refills, the half refills
constexpr int16_t kFullLifeItems[3] = {0x441, 0x442, 0x449};
constexpr int16_t kHalfLifeItems[3] = {0x44a, 0x44b, 0x44c};
constexpr uint32_t kRefillBackInShops = kEnergyRefillItem + kForSaleFlags; // the refill used: the shops sell it again
constexpr int kLifeUnit = 16; // a canister charge: 16 hit points
// the use's question: still waiting / yes / no (the handler's step moves on by one for each answer)
constexpr int kAskWaiting = 0, kAskNo = -1;

// The items page's count text (TableId::StatusItemsCountText, a bank of the exe's data the page rewrites: "nn/nn", the charges left /
// their maximum): a patched record, its bytes the game's built data the bank's readers read
struct ItemCountText : DataRecord {
    static constexpr uint32_t kBytes = 8;
    std::array<uint8_t, kBytes> bytes{}; // +0: the bank's offset; +2 the charges, +4 the slash, +5 the maximum
    template <class R> static ItemCountText load(const R& r) {
        ItemCountText t;
        for (size_t i = 0; i < kBytes; ++i) t.bytes[i] = r.u8(i);
        return t;
    }
    void save(std::span<uint8_t> out) const { std::copy(bytes.begin(), bytes.end(), out.begin()); }
    std::span<uint8_t> charges() { return std::span<uint8_t>(bytes).subspan(2, 2); }
    std::span<uint8_t> maximum() { return std::span<uint8_t>(bytes).subspan(5, 2); }
    uint8_t& slash() { return bytes[4]; }
};
PatchedRecord<ItemCountText> gItemCountText{TableId::StatusItemsCountText};
ItemCountText& itemCountText() {
    ItemCountText& t = *gItemCountText;
    registerBuiltData(stateAddress(TableId::StatusItemsCountText), t.bytes); // the bank's readers read the copy
    return t;
}
// the PS1 encodings of the count's glyphs (SUBSCN.BIN 0x2d2d0: digit d = glyph d, the space 0x4c; 0x363f8: '/' 0x4b)
constexpr uint8_t kPs1Space = 0x4c, kPs1Slash = 0x4b;

// exe 0x5ea670(dest, n): two digits (a leading space) into an exe string; a Latin pack: the PS1's glyphs
void countDigits(std::span<uint8_t> at, uint8_t n) {
    const bool ps1 = ps1Text();
    at[0] = uint8_t(n / 10 ? (ps1 ? n / 10 : '0' + n / 10) : (ps1 ? kPs1Space : ' '));
    at[1] = uint8_t(ps1 ? n % 10 : '0' + n % 10);
}
// the two-column list's entry under the cursor (the column + the row's two entries)
size_t gridEntryUnderCursor(const StatusScreen& s) {
    return size_t(uint32_t(int32_t(s.cursor) + (int32_t(s.listScroll) + s.listRow) * 2));
}

// exe 0x5ea1a0(block, list): the owned items into the list (kItemsList: the usable items, kKeyItemsList: the key
// items), the list's rows
void buildItemList(StatusScreen& s, int list) {
    StatusList& l = statusList();
    l.clearWords();
    uint32_t n = 0;
    if (list == kItemsList) {
        for (uint32_t i = 0; i < kUsableItemCount; ++i)
            if (storyFlag(i + kUsableItems)) l.entries[n++] = uint16_t(i + kUsableItems);
    } else if (list == kKeyItemsList) {
        for (uint32_t i = 0; i < kKeyItemCount; ++i)
            if (storyFlag(i + kKeyItems)) l.entries[n++] = uint16_t(i + kKeyItems);
    }
    l.entries[n] = kListEnd;
    s.listLength = uint8_t(n);
    s.listRows = uint8_t((n + 1) >> 1);
    s.listVisible = kListVisibleRows;
}
// the items list's draw flags: kListCursor the cursor bar and the description, kListBusy the names dimmed
constexpr uint32_t kListCursor = 1, kListBusy = 2;
// exe 0x5ea270(block, flags): the items list -- the frame, the visible rows (icons, names; clipped to the list while it
// scrolls), the scroll bar; with kListCursor the cursor bar (red, pulsing), the description of the item under it in
// window 3 and, for the canister / the cure, the charges; kListBusy dims the names (a use runs)
void itemsListDraw(StatusScreen& s, uint32_t flags) {
    frameDraw(0xc, 0x32, 0x128, 0x78, kFrameRound);
    const int a = s.scrollStep;
    int y = 0x36;
    if (a == 1 || a == -2) y = 0x2c;
    else if (a == 2 || a == -1) y = 0x31;
    const DrawArea area = displayDrawArea();
    otClipDrawArea(otOpen(kOtText));
    int vis = int(s.listLength) - int(s.listScroll) * 2;
    if (vis > 0xd) vis = 0xe;
    int ty = y + 1;
    StatusList& list = statusList();
    for (int i = 0; i < 0x10; ++i) {
        int k = i + int(s.listScroll) * 2;
        if (s.scrollStep < 0) k -= 2;
        const int16_t id = int16_t(list.entries[size_t(uint32_t(int32_t(int16_t(k))))]);
        if (id < 0) break;
        const int xo = (i & 1) ? 0x88 : 0;
        if ((flags & kListCursor) && i < 0xe)
            otHitRow(otOpen(kOtRows), xo + 0x11, y, 0x88, 0x10, kListCell + (i >> 1) * 2 + (i & 1));
        itemIconDraw(xo + 0x11, y, id, (flags & kListBusy) != 0);
        uint32_t dim = itemEntry(id).usable != 0 ? 0 : kTextPaletteDim;
        if (flags & kListBusy) dim = kTextPaletteDim;
        bankTextDraw(textPlace(uint32_t(xo + 0x21), uint32_t(ty), dim), menuBanks().names, itemString(id), kTextNoRuby);
        if (i & 1) {
            y += 0x10;
            ty += 0x10;
        }
    }
    otClip(otOpen(kOtText), 0xc, area.y + 0x37, 0x128, (((vis + 1) >> 1) << 4) - 1);
    statusScrollBarDraw(s, 0x126, 0x32);
    if (!(flags & kListCursor) || s.cursorShown == 0 || s.listRows == 0) return;
    otTile(otOpen(kOtRows), s.cursor * 0x88 + 0x22, int(s.listRow) * 0x10 + 0x37, 0x78, 0xe, barPulse(), kTileOpaque);
    const int16_t entry = int16_t(s.cursor + (int(s.listScroll) + s.listRow) * 2);
    const int16_t id = int16_t(list.entries[size_t(uint32_t(int32_t(entry)))]);
    if (id >= 0) menuWindowOpen(kDescriptionWindow, menuBanks().descs, itemString(id));
    ShopUpgrades& charges = shopUpgrades();
    ItemCountText& count = itemCountText();
    if (id == int16_t(kLifeCanisterItem)) {
        countDigits(count.charges(), uint8_t(charges.first.level));
        countDigits(count.maximum(), uint8_t(charges.first.bought));
    } else if (id == int16_t(kCureItem)) {
        countDigits(count.charges(), uint8_t(charges.second.level));
        countDigits(count.maximum(), uint8_t(charges.second.bought));
    } else {
        return;
    }
    count.slash() = ps1Text() ? kPs1Slash : uint8_t('/');
    bankTextDraw(textPlace(0xe0, 0xc2), stateAddress(TableId::StatusItemsCountText), 0, 0);
}

// the use's question shared by the handlers (their steps 0..2): window 1, message q with the item; kAskWaiting,
// 1 = "yes" (the handler's step + 1), kAskNo
int askUse(StatusScreen& s, UseMessage q, bool advanced) {
    if (!advanced) {
        messageWindow(kUseWindow).itemName = uint16_t(s.item);
        if (menuWindowOpen(kUseWindow, menuBanks().use, int16_t(q)) == -1) return kAskWaiting;
        s.step += 1;
    }
    if (windowStatus(kUseWindow) != 0) return kAskWaiting;
    const uint32_t ans = messageSystem().answerOf(kUseWindow);
    if (ans == 0) {
        s.step += 1;
        return kAskWaiting;
    }
    if (ans < 3) return kAskNo;
    return kAskWaiting;
}
// the handler's result message (its step 4)
int showResult(StatusScreen& s) {
    if (menuWindowOpen(kUseWindow, menuBanks().use, s.result) == -1) return 0;
    s.step += 1;
    return 0;
}
// exe 0x5eabb0: class 1, the life refills (the canister: its charges, else full / half refills)
int useLife(StatusScreen& s) {
    switch (s.step) {
    case 0:
        s.step = 1;
        [[fallthrough]];
    case 1: return askUse(s, UseMessage::AskLife, false);
    case 2: return askUse(s, UseMessage::AskLife, true);
    case 3: {
        Player& p = player();
        ShopUpgrades& charges = shopUpgrades();
        int max = p.maxHitPoints;
        int need = max - p.hitPoints;
        if (need < 1) {
            s.result = int8_t(UseMessage::AlreadyFull);
            s.step += 1;
            return 0;
        }
        const int16_t item = s.item;
        if (item == int16_t(kLifeCanisterItem)) {
            if (charges.first.level == 0) {
                s.result = int8_t(UseMessage::CanisterEmpty);
                s.step += 1;
                return 0;
            }
            need = (need + 0xf) >> 4;
            if (charges.first.level <= need) need = charges.first.level;
            charges.first.level = int8_t(charges.first.level - need);
            need <<= 4;
        } else if (item == kHalfLifeItems[0] || item == kHalfLifeItems[1] || item == kHalfLifeItems[2] ||
                   item == kFullLifeItems[0] || item == kFullLifeItems[1] || item == kFullLifeItems[2]) {
            if (item >= kHalfLifeItems[0]) max >>= 1;
            clearStoryFlag(uint32_t(item));
            need = max;
        }
        if (need > 0) {
            hitPointsChange(p, need);
            if (p.maxHitPoints <= p.hitPoints) {
                s.result = int8_t(UseMessage::NowFull);
                s.step += 1;
                return 0;
            }
            s.result = int8_t(UseMessage::Recovered);
        }
        s.step += 1;
        return 0;
    }
    case 4: return showResult(s);
    case 5: return windowStatus(kUseWindow) == 0 ? 1 : 0;
    default: return 0;
    }
}
// exe 0x5eadc0: class 2 (the cure): its charges, the status effects cleared
int useCure(StatusScreen& s) {
    switch (s.step) {
    case 0:
        s.step += 1;
        [[fallthrough]];
    case 1: return askUse(s, UseMessage::AskCure, false);
    case 2: return askUse(s, UseMessage::AskCure, true);
    case 3: {
        Player& p = player();
        ShopUpgrades& charges = shopUpgrades();
        if (charges.second.level == 0) {
            s.result = int8_t(UseMessage::NoCureLeft);
        } else if (uint8_t(p.status) == 0) {
            s.result = int8_t(UseMessage::NothingToCure);
        } else {
            charges.second.level = int8_t(charges.second.level - 1);
            s.result = int8_t(UseMessage::Cured);
            p.status = uint16_t(p.status & 0xff00u);
        }
        s.step += 1;
        [[fallthrough]];
    }
    case 4: return showResult(s);
    case 5: return windowStatus(kUseWindow) != 0 ? 0 : 1;
    default: return 0;
    }
}
// exe 0x5eaed0: class 3 (the energy refill): the special weapon's energy refilled (used up: the shops sell it again)
int useEnergy(StatusScreen& s) {
    switch (s.step) {
    case 0:
        s.step += 1;
        [[fallthrough]];
    case 1: return askUse(s, UseMessage::AskEnergy, false);
    case 2: return askUse(s, UseMessage::AskEnergy, true);
    case 3: {
        Player& p = player();
        if (p.energyPool == p.energyReserve) {
            s.result = int8_t(UseMessage::EnergyFull);
        } else {
            weaponEnergyAdd(p, p.energyReserve);
            clearStoryFlag(kEnergyRefillItem);
            setStoryFlag(kRefillBackInShops);
            s.result = int8_t(UseMessage::EnergyRefilled);
        }
        s.step += 1;
        [[fallthrough]];
    }
    case 4: return showResult(s);
    case 5: return windowStatus(kUseWindow) != 0 ? 0 : 1;
    default: return 0;
    }
}
// exe 0x5eafd0: the other classes: a message by the class (cannot be used, ask Roll / give it to someone, works while
// you have it)
int useNone(StatusScreen& s) {
    const uint8_t st = s.step;
    if (st == 0) {
        s.step = 1;
    } else if (st != 1) {
        if (st != 2) return 0;
        return windowStatus(kUseWindow) != 0 ? 0 : 1;
    }
    UseMessage q;
    switch (itemEntry(s.item).useClass) {
    case kClassNone: q = UseMessage::CannotUse; break;
    case kClassAskRoll: q = UseMessage::AskRoll; break;
    case kClassGiveA: q = UseMessage::GiveA; break;
    case kClassGiveB: q = UseMessage::GiveB; break;
    case kClassGiveC: q = UseMessage::GiveC; break;
    default: q = UseMessage::WorksWhileHeld; break;
    }
    if (menuWindowOpen(kUseWindow, menuBanks().use, int16_t(q)) == -1) return 0;
    s.step += 1;
    return windowStatus(kUseWindow) != 0 ? 0 : 1;
}
// exe 0x5eb080: class 8 (the letters): read one (its message)
int useLetter(StatusScreen& s) {
    switch (s.step) {
    case 0:
        s.step += 1;
        [[fallthrough]];
    case 1: return askUse(s, UseMessage::AskLetter, false);
    case 2: return askUse(s, UseMessage::AskLetter, true);
    case 3: {
        const uint16_t id = uint16_t(s.item);
        const int letter = id == kFirstLetterItem ? 0 : id == kFirstLetterItem + 1 ? 1 : id == kFirstLetterItem + 2 ? 2
                         : id == kFirstLetterItem + 3 ? 3 : 4;
        s.result = int8_t(int(UseMessage::FirstLetter) + letter);
        if (menuWindowOpen(kUseWindow, menuBanks().use, s.result) == -1) return 0;
        s.step += 1;
        [[fallthrough]];
    }
    case 4: return windowStatus(kUseWindow) == 0 ? 1 : 0;
    default: return 0;
    }
}
// the exe table 0x91ade0 (the class -> its handler)
int useItem(StatusScreen& s, int useClass) {
    switch (useClass) {
    case kClassLife: return useLife(s);
    case kClassCure: return useCure(s);
    case kClassEnergy: return useEnergy(s);
    case kClassLetter: return useLetter(s);
    default: return useNone(s);
    }
}
// exe 0x5e9f80: the rows (the items / the key items / back; horizontal), the list of the row under the cursor
void itemsRows(StatusScreen& s) {
    const ItemsState st = ItemsState(s.state);
    if (st == ItemsState::Enter) {
        s.switching = 0;
        cursorReset(s);
        s.listKind = kItemsList;
        s.state = uint8_t(ItemsState::Rows);
    } else if (st == ItemsState::Rows) {
        menuCursorStep(s, 1);
        if (menuPadPressed() & kPadConfirm) {
            const uint8_t t = itemRowStates[size_t(int32_t(s.cursor))];
            if (t == uint8_t(ItemsState::Items)) s.listKind = kItemsList;
            else if (t == uint8_t(ItemsState::KeyItems)) s.listKind = kKeyItemsList;
            s.state = t;
        }
    } else if (st == ItemsState::Back) {
        levelBack(s, 1);
    }
    buildItemList(s, itemRowLists[size_t(int32_t(s.cursor))]);
    itemsListDraw(s, 0);
}
// exe 0x5ea030: the list (its set-up, the cursor, back, a use, the list rebuilt after it)
void itemsList(StatusScreen& s) {
    bool toCursor = true;
    StatusList& list = statusList();
    switch (ItemsListStep(s.sub)) {
    case ItemsListStep::SetUp:
        buildItemList(s, s.listKind);
        cursorReset(s);
        break;
    case ItemsListStep::Cursor: {
        const int r = twoColumnListStep(s, 2, s.listLength, 2);
        if (s.listRows != 0 && r == kAnswerConfirm) {
            const uint16_t id = list.entries[gridEntryUnderCursor(s)];
            s.sub = uint8_t(ItemsListStep::StartUse);
            s.item = int16_t(id);
        }
        toCursor = false;
        break;
    }
    case ItemsListStep::Back:
        s.listScroll = 0;
        levelBack(s, 2);
        toCursor = false;
        break;
    case ItemsListStep::StartUse:
        windowClose(kUseWindow, WindowClose::Gentle);
        s.step = 0;
        s.sub += 1;
        [[fallthrough]];
    case ItemsListStep::Use: {
        const int r = useItem(s, itemEntry(s.item).useClass);
        if (r == 0) {
            toCursor = false;
        } else if (r > 0) {
            s.sub += 1;
            toCursor = false;
        }
        break;
    }
    case ItemsListStep::Rebuild:
        buildItemList(s, s.listKind);
        if (s.cursor == 1 && int16_t(list.entries[1 + (uint32_t(s.listScroll) + s.listRow) * 2]) == -1) s.cursor = 0;
        break;
    default:
        toCursor = false;
        break;
    }
    if (toCursor) s.sub = uint8_t(ItemsListStep::Cursor);
    itemsListDraw(s, int8_t(s.sub) < int8_t(ItemsListStep::StartUse) ? kListCursor : kListCursor | kListBusy);
    s.cursor = int8_t(s.cursorKept);
}

} // namespace

void itemsPage(StatusScreen& s) {
    if (s.state <= uint8_t(ItemsState::Back)) itemsRows(s);
    else itemsList(s);
    pageDraw(s, kItemsPage);
    s.cursor = int8_t(s.cursorKept);
}

} // namespace od::port
