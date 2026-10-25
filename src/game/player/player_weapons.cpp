// The player's weapons (see player_weapons.hpp).
#include "game/actor_state.hpp"
#include "game/field_alias.hpp"
#include "game/player/player_weapons.hpp"

#include <cstdio>
#include <stdexcept>
#include <string>

#include "engine/data_source.hpp"
#include "game/actor_pools.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/distances.hpp"
#include "game/fixed_point.hpp"
#include "game/game_state.hpp"
#include "game/global_class_types.hpp"
#include "game/player/player_animation.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/resolution_error.hpp"
#include "game/shots/player_shots.hpp"
#include "game/shots/shot_common.hpp"

namespace od::port {

namespace {

// The weapon table (exe 0x8c4b68, 8 bytes per weapon id)
struct WeaponRecord : DataRecord {
    uint8_t fireFrame = 0;   // the arm animation's frame that fires
    uint8_t repeatFrom = 0;  // the repeat window (+ the rate stat)
    uint8_t repeatTo = 0;
    uint8_t repeats = 0;     // the trigger held fires again
    uint8_t usableMoving = 0; // fires while moving (else the player stops)
    int8_t allowsMoves = 0;  // the moves may start while the arm is up
    int8_t allowsTurns = 0;  // the turn in place may start
    uint8_t shotType = 0;    // the shot actor's type (pool 0x10, the global class table 0x8b1fb0)
    static WeaponRecord load(const TableReader& r) {
        WeaponRecord rec;
        rec.fireFrame = r.u8(0);
        rec.repeatFrom = r.u8(1);
        rec.repeatTo = r.u8(2);
        rec.repeats = r.u8(3);
        rec.usableMoving = r.u8(4);
        rec.allowsMoves = r.s8(5);
        rec.allowsTurns = r.s8(6);
        rec.shotType = r.u8(7);
        return rec;
    }
};
const DataTable<RecordTable<WeaponRecord, 18, 8>> weaponTable{TableId::PlayerWeaponsWeaponTable};
// the weapon table's record of a weapon id (the exe indexes it with the signed id)
WeaponRecord weaponRecord(int weapon) { return weaponTable[size_t(uint32_t(weapon))]; }
// The weapon stat table (exe 0x8c4eb0): per weapon id the address of its rows, 4 s16 stats (WeaponStat) per level
// One level of a weapon's stats (WeaponStat order) and a weapon's stats by level
struct WeaponStatTable : DataRecord {
    static constexpr size_t kLevels = 8, kStats = 4;
    static constexpr uint32_t kBytes = kLevels * kStats * 2;
    std::array<std::array<int16_t, kStats>, kLevels> levels{};
    static WeaponStatTable load(const TableReader& r) {
        WeaponStatTable t;
        for (size_t l = 0; l < kLevels; ++l)
            for (size_t k = 0; k < kStats; ++k) t.levels[l][k] = r.s16((l * kStats + k) * 2);
        return t;
    }
};
const DataTable<RecordPointerTable<WeaponStatTable, 18, WeaponStatTable::kBytes>> weaponStatTables{
    TableId::PlayerWeaponStatTables};
// The arms' handlers by weapon id (exe 0x8c4f40: the buster's, 0x8c4ef8: the special weapon's): the named routines
// (engine/routines.csv: the buster's arm for every buster; the generic special arm for weapons 1..3, 5..7, 9, 13, 14,
// 16, the cannon's for 4, the extinguisher's for 15, the Blade Arm's for 11)
const DataTable<RoutineTable<18>> busterArmHandlers{TableId::BusterArmHandlers};
const DataTable<RoutineTable<18>> specialArmHandlers{TableId::SpecialArmHandlers};

// The special weapons' gauges (exe 0x8c4bf8, 8 bytes per weapon id): its refill rate, a shot's cost, its maximum
struct GaugeRecord : DataRecord {
    int16_t rate = 0;
    int16_t cost = 0;
    int16_t max = 0;
    static GaugeRecord load(const TableReader& r) {
        GaugeRecord rec;
        rec.rate = r.s16(0);
        rec.cost = r.s16(2);
        rec.max = r.s16(4);
        return rec;
    }
};
const DataTable<RecordTable<GaugeRecord, 18, 8>> gaugeTable{TableId::PlayerWeaponsGaugeTable};

constexpr int16_t kInfiniteEnergy = 0x7fff;        // energyReserve: infinite
constexpr uint8_t kShotSweepGroup = kActorSweepGroup1;
constexpr int16_t kCannonRecoil = 0x300;           // the Buster Cannon's backward speed at the shot
constexpr int16_t kCannonRecoilDecay = 0xe0;
constexpr int16_t kBladeLunge = -0x200;            // the Blade Arm's forward speed before the swing
constexpr int16_t kBladeSecondLunge = -0x140;
constexpr int16_t kBladeLeapUp = -0x180;
constexpr int16_t kBladeLeapForward = -0x280;
constexpr int8_t kBladeLeapFrame = 5;
constexpr int16_t kBladeGravity = 0x30;
constexpr int16_t kBladeBrake = 0x20;
constexpr int8_t kBladeImmuneTicks = 0x18;   // the second swing's steady immunity
constexpr uint8_t kChargeLevelMask = 0x0f;
constexpr uint8_t kChargeMarkerKeep = 0xef;        // no marker spawned: the charge's bit 4 cleared
constexpr uint8_t kMarkerLoopFlags = kActorSweepGroup1 | kActorLateLoop;

// the arm raise modes (exe 0x57af30): the arm's flags; + kRaiseWithAnim: the arm animation asked for too
enum ArmRaise : uint32_t {
    kRaiseBusterLegsFree = 0, // the buster on the upper body only (mix 2): moving
    kRaiseBuster = 1,         // the buster, the whole body: standing
    kRaiseSpecialLegsFree = 2,
    kRaiseSpecial = 3,
    kRaiseWithAnim = 4,
};
constexpr uint32_t kRaiseSpecialBit = 2;

// exe 0x57af30(player, mode): the arm's flags; with the animation: the upper body's arm animation from its start
void raiseArm(Player& p, uint32_t mode) {
    p.armFlags &= kArmDownMask;
    const uint16_t f = p.armFlags;
    switch (mode) {
    case kRaiseBusterLegsFree: p.armFlags = uint16_t(f | kArmBuster | kArmUpperOnly | 2u); return;
    case kRaiseBuster: p.armFlags = uint16_t(f | kArmBuster); return;
    case kRaiseSpecialLegsFree: p.armFlags = uint16_t(f | kArmSpecial | kArmUpperOnly | 1u); return;
    case kRaiseSpecial: p.armFlags = uint16_t(f | kArmSpecial); return;
    case kRaiseWithAnim + kRaiseBusterLegsFree:
        p.armFlags = uint16_t(f | kArmBuster | kArmUpperOnly | 2u);
        p.upper.anim = uint8_t(busterArmAnim(p));
        break;
    case kRaiseWithAnim + kRaiseBuster:
        p.armFlags = uint16_t(f | kArmBuster);
        p.upper.anim = uint8_t(busterArmAnim(p));
        break;
    case kRaiseWithAnim + kRaiseSpecialLegsFree:
        p.armFlags = uint16_t(f | kArmSpecial | kArmUpperOnly | 1u);
        p.upper.anim = uint8_t(specialArmAnim(p));
        break;
    case kRaiseWithAnim + kRaiseSpecial:
        p.armFlags = uint16_t(f | kArmSpecial);
        p.upper.anim = uint8_t(specialArmAnim(p));
        break;
    default: break;
    }
    p.resetArmStep(0);
    p.upper.prevAnim = 0;
    p.upper.key.frame = 0;
    if (!(p.armFlags & kArmUpperOnly) && int8_t(p.body.anim) == int8_t(p.body.prevAnim))
        requestAnimId(p, int8_t(p.upper.anim), 0, true);
}

// exe 0x57ae70(player, mode, context, usable): the arm raised unless the special weapon cannot pay, or the auto-lock
// option waits for the lock; standing: the whole-body raise; moving with a weapon not usable while moving: the player
// stops instead (0); airborne: only when usable
int tryRaiseArm(Player& p, uint32_t mode, FireContext ctx, int16_t usable) {
    if ((mode & kRaiseSpecialBit) && !specialEnergyReady(p)) return 0;
    if (p.lockTarget != 0) {
        const bool lockReached = (p.lockFlags & (kLockPitchOn | kLockYawOn)) == (kLockPitchOn | kLockYawOn);
        if (!(mode & kRaiseSpecialBit)) {
            if (gameOptions().autoLockBuster != 0 && !lockReached) return 0;
        } else if (gameOptions().autoLockSpecial != 0 && !lockReached) {
            return 0;
        }
    }
    if (ctx == FireContext::Standing) {
        mode = uint16_t(mode + 1);
    } else if (ctx == FireContext::Moving) {
        if (usable == 0) {
            p.subState = uint8_t(PlayerSub::Idle);
            p.enterStep(0);
            return 0;
        }
    } else {
        if (ctx != FireContext::Airborne) return 1;
        if (usable == 0) return 0;
    }
    raiseArm(p, mode);
    return 1;
}

// the upper body's key frame (the arm animation's) and its end
int8_t upperFrame(const Player& p) { return p.upper.key.frame; }

// exe 0x573150: the buster's arm (step +0x13e): 0 wait for the fire frame -> the shot; 1 the arm animation restarted at
// the fire frame + the shot; 2 / 3 the repeat window (the trigger held -> kArmBusterRefire); 4 the arm animation's end ->
// the arm down
void busterArm(Player& p) {
    const int8_t step = p.armStep;
    const uint16_t held = p.held;
    const int w = p.activeBuster;
    const WeaponRecord t = weaponRecord(w);
    switch (step) {
    case 0:
        if (int(upperFrame(p)) == int(t.fireFrame)) {
            p.setArmBits(kArmBusterShot);
            p.armStep = int8_t(step + 2);
        }
        return;
    case 1: {
        if (!(p.armFlags & kArmUpperOnly)) startBodyAnimation(p, PlayerAnim(p.body.anim), t.fireFrame);
        startUpperAnimation(p, uint8_t(p.armFlags & kArmMixMask), busterArmAnim(p), t.fireFrame);
        p.setArmBits(kArmBusterShot);
        p.armStep = int8_t(p.armStep + 1);
        return;
    }
    case 2:
        p.armFlags &= uint16_t(~kArmShots);
        if (t.repeats == 0) {
            p.armStep = int8_t(step + 2);
            return;
        }
        if (int(upperFrame(p)) != weaponStat(p, w, WeaponStat::Rate) + int(t.repeatFrom)) return;
        p.armStep = int8_t(p.armStep + 1);
        [[fallthrough]];
    case 3:
        if (int(upperFrame(p)) == int(t.repeatTo)) {
            p.armStep = int8_t(p.armStep + 1);
            return;
        }
        if ((held & p.button(PlayerButton::Fire)) && (p.armFlags & kArmBusterReady) == kArmBusterReady)
            p.armFlags = uint16_t((p.armFlags & kArmDownMask) | kArmBusterRefire);
        return;
    case 4:
        if (p.upperAnimEnded()) p.armFlags &= kArmOffMask;
        return;
    default: return;
    }
}

// exe 0x572d10: the arm of weapon 15 (the extinguisher, ST1E), step +0x13e: 0 the fire frame of the body's animation ->
// the shot; 1 the shot; 2 while the trigger is held and the gauge pays, the spraying frames held (back to the repeat
// start at their end), else 3; 3 the arm-down frames; 4 the animation's end -> the arm down. Not on a slippery floor:
// the motion stopped
void extinguisherArm(Player& p) {
    const WeaponRecord t = weaponRecord(p.activeSpecial);
    const int8_t step = p.armStep;
    const int8_t frame = p.body.key.frame;
    switch (step) {
    case 0:
        if (int(frame) == int(t.fireFrame)) {
            p.setArmBits(kArmSpecialShot);
            p.armStep = int8_t(step + 2);
        }
        break;
    case 1:
        p.setArmBits(kArmSpecialShot);
        p.armStep = int8_t(step + 1);
        break;
    case 2:
        p.armFlags &= uint16_t(~kArmShots);
        if (!p.holds(PlayerButton::Special) || p.gauge < p.shotCost || int(t.repeatTo) <= int(frame)) {
            p.armStep = int8_t(step + 1);
        } else if (int(t.repeatFrom) + 2 <= int(frame)) {
            startBodyAnimation(p, PlayerAnim(p.body.anim), t.repeatFrom);
        }
        break;
    case 3:
        startBodyAnimation(p, PlayerAnim(p.body.anim), t.repeatTo);
        p.armStep = int8_t(p.armStep + 1);
        break;
    case 4:
        if (p.bodyAnimEnded()) p.armFlags &= kArmOffMask;
        break;
    default: break;
    }
    if (p.slippery == 0) {
        p.velocity.x = 0;
        p.velocity.z = 0;
    }
}

// exe 0x572e50: the arm of weapon 11 (the Blade Arm). specialFiring = 1 / 2 while the swing lives (the blade shot, pool
// 0x10 type 10, frees itself without it). Step +0x13e: 0 the fire frame -> the shot, else the forward lunge; 1 the
// request cleared; 2 the first swing (its end: the arm's moving animation -> 8); a second press of the trigger during
// it pays the gauge (free from the energy stat level kFreeEnergyLevel) -> 3: the second swing (the lunge, the
// steady immunity) -> 4: at frame 5 the leap -> 5 / 7 the landing -> 6 / 8 / 9 the end (the arm down, on the ground).
// Airborne: gravity; on the ground a backward speed brakes
void bladeArm(Player& p) {
    const int w = p.activeSpecial;
    const int8_t step = p.armStep;
    const bool animEnd = p.bodyAnimEnded();
    switch (step) {
    case 0:
        if (int(p.body.key.frame) == int(weaponRecord(w).fireFrame)) {
            p.setArmBits(kArmSpecialShot);
            p.specialFiring = 1;
            p.armStep = int8_t(step + 1);
        } else {
            p.velocity.z = kBladeLunge;
        }
        break;
    case 1:
        p.armFlags &= uint16_t(~kArmShots);
        p.specialFiring = 1;
        p.armStep = int8_t(step + 1);
        break;
    case 2:
        p.specialFiring = 1;
        if (animEnd) {
            startBodyAnimation(p, PlayerAnim::SpecialArmMoving, 0);
            p.armStep = int8_t(step + 6);
        } else if (p.pressedNow(PlayerButton::Special)) {
            if (weaponLevel(p, w, WeaponStat::Energy) < kFreeEnergyLevel) {
                if (p.shotCost <= p.gauge) {
                    p.gauge = wrap16(p.gauge - p.shotCost);
                    p.armStep = int8_t(step + 1);
                }
            } else {
                p.armStep = int8_t(step + 1);
            }
        }
        break;
    case 3:
        p.specialFiring = 1;
        if (animEnd) {
            startBodyAnimation(p, PlayerAnim::BladeSecondSwing, 0);
            p.velocity.z = kBladeSecondLunge;
            p.immunity = kImmuneSteady;
            p.immunityTicks = kBladeImmuneTicks;
            p.armStep = int8_t(step + 1);
        }
        break;
    case 4:
        p.specialFiring = 2;
        if (p.body.key.frame == kBladeLeapFrame) {
            soundPlayAtPlayer(uint16_t(PlayerSound::Jump));
            p.airborne = kAirborneFalling;
            p.velocity.y = kBladeLeapUp;
            p.velocity.z = kBladeLeapForward;
            p.armStep = int8_t(step + 1);
        }
        break;
    case 5:
        p.specialFiring = 2;
        if (p.airborne == 0) {
            soundPlayAtPlayer(uint16_t(PlayerSound::Land));
            p.armStep = int8_t(step + 1);
        } else if (animEnd) {
            p.armStep = int8_t(step + 2);
        }
        break;
    case 6:
        p.specialFiring = 2;
        if (animEnd) {
            startBodyAnimation(p, PlayerAnim::BladeLanding, 0);
            if (p.slippery == 0) p.velocity.z = 0;
            p.armStep = int8_t(step + 3);
        }
        break;
    case 7:
        p.specialFiring = 2;
        if (p.airborne != 0) break; // airborne: gravity below
        startBodyAnimation(p, PlayerAnim::BladeLanding, 0);
        soundPlayAtPlayer(uint16_t(PlayerSound::Land));
        if (p.slippery == 0) p.velocity.z = 0;
        p.armStep = int8_t(step + 2);
        break;
    case 8:
        p.specialFiring = 1;
        if (p.body.key.frame > 0) {
            if (p.slippery == 0) p.velocity.z = 0;
            p.armStep = int8_t(step + 1);
        }
        break;
    case 9:
        if (animEnd) {
            p.armFlags &= kArmOffMask;
            p.airborne = 0;
        }
        break;
    default: break;
    }
    if (p.airborne == 0) {
        if (p.slippery == 0 && p.velocity.z < 0) p.velocity.z = wrap16(p.velocity.z + kBladeBrake);
        return;
    }
    p.velocity.y = wrap16(p.velocity.y + kBladeGravity);
}

// exe 0x572c50: the arm of weapon 4 (the Buster Cannon): the shot at the fire frame of the upper body with the recoil
// (the backward speed), then the recoil decays on the ground until the arm animation ends
void cannonArm(Player& p) {
    const int8_t step = p.armStep;
    switch (step) {
    case 0:
        if (int(upperFrame(p)) != int(weaponRecord(p.activeSpecial).fireFrame)) return;
        p.setArmBits(kArmSpecialShot);
        p.velocity.z = kCannonRecoil;
        p.armStep = int8_t(step + 2);
        return;
    case 1:
        p.setArmBits(kArmSpecialShot);
        p.armStep = int8_t(step + 1);
        return;
    case 2:
        p.armFlags &= uint16_t(~kArmShots);
        p.armStep = int8_t(step + 1);
        return;
    case 3:
        if (p.slippery == 0) {
            p.velocity.z = wrap16(p.velocity.z - kCannonRecoilDecay);
            if (p.velocity.z < 0) p.velocity.z = 0;
        }
        if (p.upperAnimEnded()) p.armFlags &= kArmOffMask;
        return;
    default: return;
    }
}

// exe 0x572aa0: the arm of weapons 1..3, 5..7, 9, 13, 14, 16: the shot at the fire frame, the re-aimed animations
// (step 1), the hold-to-repeat window (the weapon repeats and the gauge pays: fire again at the rate stat's frame) and
// the refire request while the trigger is held
void genericSpecialArm(Player& p) {
    const uint16_t held = p.held;
    const int w = p.activeSpecial;
    const WeaponRecord t = weaponRecord(w);
    const int8_t step = p.armStep;
    switch (step) {
    case 0:
        if (int(upperFrame(p)) != int(t.fireFrame)) return;
        p.setArmBits(kArmSpecialShot);
        p.armStep = int8_t(step + 2);
        return;
    case 1:
        if (!(p.armFlags & kArmUpperOnly)) startBodyAnimation(p, PlayerAnim(p.body.anim), t.fireFrame);
        startUpperAnimation(p, uint8_t(p.armFlags & kArmMixMask), specialArmAnim(p), t.fireFrame);
        p.setArmBits(kArmSpecialShot);
        p.armStep = int8_t(p.armStep + 1);
        return;
    case 2: {
        p.armFlags &= uint16_t(~kArmShots);
        if (t.repeats == 0 || p.gauge < p.shotCost) {
            p.armStep = int8_t(step + 2);
            return;
        }
        if (int(upperFrame(p)) != weaponStat(p, w, WeaponStat::Rate) + int(t.repeatFrom)) return;
        p.armStep = int8_t(p.armStep + 1);
        [[fallthrough]];
    }
    case 3:
        if (int(t.repeatTo) <= int(upperFrame(p))) {
            p.armStep = int8_t(p.armStep + 1);
            return;
        }
        if ((held & p.button(PlayerButton::Special)) && (p.armFlags & kArmSpecialReady) == kArmSpecialReady)
            p.armFlags = uint16_t((p.armFlags & kArmDownMask) | kArmSpecialRefire);
        return;
    case 4:
        if (p.upperAnimEnded()) p.armFlags &= kArmOffMask;
        return;
    default: return;
    }
}

// a shot of pool 0x10 the arm spawned: its type, aimed at the lock-on target's hit point (variant 1) or the screen
// centre's (variant 0)
void aimShot(Player& p, ShotActor& shot) {
    auto&& aim = PlayerShotFields(shot);
    if (p.lockTarget != 0 && p.lockSuspended == 0) {
        shot.variant = 1;
        aim.aimPoint = p.lockAimPoint;
        aim.target = p.lockedActor;
        return;
    }
    shot.variant = 0;
    aim.aimPoint = p.screenAimPoint;
    aim.target = p.softTargetActor;
}

// the charge markers (pool 0x60, kMarkerLoopFlags)
uint32_t chargeMarker(GlobalStaticType type) {
    const uint32_t a = allocActor60(0); // exe 0x5e1300
    if (a == 0) return 0;
    StaticActor& m = staticActorAt(a);
    m.flags = kActorLive;
    m.type = typeByte(type);
    m.loopFlags |= kMarkerLoopFlags;
    m.variant = 0;
    return a;
}

// exe 0x575828: the charge's start marker: charge = 1 when spawned, else its bit 4 cleared
void chargeStartMarker(Player& p) {
    if (p.charge == 0 && chargeMarker(GlobalStaticType::ChargeMarker) != 0) {
        p.charge = 1;
        return;
    }
    p.charge &= kChargeMarkerKeep;
}

// exe 0x57587a: the charge's release (the charge level - 1) at the body's bone point
void chargeReleaseMarker(Player& p) {
    const uint32_t a = chargeMarker(GlobalStaticType::ShockWave);
    if (a == 0) return;
    StaticActor& m = staticActorAt(a);
    m.params[0] = uint8_t((p.charge & kChargeLevelMask) - 1u);
    const ShortVec3& point = p.bonePoints[0];
    m.pos.x = int32_t(uint32_t(int32_t(point.x)) << 16);
    m.pos.y = int32_t(uint32_t(int32_t(point.y)) << 16);
    m.pos.z = int32_t(uint32_t(int32_t(point.z)) << 16);
}

} // namespace

RoutineRef specialArmHandlerOf(uint32_t weapon) {
    return weapon < specialArmHandlers.size() ? specialArmHandlers[weapon] : RoutineRef();
}

bool specialArmPorted(uint32_t weapon) {
    if (weapon >= specialArmHandlers.size()) return false;
    const RoutineRef& arm = specialArmHandlers[weapon];
    switch (arm.id()) {
    case RoutineId::PlayerGenericSpecialArm:
    case RoutineId::PlayerCannonArm:
    case RoutineId::PlayerExtinguisherArm:
    case RoutineId::PlayerBladeArm: return true;
    default: return arm.null() && weapon == kNoArmWeapon;
    }
}

uint8_t weaponShotType(uint32_t weapon) { return weaponTable[weapon].shotType; }

int8_t weaponLevel(const Player& p, int weapon, WeaponStat stat) {

    return p.weaponLevels[size_t(uint32_t(weapon) * 8 + uint32_t(stat))];
}

void paySpecialEnergy(Player& p) {
    p.gauge = wrap16(p.gauge - p.shotCost);
    if (p.gauge < p.shotCost) p.armFlags &= kArmSpecialSpentMask;
}

int weaponStat(const Player& p, int weapon, WeaponStat stat) {
    if (stat == WeaponStat::Extra) throw ResolutionError("weaponStat: WeaponStat::Extra has no stat table entry");
    if (weapon == 0) return 0;
    const int k = int(stat);
    const int level = weaponLevel(p, weapon, stat);
    const size_t w = size_t(uint32_t(weapon)), l = size_t(uint32_t(level));
    // a weapon without a stat table: the exe reads through a null pointer (an unmapped read, as the port reported it)
    if (!weaponStatTables->has(w)) throw std::out_of_range("weaponStat: weapon " + std::to_string(weapon) + " has no stat table");
    if (l >= WeaponStatTable::kLevels) tableIndexError(l, WeaponStatTable::kLevels);
    return weaponStatTables[w].levels[l][size_t(k)];
}

int weaponAttack(const Player& p, int weapon) {
    if (weapon == 0) return 0;
    int v = weaponStat(p, weapon, WeaponStat::Attack);
    if (gameState().difficulty == kHardestDifficulty) v >>= 1;
    return v;
}

void weaponMode(int8_t mode) {
    Player& p = player();
    p.weaponMode = mode;
    if (mode < 0) return;
    const bool allowed = p.variant == 0 && p.specialAllowed != 0; // the special weapon may be used here
    if (mode == kWeaponModeField) {
        p.activeSpecial = allowed ? int8_t(p.specialWeapon) : int8_t(0);
        p.activeBuster = p.busterWeapon;
        return;
    }
    if (mode == kWeaponModeSpecial) {
        p.specialWeapon = kExtinguisherWeapon;
        if (allowed) {
            p.activeSpecial = kExtinguisherWeapon;
            p.activeBuster = kTownBuster;
            p.specialWeapon = kExtinguisherWeapon;
            return;
        }
    } else if (mode != kWeaponModeTown) {
        return;
    }
    p.activeBuster = kTownBuster;
    p.activeSpecial = 0;
}

void weaponReset() {
    putArmDown(player());
    enableFiring(player());
}

int weaponBusy(const Player& p) {
    if (p.armFlags & kArmAnyRequest) return 1;
    return (p.carryFlags & (kCarryHolding | kCarryThrowing)) ? -1 : 0;
}

void clearWeaponState(Player& p) {
    p.armFlags = 0;
    p.carryFlags = 0;
    p.lockRequest = 0;
    p.lockRequestTicks = 0;
    p.specialFiring = 0;
    p.charge = 0;
    p.suction = 0;
    p.clearLockOnTarget();
    p.homingTarget = 0;
    p.lockedActor = 0;
    p.busterShotsInFlight = 0;
    p.interactionTarget.clear();
}

void putArmDown(Player& p) {
    p.armFlags &= uint16_t(kArmSpecialQueued | kArmBusterQueued);
    if (p.lockSuspended != 0) {
        p.lockRequest = 0;
        p.lockRequestTicks = 0;
    }
    if ((p.carryFlags & kCarryHolding) == 0) {
        p.upper.anim = p.body.anim;
        p.upper.key.frame = p.body.key.frame;
    }
}

void enableFiring(Player& p) {
    p.setArmBits(kArmReady);
    if (p.busterShotsInFlight < weaponStat(p, p.activeBuster, WeaponStat::Energy)) p.setArmBits(kArmBusterFree);
    if (p.shotCost <= p.gauge) p.setArmBits(kArmSpecialEnergy);
}

PlayerAnim busterArmAnim(const Player& p) {
    if (p.activeBuster == kTownBuster) return PlayerAnim::Kick;
    const PlayerSub s = p.sub();
    return (s == PlayerSub::Run || s == PlayerSub::Jump || s == PlayerSub::RunningJump) ? PlayerAnim::BusterArmMoving
                                                                                        : PlayerAnim::BusterArm;
}

PlayerAnim specialArmAnim(const Player& p) {
    if (p.activeSpecial == kTownBuster) return PlayerAnim::SpecialTownArm;
    const PlayerSub s = p.sub();
    return (s == PlayerSub::Run || s == PlayerSub::Jump || s == PlayerSub::RunningJump)
               ? PlayerAnim::SpecialArmMoving
               : PlayerAnim::SpecialArm;
}

int fireButtons(Player& p, FireContext ctx) {
    const uint16_t held = p.held;
    const uint16_t fire = p.button(PlayerButton::Fire), special = p.button(PlayerButton::Special);
    if (p.carryFlags != 0 || p.charge != 0) return 0;
    if (weaponBusy(p) == 0 && (special & held) && p.activeSpecial == 0) return 0;
    const int16_t specialUsable = int16_t(weaponRecord(p.activeSpecial).usableMoving);
    const int16_t busterUsable = int16_t(weaponRecord(p.activeBuster).usableMoving);
    uint16_t f = p.armFlags;
    if ((f & kArmUp) == 0) {
        if (p.activeSpecial == kChargeWeapon && !(f & (kArmBusterRefire | kArmSpecialRefire)) &&
            (f & kArmSpecialReady) == kArmSpecialReady) {
            p.armFlags = uint16_t(f & ~kArmSpecialQueued);
            if (special & held) return 0;
        }
        f = p.armFlags;
        bool raised = (f & kArmBusterRefire) && ((fire & held) || (f & kArmBusterQueued)) &&
                      (f & kArmBusterReady) == kArmBusterReady &&
                      tryRaiseArm(p, kRaiseBusterLegsFree, ctx, busterUsable) != 0;
        if (!raised) {
            f = p.armFlags;
            raised = (f & kArmSpecialRefire) && ((special & held) || (f & kArmSpecialQueued)) &&
                     (f & kArmSpecialReady) == kArmSpecialReady &&
                     tryRaiseArm(p, kRaiseSpecialLegsFree, ctx, specialUsable) != 0;
        }
        if (raised) { // a refire: the arm's handler restarts at its step 1
            p.armFlags &= uint16_t(~(kArmBusterRefire | kArmSpecialRefire | kArmBusterQueued | kArmSpecialQueued));
            p.resetArmStep(1);
            return 1;
        }
        f = p.armFlags;
        if (((fire & held) || (f & kArmBusterQueued)) && (f & kArmBusterReady) == kArmBusterReady) {
            p.armFlags = uint16_t(f & ~(kArmBusterQueued | kArmSpecialQueued));
            return tryRaiseArm(p, kRaiseWithAnim + kRaiseBusterLegsFree, ctx, busterUsable);
        }
        if (((special & held) || (f & kArmSpecialQueued)) && (f & kArmSpecialReady) == kArmSpecialReady) {
            p.armFlags = uint16_t(f & ~(kArmBusterQueued | kArmSpecialQueued));
            return tryRaiseArm(p, kRaiseWithAnim + kRaiseSpecialLegsFree, ctx, specialUsable);
        }
    }
    p.armFlags = uint16_t(f & ~(kArmBusterQueued | kArmSpecialQueued));
    return 0;
}

bool specialEnergyReady(const Player& p) { return p.activeSpecial != 0 && p.shotCost <= p.gauge; }

namespace {
// exe 0x57b110 / 0x57b170's shared body: the throw holds; else the up arm's weapon allows it unless its byte is 0
template <class Allows> bool armHolds(const Player& p, Allows allows) {
    if (p.carryFlags & kCarryThrowing) return true;
    const uint16_t arm = p.armFlags & kArmUp;
    int8_t c;
    if (arm == kArmSpecial) {
        if (p.activeSpecial == 0) return false;
        c = allows(weaponRecord(p.activeSpecial));
    } else {
        if (arm != kArmBuster) return false;
        c = allows(weaponRecord(p.activeBuster));
    }
    return c == 0;
}
} // namespace

bool armHoldsMoves(const Player& p) {
    return armHolds(p, [](const WeaponRecord& r) { return int8_t(r.allowsMoves); });
}
bool armHoldsTurns(const Player& p) {
    return armHolds(p, [](const WeaponRecord& r) { return int8_t(r.allowsTurns); });
}

bool armPastFireFrame(const Player& p) {
    const uint16_t arm = p.armFlags & kArmUp;
    if (arm != 0) {
        if (arm == kArmSpecial) {
            if (p.activeSpecial == 0) return false;
            if (int(upperFrame(p)) <= int(weaponRecord(p.activeSpecial).fireFrame)) return false;
        } else {
            if (arm != kArmBuster) return false;
            if (int(upperFrame(p)) <= int(weaponRecord(p.activeBuster).fireFrame)) return false;
        }
    }
    return true;
}

bool mustStopToFire(const Player& p) {
    const uint16_t held = p.held;
    const bool special = !(p.button(PlayerButton::Special) & held) || p.activeSpecial == 0 || p.gauge < p.shotCost ||
                         weaponRecord(p.activeSpecial).usableMoving != 0;
    const bool buster = !(p.button(PlayerButton::Fire) & held) || weaponRecord(p.activeBuster).usableMoving != 0;
    return !(special && buster);
}

void busterArmUpdate(Player& p) {
    const RoutineRef& handler = busterArmHandlers[uint32_t(int32_t(p.activeBuster))];
    if (!handler.is(RoutineId::PlayerBusterArm)) {
        char msg[64];
        std::snprintf(msg, sizeof msg, "exe 0x5729e0 arm handler 0x%06x", handler.pointerValue());
        notPorted(kPlayerArea, msg);
    }
    busterArm(p);
    if (!(p.armFlags & kArmBusterShot)) return;
    const uint32_t a = allocActor10();
    if (a == 0) return;
    ShotActor& shot = shotActorAt(a);
    shot.flags = kActorLive | kActorDrawn;
    shot.type = weaponRecord(p.activeBuster).shotType;
    shot.loopFlags |= kShotSweepGroup;
    aimShot(p, shot);
}

void specialArmUpdate(Player& p) {
    const RoutineRef& handler = specialArmHandlers[uint32_t(int32_t(p.activeSpecial))];
    switch (handler.id()) {
    case RoutineId::PlayerExtinguisherArm: extinguisherArm(p); break;
    case RoutineId::PlayerBladeArm: bladeArm(p); break;
    case RoutineId::PlayerCannonArm: cannonArm(p); break;
    case RoutineId::PlayerGenericSpecialArm: genericSpecialArm(p); break;
    default: {
        char msg[64];
        std::snprintf(msg, sizeof msg, "exe 0x572910 arm handler 0x%06x", handler.pointerValue());
        notPorted(kPlayerArea, msg);
    }
    }
    if (!(p.armFlags & kArmSpecialShot)) return;
    const uint32_t a = allocActor10();
    if (a == 0) return;
    ShotActor& shot = shotActorAt(a);
    shot.flags = kActorLive;
    shot.type = weaponRecord(p.activeSpecial).shotType;
    shot.loopFlags |= kShotSweepGroup;
    aimShot(p, shot);
    for (size_t i = 0; i < shot.params.size(); ++i) shot.params[i] = 0;
}

void setupSpecialGauge(bool refill) {
    Player& p = player();
    if (refill) {
        const int16_t energy = int16_t(weaponStat(p, p.specialWeapon, WeaponStat::Energy));
        p.energyPool = energy;
        p.energyReserve = energy;
    }
    const GaugeRecord g = gaugeTable[size_t(uint32_t(int32_t(p.specialWeapon)))];
    p.gaugeRate = g.rate;
    p.shotCost = g.cost;
    p.gaugeMax = g.max;
    p.gauge = g.max;
}

void rechargeSpecialGauge(Player& p) {
    if (p.activeSpecial != 0 && (p.status & (kStatusOrbit | kStatusOrbitFloor)) && p.energyReserve < kInfiniteEnergy) {
        const int16_t rate = p.gaugeRate, pool = p.energyPool; // the orbit status drains twice the rate
        if (pool == 0) {
            if (rate * 2 < int(p.gauge))
                p.gauge = wrap16(p.gauge + rate * -2);
            else
                p.gauge = 0;
        } else if (rate * 2 < int(pool)) {
            p.energyPool = wrap16(pool + rate * -2);
        } else {
            p.energyPool = 0;
        }
    }
    const uint16_t f = p.armFlags;
    if ((f & kArmSpecial) || p.charge != 0) return;
    if (p.energyReserve == 0) { // self-recharging
        p.gauge = wrap16(p.gauge + p.gaugeRate);
        if (p.shotCost <= p.gauge) p.armFlags = uint16_t(f | kArmSpecialEnergy);
    } else if (p.activeSpecial != 0) {
        int fill = int(p.gaugeMax) - int(p.gauge);
        if (fill != 0) {
            if (p.gaugeRate < fill) fill = p.gaugeRate;
            const int16_t pool = p.energyPool;
            if (pool < fill) fill = pool;
            if (p.energyReserve < kInfiniteEnergy) p.energyPool = wrap16(pool - wrap16(fill));
            p.gauge = wrap16(p.gauge + wrap16(fill));
            if (p.shotCost <= p.gauge) p.armFlags = uint16_t(f | kArmSpecialEnergy);
        }
    }
}

void updateCharge(Player& p, uint16_t held) {
    if ((p.armFlags & kArmAnyRequest) == 0 && specialEnergyReady(p) && p.activeSpecial == kChargeWeapon &&
        (p.button(PlayerButton::Special) & held) && p.hitPoints >= 0 && p.sub() != PlayerSub::KnockDown) {
        chargeStartMarker(p);
    } else {
        if ((p.button(PlayerButton::Special) & p.released) && (p.charge & kChargeLevelMask) > 1) chargeReleaseMarker(p);
        p.charge = 0;
    }
}

namespace {

// the shield's ring marker (pool 0xa0 global type 6; its class is not transcribed): its spawn parameters, the pitch /
// yaw it faces
struct ShieldRingParams {
    explicit ShieldRingParams(const EffectActor& actor) : ShieldRingParams(const_cast<EffectActor&>(actor)) {}
    explicit ShieldRingParams(EffectActor& actor) : pitch(actor.params, 0), yaw(actor.params, 2) {}
    BytesField<int16_t, 4> pitch;
    BytesField<int16_t, 4> yaw;
};
constexpr uint32_t kShieldReach = 0xa0;    // added to the shot's radius
constexpr uint8_t kShieldBlocksMask = 0xf; // the blocks counted (at most 0xf)
constexpr uint8_t kShieldBlocked = 0x10;   // a block happened

} // namespace

bool playerShieldBlocks(const Actor& shot, uint32_t radius) {
    Player& p = player();
    if (p.charge == 0) return false;
    if (uint32_t(distance3(shot.pos, p.pos)) > radius + kShieldReach) return false; // exe 0x5e3a10(shot, player)
    uint32_t blocks = p.charge & kShieldBlocksMask;
    if (blocks <= 0xe) ++blocks;
    p.charge = uint8_t(blocks | kShieldBlocked);
    if (const uint32_t address = allocActorA0(0)) { // exe 0x5e13b0
        EffectActor& ring = effectActorAt(address);
        ring.flags = kActorLive | kActorDrawn;
        ring.type = typeByte(GlobalEffectType::ShieldRing);
        ring.loopFlags |= kActorLateLoop;
        const int16_t from[3] = {shot.prevPos.x.whole, shot.prevPos.y.whole, shot.prevPos.z.whole};
        const ShortVec3& body = p.bonePoints[0];
        const int16_t to[3] = {body.x, body.y, body.z};
        auto&& params = ShieldRingParams(ring);
        params.pitch = wrap16(-pitchOf(from, to)); // exe 0x5e3d10
        params.yaw = int16_t(angleOf(from, to));   // exe 0x5e3c00
    }
    return true;
}

} // namespace od::port
