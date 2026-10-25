// The shops of dash2.exe: exe 0x5d1e30, the stage callback (StageState::script8Callback) of the shop stages (ST0A, 1A,
// 1B, 25, 29, 3B, 3F), run as script 8 (handler 0x5128f0) once the shop NPC's message sets kShopOpenFlag. Native state,
// bit-exact against
// tests/data/story_*shop*.bin.
//
// The script block's states (ShopBlockState): the set-up, the menus, the clerk's goodbye. The menus (ShopMenu: the
// main menu, buy, sell, the buster upgrades) keep their state in ShopState; the item list is the u16 ids at kShopList
// (kShopBack ends it). Messages: window 4 the clerk (the exe bank 0x8bb1c4, ids by the shop's kind: ShopLine), window 2
// the menus' choices, window 1 the list's header, window 3 the price question; the item descriptions in the bank
// 0x8b7e64 (id - 0x380). A language pack's copies of both: game/menu_banks MenuBanks (shop, descs).
// The list itself (exe 0x5d2cf0: the names, prices, icons, the hand, the scroll arrows): shop.cpp's shopListDraw, its
// ScreenPrims into the sink of setShopDraw.
#pragma once

#include <cstdint>
#include <vector>

#include "engine/screen_prim.hpp"
#include "game/state_array.hpp"
#include "game/list_cursor.hpp"

namespace od::port {

struct ScriptState;

constexpr uint16_t kShopBack = 0x485;    // the list's "back"

// The shop's script block's state (+4 of game/script_state's block; the handlers of the exe table 0x8d1c94)
enum class ShopBlockState : uint8_t {
    SetUp = 0,   // exe 0x5d1e50: the shop layout, the camera script, the state cleared, the stock
    Menus = 1,   // exe 0x5d1ec0: the menus until the steps end
    Goodbye = 2, // exe 0x5d1ef0: the clerk's goodbye, the block's end
};

// +0: the menu
enum class ShopMenu : uint8_t {
    Main = 0,     // exe 0x5d1ff0: buy / sell / the upgrades / leave
    Buy = 1,      // exe 0x5d2130: the page, then the list
    Sell = 2,     // exe 0x5d2150
    Upgrades = 3, // exe 0x5d27b0: the buster upgrades
};

// The menus' state (0xa5aa60)
struct ShopState {
    uint8_t menu = 0;     // +0x00: ShopMenu
    uint8_t step = 0;     // +0x01: the menu's step (Buy / Sell: 1 the page, 2 the list)
    uint8_t subStep = 0;  // +0x02: the step's own (the list: 0 browsing, 1 buying, 2 selling)
    uint8_t listStep = 0; // +0x03: the list's / the purchase's
    ListCursor list;      // +4 .. +9: the list's cursor
    uint8_t unread0a = 0; // +0x0a: cleared with the cursor (their reader is not traced)
    uint8_t unread0b = 0;
    int16_t item = 0;     // +0x0c: the item under the hand
    uint8_t kind = 0;     // +0x0e: the shop's kind (kShopKindFlag: 1; its messages, its stock)
    uint8_t page = 0;     // +0x0f: the list's page (0 the items, 1 the parts)
    int32_t price = 0;    // +0x10: the price asked / paid

    ShopMenu currentMenu() const { return ShopMenu(menu); }
    // the menu m at step 0, the step bytes cleared (the exe's dword store at +0)
    void enterMenu(ShopMenu m) {
        menu = uint8_t(m);
        step = 0;
        subStep = 0;
        listStep = 0;
    }
    // the 0x14 bytes cleared one at a time (exe 0x40dc40(0xa5aa60, 0xa5aa70))
    void clearBytes() { *this = ShopState{}; }
};
inline ShopState& shopState() {
    static ShopState s;
    return s;
}

// The two buster upgrades' levels (0xa6d1f0: {level, the level bought} for upgrade flag 0x400, then for 0x401): the
// upgrades menu raises the level to the one bought, for its price. The status sub-screen's items page uses them as the
// charges of the items 0x400 (the life canister) / 0x401 (the cure): the charges left and their maximum
struct ShopUpgrade {
    int8_t level = 0;
    int8_t bought = 0;
};
struct ShopUpgrades {
    ShopUpgrade first;
    ShopUpgrade second;
    // the four bytes after them the save keeps: [0] (its reader is not traced), [1..3] the ST32 quiz's records of its
    // three courses (content/calbania/st32_quiz: rank | misses << 4)
    StateArray<uint8_t, 4> unreadBytes{};
};
inline ShopUpgrades& shopUpgrades() {
    static ShopUpgrades s;
    return s;
}

// exe 0x5d1e30(block): the shop's script block (script 8) one tick
void shopTick(ScriptState& block);
// where shopTick puts the list's primitives (exe 0x5d2cf0) this tick; null = none collected
void setShopDraw(std::vector<ScreenPrim>* draw);
// exe 0x5d1d00(item, buying): the price of an item (buying: with the good / bad tier's discount / mark-up), 0 = not sold
int32_t shopPrice(int16_t item, bool buying);

} // namespace od::port
