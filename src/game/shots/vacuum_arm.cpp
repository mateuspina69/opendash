// The vacuum arm (weapon 8, pool 0x10 type 7, exe 0x56e630): the suction effect (game/suction_effect) at the hand;
// while the special weapon is armed and its button held (the energy paid each tick below the energy level 3) the
// player's suction byte is set (the effect grows, the pickups are pulled), else the shot is freed.
#include "game/actor_state.hpp"
#include "game/shots/player_shots.hpp"

#include "game/actor_pools.hpp"
#include "game/fixed_point.hpp"
#include "game/global_class_types.hpp"
#include "game/player/player_state.hpp"
#include "game/player/player_weapons.hpp"
#include "game/port_gap.hpp"
#include "game/shots/shot_common.hpp"
#include "game/suction_effect.hpp"

namespace od::port {

namespace {

inline constexpr const char* kVacuumArea = "vacuum arm";

// [0x8c49f0]
enum class VacuumState : uint8_t { Spawn = 0, Hold = 1 }; // exe 0x56e660 / 0x56e6b0
constexpr uint8_t kSuctionHeld = 3; // the player's suction byte while held (the effect grows, the pickups follow)

void spawn(ShotActor& shot) { // exe 0x56e660: the suction effect (retried while the pool is full)
    shot.flags &= uint8_t(~kActorDrawn);
    shot.runFlags |= kActorWarpKept;
    const uint32_t address = allocActorA0(0);
    if (address == 0) return;
    EffectActor& e = effectActorAt(address);
    e.flags = kActorLive;
    e.loopFlags |= kActorSweepGroup1 | kActorLateLoop;
    e.type = typeByte(GlobalEffectType::Suction);
    e.variant = 0;
    auto&& suction = classState<SuctionFields>(e);
    suction.rangeLevel = weaponLevel(player(), player().activeSpecial, WeaponStat::Range);
    suction.vacuumShot = &shot;
    ++shot.state;
}

void hold(ShotActor& shot) { // exe 0x56e6b0
    Player& p = player();
    if (!(p.armFlags & kArmSpecial) || !p.holds(PlayerButton::Special)) {
        freeActor(shot);
        return;
    }
    if (weaponLevel(p, p.activeSpecial, WeaponStat::Energy) < kFreeEnergyLevel) {
        p.gauge = wrap16(p.gauge - p.shotCost);
        if (p.gauge < p.shotCost) {
            p.armFlags &= kArmSpecialSpentMask;
            freeActor(shot);
            return;
        }
    }
    p.suction = kSuctionHeld;
}

} // namespace

void vacuumArmShotUpdate(const PlayerEnv&, uint32_t address) {
    ShotActor& shot = shotActorAt(address);
    dropDeadTarget(shot);
    switch (VacuumState(shot.state)) {
    case VacuumState::Spawn: spawn(shot); return;
    case VacuumState::Hold: hold(shot); return;
    default: notPortedAt(kVacuumArea, "0x56e630 state", shot.state);
    }
}

} // namespace od::port
