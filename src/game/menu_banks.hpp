// The message banks the menus read (the status sub-screen, the shops, the development room): the exe's Japanese banks,
// or a language pack's PS1 copies mapped elsewhere (the host sets them), and the lookup from the exe bank a row table
// names to the language's.
#pragma once

#include <cstdint>

namespace od::port {

// The banks (the exe's for Japanese: exeMenuBanks; a language pack maps its PS1 copies elsewhere)
struct MenuBanks {
    uint32_t names = 0;     // item names (id - 0x380, no page header, each ends fb 1f)
    uint32_t descs = 0;     // item descriptions (window 3 on the items page)
    uint32_t help = 0;      // the panel rows' help (window 0)
    uint32_t itemsHelp = 0; // the items page rows' help (window 0)
    uint32_t use = 0;       // the items page: "use X?", results, letters (window 1)
    uint32_t misc = 0;      // the weapon switch messages (window 1)
    uint32_t areas = 0;     // the area names (the panel: messages k and 0x20 + k of the area table)
    // the options page: the rows' help (a pack's is the PS1's: its own nine rows, see status_options) and the rows'
    // banks
    uint32_t optHelp = 0;
    uint32_t optCamera = 0, optLock = 0, optLockMode = 0, optVibration = 0;
    uint32_t optSound = 0, optVolume = 0, optDefaults = 0;
    // the equipment page (window 0): the rows' help, the special weapon's, the buster parts', the sort rows' (+ the
    // manual sort's messages 6 / 7), the body parts'
    uint32_t equipHelp = 0, equipWeapon = 0, equipBuster = 0, equipSort = 0;
    uint32_t equipBody = 0;
    // the junk shops (game/shop): the clerk / menu / price question messages (a page header per message; a pack's is
    // the PS1 shop overlays' bank) and the list's 7 price strings (exe 0x5d2cf0 prints into them: 0xc bytes each, u16
    // 2, 7 digits, the zenny sign, fb 1f; the PS1's are in its own encoding)
    uint32_t shop = 0, shopPrices = 0;
};
// the exe's banks (the data source's tables)
MenuBanks exeMenuBanks();
void setMenuBanks(const MenuBanks& b);
// the banks set (none set: the exe's)
const MenuBanks& menuBanks();
// The key configuration's banks (window 0's help, window 2's action lists): OpenDash's own where the options page built
// them (setKeyConfigBanks; 0: not), else the source's
void setKeyConfigBanks(uint32_t help, uint32_t lists);
uint32_t keyConfigHelpBank();
uint32_t keyConfigListsBank();
// an exe bank address -> the language pack's copy (the row tables name the exe's banks)
uint32_t langBank(uint32_t bank);
// the shop list's price strings: kShopPriceStrings strings of kShopPriceStringSize bytes from MenuBanks::shopPrices
constexpr uint32_t kShopPriceStrings = 7, kShopPriceStringSize = 0xc;

} // namespace od::port
