// The shops (see shop.hpp), transcribed from dash2.exe one function per exe function, the item list's drawing (exe
// 0x5d2cf0) with them (its packets through game/menu_draw).
#include "game/loaded_data.hpp"
#include "game/shop.hpp"
#include "game/shop_state.hpp"

#include <cstdlib>

#include "engine/data_source.hpp"
#include "game/audio_logic.hpp"
#include "game/camera_state.hpp"
#include "game/display.hpp"
#include "game/game_state.hpp"
#include "game/items.hpp"
#include "game/menu_banks.hpp"
#include "game/menu_draw.hpp"
#include "game/menu_pointer.hpp"
#include "game/message.hpp"
#include "game/message_state.hpp"
#include "game/text_format.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/rewards.hpp"
#include "game/script_engine.hpp"
#include "game/script_state.hpp"
#include "game/story_flags.hpp"

using namespace od::port::shop_state;

namespace od::port {

namespace {

std::vector<ScreenPrim>* gDraw = nullptr;

constexpr const char* kShopPort = "shop port"; // the area of its port-gap errors

// ---- the items and their flags ----
// Item ids (the system strings' ids): 0x380.. items and parts, the upgrades kLifeUpgrade.. (7), kShopBack. Flag id
// marks an item owned, flag id + kForSaleFlags for sale (the stock); flag kStoryStockFlags + story the story's stock
// added
constexpr int16_t kFirstItem = int16_t(kSpecialWeaponItems);
constexpr int16_t kFirstPart = int16_t(kBusterPartItems); // the parts and items (0x394 .. 0x477) bought as flags
constexpr int16_t kLastPart = 0x477;
constexpr int16_t kLifeUpgrade = 0x478;  // the life gauge: + 0x10 hit points, 5 times
constexpr int16_t kUpgradeOne = 0x479;   // the first buster upgrade's level (with flag kUpgradeOneFlag)
constexpr int16_t kUpgradeTwo = 0x47a;   // the second's
constexpr uint32_t kUpgradeCount = 7;    // the upgrade ids kLifeUpgrade .. 0x47e
constexpr int16_t kFirstGift = 0x45a;    // the gifts 0x45a .. 0x460: sold to raise the tool kit balance
constexpr int16_t kLastGift = 0x460;
constexpr int16_t kBigGift = 0x45e;      // the gift worth kBigGiftBalance
constexpr int kGiftBalance = 1000, kBigGiftBalance = 10000;
constexpr uint32_t kGiftGivenFlags[7] = {0x131, 300, 0x12d, 299, 0x12f, 0x12e, 0x130}; // by gift: the flag it sets
constexpr int16_t kSoldBack = 0x441;      // the part whose sale takes kSoldBackBalance from the tool kit balance
constexpr int kSoldBackBalance = -5000;
constexpr int16_t kNotResold = 0x44c;     // the parts kNotResold + 1 .. 0x44f are not offered again once sold
constexpr int16_t kNotResoldEnd = 0x450;

constexpr uint32_t kStoryStockFlags = 0x480;
constexpr uint32_t kUpgradeOneFlag = 0x400, kUpgradeTwoFlag = 0x401; // owning them opens the upgrades
constexpr uint32_t kSecondKindFlags = 0x53c; // the for-sale flags from here on are the second kind's stock
constexpr uint32_t kShopKindFlag = 0x6fe;    // the second kind of shop
constexpr uint32_t kShopOpenFlag = 0x6ff;    // the clerk's message opened the shop
constexpr int16_t kLifeBase = 0x50;          // the life gauge's first level (hit points)
constexpr int16_t kLifeStep = 0x10;
constexpr int32_t kLifeLevels = 5;
constexpr int8_t kUpgradeOneStart = 5, kUpgradeTwoStart = 3; // the levels the upgrade items start at
constexpr int8_t kUpgradeMax = 99;
constexpr int32_t kUpgradePriceLevels = 0xe; // the level prices' last entry
constexpr int32_t kUpgradeOneUnit = 100, kUpgradeTwoUnit = 1000; // the upgrades menu's price per level
constexpr int32_t kPriceUnit = -500;         // a negative price is in units of 500
constexpr int32_t kPriceMax = 9999999;
constexpr int32_t kPriceShiftFrom = 0x480, kPriceShiftEnd = 0x580, kPriceShift = 0x100;
// the default parts (index 0 of each kind) are never owned / sold: their for-sale flags cleared
constexpr uint32_t kDefaultPartFlags[8] = {0x394, 0x3b8, 0x3bc, 0x3c8, 0x494, 0x4b8, 0x4bc, 0x4c8};
// the owned parts' ids: the buster parts, the helmet, the armour, the legs
constexpr int16_t kBusterPartIds = int16_t(kBusterPartItems), kHelmetIds = int16_t(kHelmetItems),
                  kArmourIds = int16_t(kArmourItems), kLegsIds = int16_t(kLegItems);
constexpr uint8_t kVisibleRows = 6;

// ---- the exe's tables ----
const DataTable<ScalarTable<uint8_t, 0x20>> shopLines{TableId::ShopLines}; // 16 message ids per shop kind (ShopLine)
// by item id (ids kFirstItem .. 0x47f), and the ids for sale 0x480 .. 0x57f moved by kPriceShift: one array on the PC
// (exe 0x8d11a0), two tables on the PS1 (SLUS_011.40 0x800708dc / 0x80068b90, by the same index)
const DataTable<ScalarTable<int16_t, kPriceShiftFrom>> itemPrices{TableId::ShopItemPrices};
const DataTable<ScalarTable<int16_t, kPriceShiftEnd + kPriceShift>> forSalePrices{TableId::ShopItemPricesForSale};
const DataTable<ScalarTable<int16_t, kLifeLevels>> lifePrices{TableId::ShopLifePrices};
const DataTable<ScalarTable<int16_t, kUpgradePriceLevels + 1>> upgradeOnePrices{TableId::ShopUpgradeOnePrices};
const DataTable<ScalarTable<int16_t, kUpgradePriceLevels + 1>> upgradeTwoPrices{TableId::ShopUpgradeTwoPrices};
// by story (exe data 0x8d1bec / 0x8d1c48): pointers to a list of item indices (0xff ends it) or null
struct StoryStock : DataRecord {
    static constexpr size_t kStories = 0x17;
    static constexpr uint32_t kBytes = kStories * 4, kMaxItems = 0x100;
    std::array<std::vector<uint8_t>, kStories> lists;
    static StoryStock load(const TableReader& r) {
        StoryStock t;
        for (size_t k = 0; k < kStories; ++k) {
            const TableReader items = r.follow(k * 4, kMaxItems);
            for (size_t n = 0; n < items.size() && items.u8(n) != 0xff; ++n) t.lists[k].push_back(items.u8(n));
        }
        return t;
    }
};
const DataTable<StoryStock> storyStockA{TableId::ShopStoryStockA}, storyStockB{TableId::ShopStoryStockB};
const DataTable<ScalarTable<uint16_t, 0x10>> sellableParts{TableId::ShopSellableParts}; // the parts the sell list leaves out (0xffff ends it)
// A page of the list (exe 0x8d1cfc: the first item index and the count, by page)
struct ShopPage : DataRecord {
    uint16_t first = 0;
    uint16_t count = 0;
    static ShopPage load(const TableReader& r) {
        ShopPage rec;
        rec.first = r.u16(0x0);
        rec.count = r.u16(0x2);
        return rec;
    }
};
const DataTable<RecordTable<ShopPage, 2, 4>> shopPages{TableId::ShopPages};
constexpr uint8_t kListEnd = 0xff;
constexpr uint16_t kPartsEnd = 0xffff;

// The clerk's lines (the columns of shopLines)
enum class ShopLine : uint8_t {
    Greeting = 0,
    SellPage = 1,       // the page question of the sell menu
    BuyPage = 2,        // ... of the buy menu
    PriceQuestion = 3,
    Thanks = 4,
    UpgradesGreeting = 5,
    UpgradeOneQuestion = 6,
    UpgradeTwoQuestion = 7,
    UpgradeThanks = 8,
    UpgradeAtMax = 9,
    UpgradeNotOwned = 0xa,
    NotEnoughZenny = 0xb,
    Goodbye = 0xc,
    UpgradeNotEnough = 0xd,
    GiftQuestion = 0xe,
};
// the windows and the menu messages
constexpr int8_t kListWindow = 1, kMenuWindow = 2, kQuestionWindow = 3, kClerkWindow = 4;
constexpr int16_t kListMessage = 0, kListHeaderMessage = 1, kMainMenuMessage = 3, kPageMessage = 4,
                  kUpgradesMenuMessage = 5;

// the main menu's answers
enum class MainAnswer : uint8_t { Buy = 0, Sell = 1, Upgrades = 2, Leave = 3, Cancel = 4 };
// the page question's
enum class PageAnswer : uint8_t { Items = 0, Parts = 1, Back = 2, Cancel = 3 };
// a yes / no question's (window 3)
constexpr uint8_t kYes = 0, kLastAnswer = 3;
// the upgrades menu's
enum class UpgradesAnswer : uint8_t { One = 0, Two = 1, Back = 2, Cancel = 3 };
// the list's steps (+3) and the upgrades' (+2)
constexpr uint8_t kListBrowse = 5, kListLeave = 6, kListNotEnough = 10;
constexpr uint8_t kUpgradePay = 5, kUpgradeMenuBack = 4, kUpgradeAtMax = 10, kUpgradeNotEnough = 0x14;
constexpr uint8_t kUpgradeAnswerShown = 100;
// the list's mode (exe 0x5d3210's argument and the draw's)
constexpr uint32_t kListParts = 1, kListSelling = 2;

// The shop's script block handlers (exe table 0x8d1c94, by the block's state) and the menus' (exe table 0x8d1ca0 by
// ShopMenu; buy / sell then by their step: 0x8d1cb0 / 0x8d1cbc): the named routines (engine/routines.csv)
const DataTable<RoutineTable<3>> shopBlockHandlers{TableId::ShopBlockHandlers};
const DataTable<RoutineTable<4>> shopMenuHandlers{TableId::ShopMenuHandlers};
const DataTable<RoutineTable<3>> buyStepHandlers{TableId::ShopBuyStepHandlers};
const DataTable<RoutineTable<3>> sellStepHandlers{TableId::ShopSellStepHandlers};

// ---- the item list's drawing (exe 0x5d2cf0) ----
// the shop's item icons (exe table 0x8d1b30: a sprite's texture word by kind, tpage 0xd)
const DataTable<ScalarTable<uint32_t, 0x10>> shopIconArts{TableId::ShopIconArts};
// the hand's bob (exe table 0x8d1cc8: x offsets by (frame >> 1) & 0xf)
const DataTable<ScalarTable<int8_t, 0x10>> shopHandBob{TableId::ShopHandBob};
// the scroll arrows (exe data: three corners {s16 x, s16 y})
struct ShopArrowShape : DataRecord {
    static constexpr uint32_t kBytes = 0xc;
    std::array<int16_t, 6> corners{}; // +0x0
    template <class R> static ShopArrowShape load(const R& r) {
        ShopArrowShape v;
        for (size_t i = 0; i < v.corners.size(); ++i) v.corners[i] = r.template read<int16_t>(0x0 + i * sizeof(int16_t));
        return v;
    }
};
constexpr uint32_t kArrowOutline = 0x008000;
constexpr uint16_t kIconMode = 0x0d;
constexpr uint32_t kAskingHand = 0x606060;
constexpr int16_t kBackString = 0x105; // the "back" row's system string
constexpr uint32_t kListRowsShown = 6;
constexpr int16_t kItemStrings = 0x380, kPartStrings = 0x700; // an id's system string: id - these

// exe 0x5d3a00(x, y, item): the item's icon (16 x 16, by the item's kind; the item id - 0x380)
void shopIconDraw(int x, int y, int16_t id) {
    int k;
    if (id >= 0x14 && id < 0x38) k = 0;
    else if (id >= 0x38 && id < 0x3c) k = 2;
    else if (id >= 0x3c && id < 0x48) k = 3;
    else if (id >= 0x48 && id < 0x50) k = 1;
    else if (id >= 0x83 && id < 0x88) k = 9;
    else if (id >= 0x88 && id < 0xbc) k = 6;
    else if (id > 0xcc && id < 0xd0) k = 10;
    else if (id >= 0xbc && id < 0xbe) k = 8;
    else if (id >= 0xbe && id < 0xc1) k = 7;
    else if (id >= 0xc3 && id <= 0xf6) k = 8;
    else k = 5;
    const size_t c = menu_draw::otOpen(menu_draw::kOtText);
    menu_draw::otDrawMode(c, kIconMode);
    menu_draw::otSprite(c, x, y, shopIconArts[size_t(k)], 0x10, 0x10, menu_draw::kNeutral, menu_draw::kSprite16);
}
// a scroll arrow: an F3 of colour `col`, then its outline (the 0x4c polyline, back to the first corner)
void shopArrowDraw(uint32_t shape, uint32_t col) {
    const ShopArrowShape tri = dataRecord<ShopArrowShape>(shape, ShopArrowShape::kBytes);
    const size_t c = menu_draw::otOpen(menu_draw::kOtText);
    int x[3], y[3];
    for (size_t k = 0; k < 3; ++k) {
        x[k] = tri.corners[k * 2];
        y[k] = tri.corners[k * 2 + 1];
    }
    menu_draw::otQuad(c, {x[0], x[1], x[2], x[2]}, {y[0], y[1], y[2], y[2]}, col, menu_draw::kPolyF3);
    for (int k = 0; k < 3; ++k) {
        ScreenPrim& l = menu_draw::otPut(c, ScreenPrim::Line);
        l.qx[0] = int16_t(x[k]);
        l.qy[0] = int16_t(y[k]);
        l.qx[1] = int16_t(x[(k + 1) % 3]);
        l.qy[1] = int16_t(y[(k + 1) % 3]);
        l.colour = kArrowOutline;
    }
}
// exe 0x5d2cf0(shop, flags): the item list as ScreenPrims in `draw` -- the rows (names x 0x34, prices x 0xbb, icons x
// 0x25; dimmed when the zenny do not reach it or a question is up), the hand on its row, the scroll arrows; flags
// kListSelling = selling (a quarter of the price)
void shopListDraw(const ShopState& s, uint32_t flags, std::vector<ScreenPrim>* draw) {
    using namespace menu_draw;
    begin(draw);
    const bool asking = s.subStep != 0; // a question / the clerk's reply is up: every row dimmed
    uint32_t y = 0x3f;                  // the rows' y, shifted while the list scrolls (-1 / -2 / 1 / 2)
    const int8_t sc = s.list.scrollStep;
    if (sc != 0 && std::abs(int(sc)) <= 2) {
        const int r = std::abs(int(sc)) % 3;
        y = (sc > 0) == (r == 1) ? 0x35 : 0x3a; // 1 / -2: 0x35, 2 / -1: 0x3a
    }
    const DrawArea area = displayDrawArea();
    otClipDrawArea(otOpen(kOtText));
    const uint32_t rows = s.list.rows < 7 ? s.list.rows : kListRowsShown;
    const MenuBanks& banks = menuBanks();
    for (uint32_t k = 0; k <= rows; ++k, y += 0x10) {
        int32_t i = int32_t(s.list.firstRow) + int32_t(k);
        if (sc < 0) --i;
        const int16_t id = int16_t(shopGlobals().itemList[size_t(uint32_t(i))]);
        uint32_t dim = asking ? kTextPaletteDim : 0;
        if (id == int16_t(kShopBack)) {
            bankTextDraw(textPlace(0x34, y, dim), banks.names, kBackString, kTextNoRuby);
            continue;
        }
        int32_t price;
        if ((flags & kListSelling) == 0) {
            price = shopPrice(id, true);
            if (gameState().zenny < price) dim = kTextPaletteDim;
        } else {
            price = (shopPrice(id, false) + 3) >> 2;
        }
        const uint32_t str = banks.shopPrices + k * kShopPriceStringSize; // exe 0x557be0 into the row's string
        TextBytes priceText(str + 2);
        messageNumberWrite(price, priceText, 0);
        bankTextDraw(textPlace(0xbb, y, dim), str, 0, kTextNoRuby);
        int name = id - kItemStrings, icon = name;
        if (name < 0) name = icon = 0;
        else if (name > 0x37f) name = id - kPartStrings;
        bankTextDraw(textPlace(0x34, y, dim), banks.names, int16_t(name), kTextNoRuby);
        shopIconDraw(0x25, int(int16_t(y)) - 2, int16_t(icon));
    }
    if (!asking) // OpenDash: the visible rows as the mouse's targets
        for (uint32_t k = 0; k < rows && k < s.list.visibleRows; ++k)
            otHitRow(otOpen(kOtText), 0x19, 0x3e + int(k) * 0x10, 0x114, 0x10, kListCell + int(k) * 2);
    // the list's drawing area (the rows scroll under it)
    otClip(otOpen(kOtText), 0x19, area.y + 0x3e, 0x114, int(rows << 4) - 1);
    if (s.list.row != kListNoRow) { // the hand (the PC font's glyph 1; still while a question is up, else it bobs)
        const int x = asking ? 0x18 : int(shopHandBob[(gameState().frameWord() >> 1) & 0xf]) + 0x18;
        const uint32_t c = asking ? kAskingHand : kNeutral;
        otGlyph(otOpen(kOtText), x, int(s.list.row) * 0x10 + 0x3f, 1, c, 0, 0x18);
    }
    const uint8_t ph = uint8_t(gameState().frame); // the arrows pulse
    const uint8_t lv = uint8_t(((ph & 0x1f) < 0x11 ? uint8_t(ph * 8) : uint8_t((ph & 0x1f) * 0xf8)) + 0x7f);
    if (s.list.firstRow != 0) shopArrowDraw(stateAddress(TableId::ShopArrowUp), uint32_t(lv) << 8); // more rows above
    if (int(s.list.visibleRows) < int(s.list.rows) - int(s.list.firstRow)) shopArrowDraw(stateAddress(TableId::ShopArrowDown), uint32_t(lv) << 8);
    end();
}

// the shop messages (exe 0x8bb1c4) and the item descriptions (0x8b7e64): a language pack's copies (game/menu_banks)
uint32_t shopBank() { return menuBanks().shop; }
// window `win` closed gently (its frame closes first)
void close(int8_t win) { windowClose(win, WindowClose::Gentle); }
// the clerk's line of this shop's kind
uint8_t line(ShopLine l) { return shopLines[uint32_t(l) + uint32_t(shopState().kind) * 0x10]; }
// the answer window `win`'s choice left
uint8_t answer(uint8_t win) { return messageSystem().answerOf(win); }
// windows 1..4 closed (a menu's start)
void closeShopWindows() {
    for (int8_t w = kListWindow; w <= kClerkWindow; ++w) close(w);
}

// exe 0x5d3950(id): the for-sale flags 0x578.. of the three upgrades (the life gauge until its last level, the
// buster upgrades while their item is owned and below kUpgradeMax)
void upgradeOffered(int16_t id) {
    uint32_t f;
    if (id == kLifeUpgrade) {
        const uint32_t maxHp = uint32_t(int32_t(player().maxHitPoints));
        if (int32_t((maxHp - uint32_t(kLifeBase)) & 0xfffffff0u) > 0x4f) clearStoryFlag(kLifeUpgrade + kForSaleFlags);
        return;
    }
    if (id == kUpgradeOne) {
        if (!storyFlag(kUpgradeOneFlag)) {
            clearStoryFlag(kUpgradeOne + kForSaleFlags);
            return;
        }
        f = kUpgradeOne + kForSaleFlags;
        if (shopUpgrades().first.bought > kUpgradeMax - 1) {
            clearStoryFlag(kUpgradeOne + kForSaleFlags);
            return;
        }
    } else if (id == kUpgradeTwo) {
        if (!storyFlag(kUpgradeTwoFlag)) {
            clearStoryFlag(kUpgradeTwo + kForSaleFlags);
            return;
        }
        f = kUpgradeTwo + kForSaleFlags;
        if (shopUpgrades().second.bought > kUpgradeMax - 1) {
            clearStoryFlag(kUpgradeTwo + kForSaleFlags);
            return;
        }
    } else {
        return;
    }
    setStoryFlag(f);
}

// exe 0x5d3460: the stock that comes with the story (each story up to the current one adds its two lists' items once)
void stockByStory() {
    uint32_t f = kStoryStockFlags, k = 0;
    int32_t i;
    do {
        if (!storyFlag(f)) {
            for (const DataTable<StoryStock>* tab : {&storyStockA, &storyStockB}) {
                if (k >= StoryStock::kStories) tableIndexError(k, StoryStock::kStories);
                for (const uint8_t item : (*tab)->lists[k]) setStoryFlag(uint32_t(item) + kStoryStockFlags);
            }
            setStoryFlag(f);
        }
        ++k;
        i = int32_t(f) - int32_t(kStoryStockFlags - 1);
        ++f;
    } while (i <= int32_t(gameState().story));
}

// exe 0x5d3530: the items owned (flag id) are no longer for sale (flag id + 0x100), the upgrades' offer flags, the
// default parts never for sale
void stockRefresh() {
    struct Span {
        uint32_t first, last;
    };
    for (const Span sp : {Span{0x495, 0x4b6}, Span{0x4b9, 0x4ba}, Span{0x4bd, 0x4c6}, Span{0x4c9, 0x4ce}, Span{0x500, 0x576}})
        for (uint32_t u = sp.first; u <= sp.last; ++u)
            if (storyFlag(u - kForSaleFlags) && storyFlag(u)) clearStoryFlag(u);
    for (uint32_t i = 0; i < kUpgradeCount; ++i) upgradeOffered(int16_t(kLifeUpgrade + i));
    for (const uint32_t f : kDefaultPartFlags) clearStoryFlag(f);
}

// exe 0x5d3210(state, mode): the list (mode & kListSelling: the owned items to sell, & kListParts: the parts page),
// "back" last; the cursor's rows / visible rows
void buildList(uint32_t mode) {
    ShopState& s = shopState();
    for (uint32_t i = 0; i < kUpgradeCount; ++i) upgradeOffered(int16_t(kLifeUpgrade + i));
    const uint32_t k = mode & kListParts;
    const ShopPage page = shopPages[k];
    const uint16_t cnt = page.count, base = page.first;
    uint32_t n = 0;
    if (!(mode & kListSelling)) {
        if (s.kind == 0 && k == 0)
            for (uint32_t i = 0; i < kUpgradeCount; ++i)
                if (storyFlag(i + kLifeUpgrade + kForSaleFlags)) shopGlobals().itemList[n++] = uint16_t(i + kLifeUpgrade);
        uint32_t idx = base, f = uint32_t(base) + kStoryStockFlags;
        for (uint32_t c = cnt; c != 0; --c, ++idx, ++f) {
            const bool check = s.kind == 0 ? int32_t(f) < int32_t(kSecondKindFlags) : int32_t(f) > int32_t(kSecondKindFlags - 1);
            if (check && storyFlag(f)) shopGlobals().itemList[n++] = uint16_t(idx + kFirstItem);
        }
    } else {
        uint32_t j = 0;
        for (uint16_t v = sellableParts[0]; v != kPartsEnd; v = sellableParts[j]) shopGlobals().ownedParts[j++] = v;
        if (k != 0) {
            const Player& p = player();
            for (uint32_t i = 0; i < 3; ++i) shopGlobals().ownedParts[j + i] = uint16_t(int32_t(p.busterParts[i]) + kBusterPartIds);
            shopGlobals().ownedParts[j + 3] = uint16_t(int32_t(p.helmetPart) + kHelmetIds);
            shopGlobals().ownedParts[j + 4] = uint16_t(int32_t(p.armourPart) + kArmourIds);
            shopGlobals().ownedParts[j + 5] = uint16_t(int32_t(p.legPart) + kLegsIds);
            j += 6;
        }
        for (uint32_t i = 0; i < cnt; ++i) {
            const int32_t id = int32_t(base) + int32_t(i) + kFirstItem;
            uint32_t t = 0;
            while (t < j && int16_t(shopGlobals().ownedParts[t]) != id) ++t;
            if (t == j && storyFlag(uint32_t(id))) shopGlobals().itemList[n++] = uint16_t(id);
        }
    }
    shopGlobals().itemList[n] = kShopBack;
    s.list.rows = uint8_t(n + 1);
    s.list.visibleRows = kVisibleRows;
}

// exe 0x5d36d0(item): bought (the price paid, then the upgrade's level or the item's flags and effects)
void buy(int16_t item) {
    zennyAdd(-shopPrice(item, true));
    Player& p = player();
    ShopUpgrades& up = shopUpgrades();
    if (item < kFirstPart || item > kLastPart) {
        if (item == kLifeUpgrade) {
            const int32_t k = (int32_t(p.maxHitPoints) - kLifeBase) >> 4;
            if (k >= 0 && k < kLifeLevels) {
                p.hitPoints = int16_t(p.maxHitPoints + kLifeStep);
                p.maxHitPoints = p.hitPoints;
            }
        } else if (item == kUpgradeOne) {
            if (up.first.bought < kUpgradeMax) {
                up.first.level = int8_t(up.first.bought + 1);
                up.first.bought = up.first.level;
                upgradeOffered(kUpgradeOne);
                return;
            }
        } else if (item == kUpgradeTwo && up.second.bought < kUpgradeMax) {
            up.second.level = int8_t(up.second.bought + 1);
            up.second.bought = up.second.level;
            upgradeOffered(kUpgradeTwo);
            return;
        }
        upgradeOffered(item);
        return;
    }
    const uint32_t u = uint32_t(int32_t(item));
    clearStoryFlag(u + kForSaleFlags);
    setStoryFlag(u);
    if (u == kUpgradeOneFlag) {
        up.first.bought = kUpgradeOneStart;
        up.first.level = kUpgradeOneStart;
    } else if (u == kUpgradeTwoFlag) {
        up.second.bought = kUpgradeTwoStart;
        up.second.level = kUpgradeTwoStart;
    } else if (int16_t(u) >= kFirstGift && int16_t(u) <= kLastGift) {
        toolKitBalanceAdd(int16_t(u) == kBigGift ? kBigGiftBalance : kGiftBalance);
        clearStoryFlag(u);
        setStoryFlag(kGiftGivenFlags[u - uint32_t(kFirstGift)]);
    }
}

// ---- the menus ---------------------------------------------------------------------------------------------------
// exe 0x5d1ff0: the main menu (the clerk's greeting, the choice of window 2)
void mainMenu(ScriptState& block) {
    ShopState& s = shopState();
    switch (s.step) {
    case 0:
        closeShopWindows();
        s.step += 1u;
        [[fallthrough]];
    case 1:
        if (windowOpen(kClerkWindow, shopBank(), line(ShopLine::Greeting)) == -1) return;
        s.step += 1u;
        [[fallthrough]];
    case 2:
        if (windowOpen(kMenuWindow, shopBank(), kMainMenuMessage) != -1) s.step += 1u;
        return;
    case 3:
        if (windowStatus(kMenuWindow) != 0) return;
        switch (MainAnswer(answer(kMenuWindow))) {
        case MainAnswer::Buy:
            s.enterMenu(ShopMenu::Buy);
            if (s.kind == 1) {
                s.step = 1;
                close(kClerkWindow);
                return;
            }
            break;
        case MainAnswer::Sell:
            s.enterMenu(ShopMenu::Sell);
            close(kClerkWindow);
            return;
        case MainAnswer::Upgrades:
            s.enterMenu(ShopMenu::Upgrades);
            close(kClerkWindow);
            return;
        case MainAnswer::Leave:
        case MainAnswer::Cancel: block.enterState(uint8_t(ShopBlockState::Goodbye)); break;
        default: break;
        }
        close(kClerkWindow);
        return;
    default: return;
    }
}

// exe 0x5d2170: the page (the items / the parts) of the buy / sell list
void pageMenu() {
    ShopState& s = shopState();
    switch (s.subStep) {
    case 0:
        closeShopWindows();
        s.subStep += 1u;
        break;
    case 1: break;
    case 2: goto openChoice;
    case 3: goto wait;
    default: return;
    }
    if (windowOpen(kClerkWindow, shopBank(),
                   s.currentMenu() == ShopMenu::Buy ? line(ShopLine::BuyPage) : line(ShopLine::SellPage)) == -1)
        return;
    s.subStep += 1u;
openChoice:
    if (windowOpen(kMenuWindow, shopBank(), kPageMessage) == -1) return;
    s.subStep += 1u;
wait:
    if (windowStatus(kMenuWindow) != 0) return;
    switch (PageAnswer(answer(kMenuWindow))) {
    case PageAnswer::Items:
        s.page = 0;
        s.step = 1;
        s.subStep = 0;
        close(kClerkWindow);
        return;
    case PageAnswer::Parts:
        s.page = 1;
        s.step = 2;
        s.subStep = 0;
        close(kClerkWindow);
        return;
    case PageAnswer::Back:
    case PageAnswer::Cancel: s.enterMenu(ShopMenu::Main); break;
    default: break;
    }
    close(kClerkWindow);
}

// exe 0x5d22b0: the item list; buy (+2 = 1) or sell (+2 = 2) the one under the hand
void listMenu() {
    ShopState& s = shopState();
    uint8_t selling = 0;
    uint32_t mode = 0;
    if (s.currentMenu() == ShopMenu::Sell) {
        selling = kListSelling;
        mode = kListSelling;
    }
    if (s.step == 2) mode = selling | kListParts;
    auto tail = [&]() { // the list drawing (exe 0x5d2cf0) while window 2 waits
        if (windowStatus(kMenuWindow) & kWindowStatusWaiting) shopListDraw(s, mode, gDraw);
    };
    const uint8_t c = s.subStep;
    if (c == 0) {
        switch (s.listStep) {
        case 0:
            close(kMenuWindow);
            s.list.row = 0;
            s.list.firstRow = 0;
            s.list.rowStep = 0;
            s.list.scrollStep = 0;
            s.unread0a = 0;
            s.unread0b = 0;
            s.listStep += 1u;
            [[fallthrough]];
        case 1:
            if (windowOpen(kMenuWindow, shopBank(), kListMessage) == -1) break;
            s.listStep += 1u;
            [[fallthrough]];
        case 2:
            close(kListWindow);
            buildList(mode);
            listCursorStep(s.list);
            if (s.list.rows == 0) s.list.row = kListNoRow;
            s.listStep += 1u;
            [[fallthrough]];
        case 3:
            if (windowOpen(kListWindow, shopBank(), kListHeaderMessage) == -1) break;
            s.listStep += 1u;
            [[fallthrough]];
        case 4:
            if (windowStatus(kMenuWindow) & kWindowStatusWaiting) s.listStep += 1u;
            break;
        case kListBrowse: {
            const ListAnswer r = listCursorStep(s.list);
            s.item = int16_t(shopGlobals().itemList[uint32_t(s.list.firstRow) + s.list.row]);
            if ((windowStatus(kListWindow) & kWindowStatusWaiting) && s.list.row != kListNoRow)
                windowOpen(kClerkWindow, menuBanks().descs, int16_t(s.item - kFirstItem));
            if (r != ListAnswer::None) {
                if (uint16_t(s.item) == kShopBack || r == ListAnswer::Cancelled) {
                    close(kMenuWindow);
                    s.listStep = kListLeave;
                } else {
                    s.listStep = 0;
                    s.subStep = uint8_t((s.currentMenu() != ShopMenu::Buy ? 1u : 0u) + 1u);
                }
            }
            break;
        }
        case kListLeave:
            if (windowStatus(kMenuWindow) != 0) break;
            if (s.kind == 0) {
                s.step = 0;
                s.listStep = 0;
            } else if (selling == 0) {
                s.enterMenu(ShopMenu::Main);
            } else {
                s.step = 0;
                s.listStep = 0;
            }
            break;
        default: break;
        }
        tail();
        return;
    }
    auto backToList = [&]() {
        s.subStep = 0;
        s.listStep = kListBrowse;
    };
    auto reply = [&](uint8_t k) { // the clerk's reply, then back to the list
        if (windowOpen(kClerkWindow, shopBank(), k) == -1) return;
        s.listStep += 1u;
        if (windowStatus(kClerkWindow) == 0) backToList();
    };
    auto waitReply = [&]() {
        if (windowStatus(kClerkWindow) == 0) backToList();
    };
    if (c == 1) { // buy
        switch (s.listStep) {
        case 0: {
            close(kClerkWindow);
            close(kQuestionWindow);
            const int32_t pr = shopPrice(s.item, true);
            s.price = pr;
            if (gameState().zenny < pr) {
                s.listStep = kListNotEnough;
                break;
            }
            s.listStep += 1u;
            [[fallthrough]];
        }
        case 1:
            if (windowStatus(kListWindow) & kWindowStatusWaiting) {
                const uint16_t id = uint16_t(s.item);
                const uint8_t k = (id < kFirstGift || id > kLastGift) ? line(ShopLine::PriceQuestion)
                                                                      : line(ShopLine::GiftQuestion);
                if (windowOpen(kQuestionWindow, shopBank(), k) != -1) s.listStep += 1u;
            }
            break;
        case 2: {
            if (windowStatus(kQuestionWindow) != 0) break;
            const uint8_t n = answer(kQuestionWindow);
            if (n == kYes) {
                buy(s.item);
                buildList(mode);
                listCursorStep(s.list);
                s.listStep += 1u;
            } else if (n < kLastAnswer) {
                backToList();
            }
            break;
        }
        case 3: reply(line(ShopLine::Thanks)); break;
        case 4:
        case kListNotEnough + 1: waitReply(); break;
        case kListNotEnough: reply(line(ShopLine::NotEnoughZenny)); break;
        default: break;
        }
        tail();
        return;
    }
    if (c == 2) { // sell
        switch (s.listStep) {
        case 0:
            close(kClerkWindow);
            close(kQuestionWindow);
            s.listStep += 1u;
            [[fallthrough]];
        case 1:
            if ((windowStatus(kListWindow) & kWindowStatusWaiting) &&
                windowOpen(kQuestionWindow, shopBank(), line(ShopLine::PriceQuestion)) != -1)
                s.listStep += 1u;
            break;
        case 2: {
            if (windowStatus(kQuestionWindow) != 0) break;
            const uint8_t n = answer(kQuestionWindow);
            if (n == kYes) {
                const uint16_t id = uint16_t(s.item);
                setStoryFlag(uint32_t(id) + kForSaleFlags);
                clearStoryFlag(id);
                if (int16_t(id) == kSoldBack) {
                    toolKitBalanceAdd(kSoldBackBalance);
                    clearStoryFlag(uint32_t(id) + kForSaleFlags);
                } else if (int16_t(id) > kNotResold && int16_t(id) < kNotResoldEnd) {
                    clearStoryFlag(uint32_t(id) + kForSaleFlags);
                }
                const int32_t v = (shopPrice(s.item, false) + 3) >> 2;
                s.price = v;
                zennyAdd(v);
                buildList(mode);
                listCursorStep(s.list);
                s.listStep += 1u;
            } else if (n < kLastAnswer) {
                backToList();
            }
            break;
        }
        case 3: reply(line(ShopLine::Thanks)); break;
        case 4: waitReply(); break;
        default: break;
        }
    }
    tail();
}

// exe 0x5d27b0: the buster upgrades (owning their item kUpgradeOneFlag / kUpgradeTwoFlag; the level raised to the one
// bought for the levels' price)
void upgradesMenu() {
    ShopState& s = shopState();
    auto confirm = [&]() { // window 3 answered: yes -> pay, else back to the menu
        if (windowStatus(kQuestionWindow) != 0) return;
        const uint8_t n = answer(kQuestionWindow);
        if (n == kYes) {
            s.subStep = kUpgradePay;
            return;
        }
        if (n < kLastAnswer) {
            s.step = 0;
            s.subStep = 0;
        }
    };
    auto done = [&](uint8_t k) { // the clerk's thanks, then the menu
        if (windowOpen(kClerkWindow, shopBank(), k) == -1) return;
        s.subStep += 1u;
        if (windowStatus(kClerkWindow) == 0) {
            s.step = 0;
            s.subStep = kUpgradeMenuBack;
        }
    };
    auto level = [&](uint32_t f, ShopUpgrade& up, int32_t unit, uint8_t ask) {
        switch (s.subStep) {
        case 0:
            close(kQuestionWindow);
            if (!storyFlag(f)) {
                s.step = 3;
                s.subStep = 0;
                return;
            }
            if (uint8_t(up.level) == uint8_t(up.bought)) {
                s.step = 3;
                s.subStep = kUpgradeAtMax;
                return;
            }
            s.price = (int32_t(up.bought) - up.level) * unit;
            s.subStep += 1u;
            [[fallthrough]];
        case 1:
            messageWindow(size_t(kQuestionWindow)).number = s.price;
            if (windowOpen(kQuestionWindow, shopBank(), ask) == -1) return;
            s.subStep += 1u;
            [[fallthrough]];
        case 2: confirm(); return;
        case kUpgradePay:
            if (gameState().zenny < s.price) {
                s.step = 3;
                s.subStep = kUpgradeNotEnough;
                return;
            }
            zennyAdd(-s.price);
            up.level = up.bought;
            s.subStep += 1u;
            [[fallthrough]];
        case 6:
            close(kClerkWindow);
            s.subStep += 1u;
            [[fallthrough]];
        case 7: done(line(ShopLine::UpgradeThanks)); return;
        case 8:
            if (windowStatus(kClerkWindow) == 0) {
                s.step = 0;
                s.subStep = kUpgradeMenuBack;
            }
            return;
        default: return;
        }
    };
    switch (s.step) {
    case 0:
        switch (s.subStep) {
        case 0:
            closeShopWindows();
            s.subStep += 1u;
            [[fallthrough]];
        case 1:
            if (windowOpen(kClerkWindow, shopBank(), line(ShopLine::UpgradesGreeting)) == -1) return;
            s.subStep += 1u;
            [[fallthrough]];
        case 2:
            if (windowOpen(kMenuWindow, shopBank(), kUpgradesMenuMessage) == -1) return;
            s.subStep += 1u;
            return;
        case 3:
            if (windowStatus(kMenuWindow) != 0) return;
            switch (UpgradesAnswer(answer(kMenuWindow))) {
            case UpgradesAnswer::One:
                s.step = 1;
                s.subStep = 0;
                close(kClerkWindow);
                return;
            case UpgradesAnswer::Two:
                s.step = 2;
                s.subStep = 0;
                close(kClerkWindow);
                return;
            case UpgradesAnswer::Back:
            case UpgradesAnswer::Cancel: s.subStep = kUpgradeMenuBack; break;
            default: break;
            }
            close(kClerkWindow);
            return;
        case kUpgradeMenuBack: s.enterMenu(ShopMenu::Main); return;
        default: return;
        }
    case 1: level(kUpgradeOneFlag, shopUpgrades().first, kUpgradeOneUnit, line(ShopLine::UpgradeOneQuestion)); return;
    case 2: level(kUpgradeTwoFlag, shopUpgrades().second, kUpgradeTwoUnit, line(ShopLine::UpgradeTwoQuestion)); return;
    case 3: { // the clerk's line why there is nothing to pay, then the menu
        uint8_t k;
        switch (s.subStep) {
        case 0: k = line(ShopLine::UpgradeNotOwned); break;
        case kUpgradeAtMax: k = line(ShopLine::UpgradeAtMax); break;
        case kUpgradeNotEnough: k = line(ShopLine::UpgradeNotEnough); break;
        case kUpgradeAnswerShown:
            if (windowStatus(kClerkWindow) == 0) {
                s.step = 0;
                s.subStep = 0;
            }
            return;
        default: return;
        }
        if (windowOpen(kClerkWindow, shopBank(), k) != -1) s.subStep = kUpgradeAnswerShown;
        return;
    }
    default: return;
    }
}

// exe 0x5d1fd0: the menu's handler (exe table 0x8d1ca0); buy / sell by their step's (0x8d1cb0 / 0x8d1cbc)
void menuTick(ScriptState& block) {
    ShopState& s = shopState();
    const RoutineRef& h = shopMenuHandlers[s.menu];
    switch (h.id()) {
    case RoutineId::ShopMainMenu: mainMenu(block); return;
    case RoutineId::ShopBuyMenu:
    case RoutineId::ShopSellMenu: {
        const RoutineRef& sub = h.is(RoutineId::ShopBuyMenu) ? buyStepHandlers[s.step] : sellStepHandlers[s.step];
        switch (sub.id()) {
        case RoutineId::ShopPageMenu: pageMenu(); return;
        case RoutineId::ShopListMenu: listMenu(); return;
        default: notPortedAt(kShopPort, "shop sub-menu", sub.pointerValue());
        }
    }
    case RoutineId::ShopUpgradesMenu: upgradesMenu(); return;
    default: notPortedAt(kShopPort, "shop menu", s.menu);
    }
}

} // namespace

void shopTick(ScriptState& b) {
    ShopState& s = shopState();
    switch (shopBlockHandlers[b.state].id()) {
    case RoutineId::ShopSetUp:
        windowLayoutVariant(WindowLayout::Shop);
        scriptSequenceStart(stateAddress(TableId::ShopCameraScript), stateAddress(TableId::ShopCameraSteps));
        scriptReplayPadOn();
        scriptSequenceTick();
        s.clearBytes();
        s.kind = storyFlag(kShopKindFlag) ? 1 : 0;
        stockByStory();
        stockRefresh();
        b.enterState(uint8_t(ShopBlockState::Menus));
        return;
    case RoutineId::ShopMenus:
        if (b.steps->id == kScriptStepEnd) b.enterState(uint8_t(ShopBlockState::Goodbye));
        else menuTick(b);
        scriptSequenceTick();
        return;
    case RoutineId::ShopGoodbye: // the clerk's goodbye, then the end of the block
        switch (b.subState) {
        case 0:
            close(kClerkWindow);
            b.subState += 1u;
            [[fallthrough]];
        case 1:
            if (windowOpen(kClerkWindow, shopBank(), line(ShopLine::Goodbye)) == -1) return;
            b.subState += 1u;
            [[fallthrough]];
        case 2:
            if (windowStatus(kClerkWindow) == 0) b.subState += 1u;
            return;
        case 3:
            clearStoryFlag(kShopOpenFlag);
            clearStoryFlag(kShopKindFlag);
            close(kListWindow);
            windowLayoutVariant(WindowLayout::Dialogue);
            b.subState += 1u;
            return;
        case 4:
            scriptReplayPadOff();
            scriptEndBlock();
            cameraState().setMode(CameraMode::Blend);
            messageSystem().waitBits &= ~1u;
            return;
        default: return;
        }
    }
    notPortedAt(kShopPort, "shop state", b.state);
}

void setShopDraw(std::vector<ScreenPrim>* draw) { gDraw = draw; }

int32_t shopPrice(int16_t item, bool buying) {
    int32_t ax = item;
    if (ax < kFirstItem) return 0;
    if (ax >= kPriceShiftFrom) { // the ids 0x480 .. 0x57f read the price table 0x100 further on
        if (ax >= kPriceShiftEnd) return 0;
        ax += kPriceShift;
    }
    const int32_t id = int32_t(int16_t(ax));
    int32_t v;
    if (id == kLifeUpgrade) {
        const int32_t k = (int32_t(player().maxHitPoints) - kLifeBase) >> 4;
        if (k < 0 || k > kLifeLevels - 1) return 0;
        v = lifePrices[uint32_t(k)];
    } else if (id == kUpgradeOne || id == kUpgradeTwo) {
        int32_t k = id == kUpgradeOne ? shopUpgrades().first.bought : shopUpgrades().second.bought;
        if (k < 0 || k > kUpgradeMax) return 0;
        if (k > kUpgradePriceLevels) k = kUpgradePriceLevels;
        v = id == kUpgradeOne ? upgradeOnePrices[uint32_t(k)] : upgradeTwoPrices[uint32_t(k)];
    } else {
        v = id >= kPriceShiftEnd ? forSalePrices[uint32_t(id)] : itemPrices[uint32_t(id)];
    }
    if (v < 0) v = v * kPriceUnit;
    if (buying) {
        const BalanceTier tier = BalanceTier(gameState().balanceTier);
        if (tier == BalanceTier::High) v = v * 4 / 5;
        else if (tier == BalanceTier::Low) v = v * 6 / 5;
    }
    if (v >= kPriceMax + 1) v = kPriceMax;
    return v;
}

} // namespace od::port
