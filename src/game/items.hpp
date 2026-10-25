// The item ids of dash2.exe: the system strings' ids from kSpecialWeaponItems (0x380), grouped by kind. An item is owned
// while the story flag of the same number is set (and for sale while the flag + kForSaleFlags is: game/shop); the
// status sub-screen's lists, the shop and a new run's equipment name them by these ranges.
#pragma once

#include <cstdint>

namespace od::port {

constexpr uint32_t kSpecialWeaponItems = 0x380;  // + the special weapon (its owned flag; the system string id)
constexpr uint32_t kBusterPartItems = 0x394;     // + the buster part
constexpr uint32_t kHelmetItems = 0x3b8;         // + the helmet
constexpr uint32_t kArmourItems = 0x3bc;         // + the armour
constexpr uint32_t kLegItems = 0x3c8;            // + the leg part
constexpr uint32_t kKeyItems = 0x3d0;            // the key items 0x3d0 .. 0x3ff
constexpr uint32_t kDifficultyMarkItems = 0x3d8; // - the difficulty: the key item a new run of that difficulty owns
constexpr uint32_t kThirdSlotItem = 0x3d9;       // the buster's third part slot
constexpr uint32_t kFirstLetterItem = 0x3f7;     // the letters 0x3f7 .. 0x3fb (read on the items page)
constexpr uint32_t kUsableItems = 0x400;         // the usable items 0x400 .. 0x451
constexpr uint32_t kLifeCanisterItem = 0x400;    // the life refill canister (its charges: game/shop ShopUpgrades)
constexpr uint32_t kCureItem = 0x401;            // the status cure (its charges: the second pair)
constexpr uint32_t kEnergyRefillItem = 0x402;    // the special weapon's energy refill
constexpr uint32_t kFlameBarrierItem = 0x403;    // the leg key items the equipment page pictures
constexpr uint32_t kLightBarrierItem = 0x404;
constexpr uint32_t kForSaleFlags = 0x100;        // + an item: for sale in the shops
constexpr uint32_t kItemStringBase = 0x380;      // an item's system string: the id - this

// the system string of an id: an item (from kItemStringBase) names the string id - kItemStringBase, a lower id is the
// string itself
constexpr int16_t itemString(int id) { return int16_t(id >= int(kItemStringBase) ? id - int(kItemStringBase) : id); }

} // namespace od::port
