// The menus' message banks (see menu_banks.hpp).
#include "game/menu_banks.hpp"

#include "engine/data_source.hpp"

namespace od::port {

namespace {
MenuBanks gBanks;
bool gBanksSet = false;
uint32_t gKeysBank = 0, gListsBank = 0; // OpenDash's own banks of the key configuration (0: the source's)
} // namespace

MenuBanks exeMenuBanks() {
    MenuBanks b;
    b.names = stateAddress(TableId::MenuBankItemNames);
    b.descs = stateAddress(TableId::MenuBankItemDescriptions);
    b.help = stateAddress(TableId::MenuBankPanelHelp);
    b.itemsHelp = stateAddress(TableId::MenuBankItemsHelp);
    b.use = stateAddress(TableId::MenuBankItemUse);
    b.misc = stateAddress(TableId::MenuBankWeaponSwitchMessages);
    b.areas = stateAddress(TableId::MenuBankAreaNames);
    b.mapHelp = stateAddress(TableId::MenuBankMapHelp);
    b.mapDisplay = stateAddress(TableId::MenuBankMapDisplay);
    b.optHelp = stateAddress(TableId::MenuBankOptionsHelp);
    b.optCamera = stateAddress(TableId::MenuBankOptionsCamera);
    b.optLock = stateAddress(TableId::MenuBankOptionsLockOn);
    b.optLockMode = stateAddress(TableId::MenuBankOptionsLockOnMode);
    b.optVibration = stateAddress(TableId::MenuBankOptionsVibration);
    b.optSound = stateAddress(TableId::MenuBankOptionsSound);
    b.optVolume = stateAddress(TableId::MenuBankOptionsVolume);
    b.optDefaults = stateAddress(TableId::MenuBankOptionsDefaults);
    b.equipHelp = stateAddress(TableId::MenuBankEquipmentHelp);
    b.equipWeapon = stateAddress(TableId::MenuBankEquipmentWeapon);
    b.equipBuster = stateAddress(TableId::MenuBankEquipmentBuster);
    b.equipSort = stateAddress(TableId::MenuBankEquipmentSort);
    b.equipBody = stateAddress(TableId::MenuBankEquipmentBody);
    b.shop = stateAddress(TableId::MenuBankShop);
    b.shopPrices = stateAddress(TableId::MenuBankShopPriceStrings);
    return b;
}

void setMenuBanks(const MenuBanks& b) {
    gBanks = b;
    gBanksSet = true;
}
const MenuBanks& menuBanks() {
    if (!gBanksSet) gBanks = exeMenuBanks(); // the bound source's
    return gBanks;
}
void setKeyConfigBanks(uint32_t help, uint32_t lists) {
    gKeysBank = help;
    gListsBank = lists;
}
uint32_t keyConfigHelpBank() { return gKeysBank ? gKeysBank : stateAddress(TableId::MenuKeyConfigHelpBank); }
uint32_t keyConfigListsBank() { return gListsBank ? gListsBank : stateAddress(TableId::MenuKeyConfigListsBank); }

uint32_t langBank(uint32_t bank) {
    const MenuBanks exe = exeMenuBanks();
    const MenuBanks& set = menuBanks();
    if (bank == exe.names) return set.names;
    if (bank == exe.descs) return set.descs;
    if (bank == exe.help) return set.help;
    if (bank == exe.itemsHelp) return set.itemsHelp;
    if (bank == exe.use) return set.use;
    if (bank == exe.misc) return set.misc;
    if (bank == exe.areas) return set.areas;
    if (bank == exe.mapHelp) return set.mapHelp;
    if (bank == exe.mapDisplay) return set.mapDisplay;
    if (bank == exe.optHelp) return set.optHelp;
    if (bank == exe.optCamera) return set.optCamera;
    if (bank == exe.optLock) return set.optLock;
    if (bank == exe.optLockMode) return set.optLockMode;
    if (bank == exe.optVibration) return set.optVibration;
    if (bank == exe.optSound) return set.optSound;
    if (bank == exe.optVolume) return set.optVolume;
    if (bank == exe.optDefaults) return set.optDefaults;
    if (bank == exe.equipHelp) return set.equipHelp;
    if (bank == exe.equipWeapon) return set.equipWeapon;
    if (bank == exe.equipBuster) return set.equipBuster;
    if (bank == exe.equipSort) return set.equipSort;
    if (bank == exe.equipBody) return set.equipBody;
    return bank;
}

} // namespace od::port
