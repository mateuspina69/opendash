// The player's weapons: the equipment's stats (the weapon stat table by each weapon's levels), the room's weapon mode,
// the arms (the buster's, square; the special weapon's, triangle): raising them from the buttons, their handlers (the
// fire frame, the repeat window, the re-fire), the shots they spawn, the special weapon's gauge and its recharge, the
// charge markers, and the tests the sub-states make on the arm (may it fire, does it hold the moves).
#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/data_source.hpp"

namespace od::port {

struct Actor;
struct Player;
enum class PlayerAnim : uint8_t;

// The stats of a weapon at the player's level of each (exe 0x5734b0 / 0x5734e0 / 0x573510 / 0x573540)
enum class WeaponStat : uint8_t {
    Attack = 0,
    Energy = 1,
    Range = 2,
    Rate = 3,
    Extra =
        4, // the weapon's own fourth upgrade (the spread's fan, the crawler's turn step, the hunter's life); no table
};
// the stat's table value at the player's level of it (0 for weapon 0); WeaponStat::Extra has no table entry (a lookup
// error)
int weaponStat(const Player& p, int weapon, WeaponStat stat);
// the index of a weapon's stat level in Player::weaponLevels (weapon * 8 + the stat)
constexpr size_t weaponLevelIndex(int weapon, WeaponStat stat) { return size_t(weapon) * 8 + size_t(stat); }
// the weapons' maximum levels (exe table 0x8c4ad8, the levels' layout: 8 bytes by weapon, weaponLevelIndex)
constexpr size_t kWeaponIds = 0x14;
inline const DataTable<ScalarTable<int8_t, kWeaponIds * 8>> weaponMaxLevels{TableId::WeaponMaxLevels};
// the buster's slot in the weapon levels (the status sub-screen's equipment page sums the parts' bonuses into it)
constexpr int kBusterLevelWeapon = 2;
// exe 0x5734b0: the attack of weapon w at its level, halved at the hardest difficulty
int weaponAttack(const Player& p, int weapon);
// the player's level of a stat of weapon w (the weapon levels w * 8 + stat)
int8_t weaponLevel(const Player& p, int weapon, WeaponStat stat);
// the energy level from which a held / repeated special weapon is free (the hold, the second swing)
constexpr int8_t kFreeEnergyLevel = 3;
// the special weapon's gauge pays a shot; short of it the special weapon is disarmed (kArmSpecialSpentMask)
void paySpecialEnergy(Player& p);

// The room's weapon modes (exe 0x573570; every per-stage room function sets one)
constexpr int8_t kWeaponModeTown = 0;    // the town: the buster's kick, no special weapon
constexpr int8_t kWeaponModeField = 1;   // the field: the saved buster and special weapon
constexpr int8_t kWeaponModeSpecial = 2; // one special weapon on both buttons (the extinguisher, ST1E)
// exe 0x573570(mode): the room's weapons (mode < 0: only the mode byte)
void weaponMode(int8_t mode);
// exe 0x5721e0 + 0x572230 on the player (the sub-screen's close, a weapon changed)
void weaponReset();

// exe 0x573480: 1 = an arm up (or a refire asked for), -1 = the carry busy (holding / throwing), 0 = free
int weaponBusy(const Player& p);
// exe 0x572180: the arms, the carry, the lock-on, the charge and the targets cleared
void clearWeaponState(Player& p);
// exe 0x5721e0: the arm put down (the queued shots kept); the lock request ends with the lock suspended; the upper body
// back on the body's animation unless carrying
void putArmDown(Player& p);
// exe 0x572230: the arm may fire again (the buster: below its shots in flight; the special weapon: its gauge pays)
void enableFiring(Player& p);
// exe 0x572280 / 0x5722c0: the upper body's animation of the buster's / the special weapon's arm (the town's kick;
// moving, jumping or standing)
PlayerAnim busterArmAnim(const Player& p);
PlayerAnim specialArmAnim(const Player& p);

// the special weapon arm handler of weapon id `weapon` (exe table 0x8c4ef8; a null routine past its ids), whether the
// port has it (weapon kNoArmWeapon has none: its null entry counts as ported) and the weapon's shot type (its weapon
// record's: the global shot class table's type) -- the host's check of the weapons the port has
constexpr uint32_t kNoArmWeapon = 10;
RoutineRef specialArmHandlerOf(uint32_t weapon);
bool specialArmPorted(uint32_t weapon);
uint8_t weaponShotType(uint32_t weapon);

// The context the fire buttons are read in (exe 0x57ac60 / 0x57ae70)

enum class FireContext : int16_t { Standing = 0, Moving = 1, Airborne = 2 };
// exe 0x57ac60(player, context): the fire buttons (square: the buster, triangle: the special weapon) raise an arm, or
// take a queued / refire request; 1 = an arm went up
int fireButtons(Player& p, FireContext ctx);
// exe 0x57b1d0: a special weapon is equipped and its gauge pays a shot
bool specialEnergyReady(const Player& p);
// exe 0x57b110 / 0x57b170: the arm (or the throw) holds the moves / the turn in place
bool armHoldsMoves(const Player& p);
bool armHoldsTurns(const Player& p);
// exe 0x57b030: no arm up, or the arm's animation is past its fire frame
bool armPastFireFrame(const Player& p);
// exe 0x57b0a0: a held trigger whose weapon is not usable while moving (the player stops to fire)
bool mustStopToFire(const Player& p);
// exe 0x5729e0: the buster's arm handler; its shot request spawns the shot
void busterArmUpdate(Player& p);
// exe 0x572910: the special weapon's arm handler; its shot request spawns the shot
void specialArmUpdate(Player& p);
// exe 0x573600(refill): the special weapon's gauge set up for the equipped special weapon (refill:
// the energy reserve and pool = its energy stat at its level); its rate, shot cost and maximum from the gauge table, the
// gauge full
void setupSpecialGauge(bool refill);
// exe 0x573670: the special weapon's gauge: drained by the orbit status, refilled from the energy (or by itself)
void rechargeSpecialGauge(Player& p);
// the special weapon's charge (exe 0x575440's tail; held: the buttons held at the tick's start): the start marker while
// the charge weapon's trigger is held, the release marker when it is let go with a charge level above 1
void updateCharge(Player& p, uint16_t held);

// exe 0x5737b0(shot, r): the player's shield (its charge byte, kept by the shield weapon) stops a shot within r + 0xa0:
// one more block counted (at most 0xf, bit 0x10), a ring marker (pool 0xa0 global type 6) at the shot's previous
// position aimed at the player's body point; true = blocked
bool playerShieldBlocks(const Actor& shot, uint32_t radius);

} // namespace od::port
