// The rewards and costs the message scripts, the pickups, the shop and the stage scripts apply to the game's counters
// (dash2.exe 0x5e51a0 .. 0x5e5390): the zenny, the two balances and their tiers, the special weapon's energy. (The hit
// points' are game/hit_points.)
#pragma once

#include <cstdint>

namespace od::port {

struct Player;

// exe 0x5e51a0: the zenny += v unless it would go below 0 (-1, nothing changed); at most kZennyMax
constexpr int32_t kZennyMax = 9999999;
int zennyAdd(int32_t v);
// exe 0x5e51d0 (fb 3e): the good / bad balance += d (+-0x7fff) and its tier; returns the tier (BalanceTier)
int balanceAdd(int d);
// exe 0x5e5260 (fb 40): the tool kit balance += d and its tier; returns the tier
int toolKitBalanceAdd(int d);
// exe 0x5e5390(actor, v): the special weapon's energy pool += v, within 0 .. the reserve; -1 when it would go below 0
// (the pool emptied)
int weaponEnergyAdd(Player& p, int v);

} // namespace od::port
