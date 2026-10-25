// ST0E: the vehicle the player defends (see st0e_defended_vehicle.hpp).
//
// The vehicle (exe 0x5b5e70; ST0E type 0x4e, the room records 0x8c6568 / 0x8c6590, the room records' actor 1). States
// (the exe's table 0x8d09d4): 0 bind 0x5b5ec0, 1 run 0x5b6000, 2 free 0x5afdd0.
//   Bind: the model of its variant, its HP the row's << 4 (or, with flag 0x5a1 -- the second room -- the HP the first
//   room carried in the stage block), a target gauge, the timer 3; flag 0x5a1 -> Drive, else Wait and the enemy-group
//   index of the turret (looked up with the vehicle posing as the turret: class 0x20, type 0x7b).
//   Run: HP < 0 -> the flags 0x584 / 0x6f0, Destroyed, voice 0x28c (once); below 1/3 HP voice 0x28b once; the sub-states
//   (the exe's table 0x8d09e0); a stand-on solid.
//   It keeps the wanted gap behind the boss vehicle along x (the boss's x minus its own), its speed along its yaw
//   (-0x200 .. -0x300), the round, a timer (the byte tables 0x8d0a0c / 18 / 24 / 30 / 38).
//   Sub-states: Wait (exe 0x5b60b0) the timer; NextRound (exe 0x5b60d0) the turret's marks clear -> the next round
//   (1..3: the gap 0x1900 - round * 0x400, voice clip 0x803a on the first; 4: the gap 0x800, a random timer) ->
//   CloseIn; CloseIn (exe 0x5b61d0) close the gap, then signal the turret -> NextRound (Linger in round 4); Linger /
//   Front (exe 0x5b62b0, flag 0x582 off / on) the timer, Linger -> HoldBack (the gap 0xc60), Front -> CloseIn (the gap
//   0x800); HoldBack (exe 0x5b6390) fall back, signal -> Front; Drive (exe 0x5b6490, flag 0x5a1) until flag 0x582 ->
//   SecondCloseIn (the gap 0x400); SecondCloseIn (exe 0x5b64e0) close in, flag 0x582 cleared once, flag 0x586 ->
//   SwapLeft; SwapLeft / SwapRight (exe 0x5b65d0 / 0x5b6710) swap sides (the gap +-0x400, flag 0x586 set / cleared) on
//   random timers until flag 0x587; Destroyed (exe 0x5b6820) sparks every other tick, a fire (static stage 3 variant
//   0x1a) every 64 ticks, slows to a stop.
#include "content/yosyonke/st0e_defended_vehicle.hpp"
#include "content/yosyonke/st0e_defended_vehicle_state.hpp"

#include "content/yosyonke/st0e_vehicles.hpp"
#include "engine/data_source.hpp"
#include "game/actor_collision.hpp"
#include "game/actor_model.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actor_stats.hpp"
#include "game/audio_logic.hpp"
#include "game/encounter.hpp"
#include "game/game_state.hpp"
#include "game/global_class_types.hpp"
#include "game/hud.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/story_flags.hpp"

namespace od::port {

using namespace st0e_defended_vehicle_state;

namespace {

enum class DefendedState : uint8_t { Bind = 0, Run = 1, Free = 2 };
enum class DefendedSub : uint8_t {
    Wait = 0,
    NextRound = 1,
    CloseIn = 2,
    Linger = 3,
    HoldBack = 4,
    Front = 5,
    Drive = 6,
    SecondCloseIn = 7,
    SwapLeft = 8,
    SwapRight = 9,
    Destroyed = 10,
};
constexpr uint8_t kVoiceLowHpSaid = 0x01;
constexpr uint32_t kFlagFrontBroken = 0x582, kFlagDefendedDown = 0x584, kFlagSidesSwapped = 0x586;
constexpr uint32_t kFlagBossDown = 0x587, kFlagDefendedDownRecord = 0x6f0, kFlagSecondRoom = 0x5a1;
constexpr uint8_t kTurretType = 0x7b, kTurretSlotClass = 0x20;
constexpr uint8_t kFireVariant = 0x1a;
// the timers' tables (u16 entries; the exe reads their low bytes)
struct TimerEntry : DataRecord {
    uint8_t ticks = 0;
    static TimerEntry load(const TableReader& r) {
        TimerEntry rec;
        rec.ticks = r.u8(0);
        return rec;
    }
};
const DataTable<RecordTable<TimerEntry, 5, 2>> lastRoundTimers{TableId::St0eDefendedVehicleLastRoundTimers};
const DataTable<RecordTable<TimerEntry, 5, 2>> frontTimers{TableId::St0eDefendedVehicleFrontTimers};
const DataTable<RecordTable<TimerEntry, 5, 2>> holdBackTimers{TableId::St0eDefendedVehicleHoldBackTimers};
const DataTable<RecordTable<TimerEntry, 4, 2>> swapLeftTimers{TableId::St0eDefendedVehicleSwapLeftTimers};
const DataTable<RecordTable<TimerEntry, 4, 2>> swapRightTimers{TableId::St0eDefendedVehicleSwapRightTimers};

// the gap to the boss vehicle along x: its x minus ours
int gapToBoss(const StaticActor& a) { return int(bossVehicle().pos.x.whole) - int(a.pos.x.whole); }
void drive(StaticActor& a) { integrateVelocity(a, 0, 0, a.velocity.z); }
// the speed down by the braking (the speed's acceleration lane) to lo (the bound lo + 1 kept), then the move
void speedUpTo(StaticActor& a, int16_t lo) {
    if (a.velocity.z > lo) a.velocity.z = int16_t(a.velocity.z - a.acceleration.z);
    else a.velocity.z = lo;
    drive(a);
}
// the random pick of 0x5b60d0 / 0x5b6390: rng byte < 0x33 / 0x66 / 0x99 / 0xcc / else -> 0..4
uint32_t pickOfFive(uint32_t r) {
    const uint8_t b = uint8_t(r);
    if (b < 0x33) return 0;
    if (b < 0x66) return 1;
    if (b < 0x99) return 2;
    return b < 0xcc ? 3 : 4;
}
void waitSub(StaticActor& a, DefendedFields& f) { // exe 0x5b60b0
    if (f.timer != 0) {
        --f.timer;
        return;
    }
    ++a.subState;
}
void nextRoundSub(StaticActor& a, DefendedFields& f) {                                // exe 0x5b60d0
    if (enemyGroupMarks(int32_t(f.turretGroup), EnemyGroupMarks::Either) == 0) { // exe 0x5e6d60(index, 0)
        if (f.round == 3) {
            f.round = 4;
            a.subState = uint8_t(DefendedSub::CloseIn);
            f.wantedGap = 0x800;
            f.timer = lastRoundTimers[pickOfFive(rngStep())].ticks;
        } else {
            const uint8_t n = uint8_t(f.round + 1);
            a.subState = uint8_t(DefendedSub::CloseIn);
            f.round = n;
            f.wantedGap = int16_t(0x1900 - (int32_t(int8_t(n)) << 10));
            if (n == 1) voiceClipPlay(0x803a); // exe 0x601050
        }
    }
    speedUpTo(a, -0x280);
}
void closeInSub(StaticActor& a, DefendedFields& f) { // exe 0x5b61d0
    const AnimatedActor& boss = bossVehicle();
    clearStoryFlag(kFlagFrontBroken);
    const int d = int(boss.pos.x.whole) - int(a.pos.x.whole);
    if (d > f.wantedGap) {
        speedUpTo(a, -0x300);
        return;
    }
    if (a.velocity.z < -0x280) {
        a.velocity.z = int16_t(a.velocity.z + a.acceleration.z);
        drive(a);
        return;
    }
    a.velocity.z = -0x280;
    enemyGroupMark(int32_t(f.turretGroup), 1);
    a.subState = uint8_t(f.round == 4 ? DefendedSub::Linger : DefendedSub::NextRound);
    drive(a);
}
void lingerSub(StaticActor& a, DefendedFields& f) { // exe 0x5b62b0 (Linger and Front)
    const bool front = DefendedSub(a.subState) == DefendedSub::Front;
    if (front) setStoryFlag(kFlagFrontBroken);
    else clearStoryFlag(kFlagFrontBroken);
    if (f.timer != 0) {
        --f.timer;
    } else if (!front) {
        a.subState = uint8_t(DefendedSub::HoldBack);
        f.wantedGap = 0xc60;
    } else {
        f.wantedGap = 0x800;
        a.subState = uint8_t(DefendedSub::CloseIn);
        const uint32_t k = pickOfFive(rngStep());
        if (k != 0) f.timer = frontTimers[k].ticks; // the exe skips the store below 0x33
    }
    speedUpTo(a, -0x280);
}
void holdBackSub(StaticActor& a, DefendedFields& f) { // exe 0x5b6390
    const AnimatedActor& boss = bossVehicle();
    clearStoryFlag(kFlagFrontBroken);
    const int d = int(boss.pos.x.whole) - int(a.pos.x.whole);
    if (d <= f.wantedGap) {
        if (a.velocity.z < -0x200) a.velocity.z = int16_t(a.acceleration.z + a.velocity.z);
        else a.velocity.z = -0x200;
    } else if (a.velocity.z > -0x280) {
        a.velocity.z = int16_t(a.velocity.z - a.acceleration.z);
    } else {
        a.velocity.z = -0x280;
        enemyGroupMark(int32_t(f.turretGroup), 1);
        a.subState = uint8_t(DefendedSub::Front);
        f.timer = holdBackTimers[pickOfFive(rngStep())].ticks;
    }
    drive(a);
}
void driveSub(StaticActor& a, DefendedFields& f) { // exe 0x5b6490
    if (a.velocity.z > -0x280) a.velocity.z = int16_t(a.velocity.z - a.acceleration.z);
    else a.velocity.z = -0x280;
    drive(a);
    if (!storyFlag(kFlagFrontBroken)) return;
    f.wantedGap = 0x400;
    ++a.subState;
}
void secondCloseInSub(StaticActor& a, DefendedFields& f) { // exe 0x5b64e0
    if (gapToBoss(a) > f.wantedGap) {
        speedUpTo(a, -0x300);
        return;
    }
    if (a.velocity.z < -0x280) {
        a.velocity.z = int16_t(a.acceleration.z + a.velocity.z);
        drive(a);
        return;
    }
    a.velocity.z = -0x280;
    if (a.step == 0) { // flag 0x582 cleared once
        clearStoryFlag(kFlagFrontBroken);
        ++a.step;
    }
    if (storyFlag(kFlagSidesSwapped)) {
        a.step = 0;
        ++a.subState;
        f.timer = 0x3c;
        f.wantedGap = -0x400;
    }
    drive(a);
}
void swapLeftSub(StaticActor& a, DefendedFields& f) { // exe 0x5b65d0
    if (gapToBoss(a) > f.wantedGap) {
        speedUpTo(a, -0x300);
        return;
    }
    if (a.velocity.z < -0x280) {
        a.velocity.z = int16_t(a.acceleration.z + a.velocity.z);
        drive(a);
        return;
    }
    a.velocity.z = -0x280;
    if (f.timer != 0) {
        --f.timer;
    } else if (!storyFlag(kFlagBossDown)) {
        if (uint16_t(f.wantedGap) == 0x400) {
            f.wantedGap = -0x400;
            clearStoryFlag(kFlagSidesSwapped);
        } else {
            f.wantedGap = 0x400;
            setStoryFlag(kFlagSidesSwapped);
            a.subState = uint8_t(DefendedSub::SwapRight);
        }
        f.timer = swapLeftTimers[rngStep() & 3].ticks;
    }
    drive(a);
}
void swapRightSub(StaticActor& a, DefendedFields& f) { // exe 0x5b6710
    if (gapToBoss(a) <= f.wantedGap) {
        if (a.velocity.z < -0x200) a.velocity.z = int16_t(a.acceleration.z + a.velocity.z);
        else a.velocity.z = -0x200;
        drive(a);
        return;
    }
    if (a.velocity.z > -0x280) {
        a.velocity.z = int16_t(a.velocity.z - a.acceleration.z);
        drive(a);
        return;
    }
    a.velocity.z = -0x280;
    if (f.timer != 0) {
        --f.timer;
    } else {
        if (uint16_t(f.wantedGap) == 0x400) {
            f.wantedGap = -0x400;
            clearStoryFlag(kFlagSidesSwapped);
            a.subState = uint8_t(DefendedSub::SwapLeft);
        } else {
            f.wantedGap = 0x400;
            setStoryFlag(kFlagSidesSwapped);
        }
        f.timer = swapRightTimers[rngStep() & 3].ticks;
    }
    drive(a);
}
// a coordinate of the wreck's sparks: ((r & 0x1ff) * 0x180 - 0x18000) >> 8 around x / z, -(r & 0x1ff) * 3 / 2 above
uint16_t sparkSpread(uint32_t r) { return uint16_t(((r * 0x180u - 0x18000u) >> 8)); }
uint16_t sparkRise(uint32_t r) { return uint16_t(((0u - r * 3u) << 7) >> 8); }
void destroyedSub(StaticActor& a) { // exe 0x5b6820
    if (gameState().frame & 1) {
        if (const uint32_t at = allocActorA0(0)) { // exe 0x5e13b0: a hit spark (effect global 0)
            EffectActor& e = effectActorAt(at);
            e.flags = kActorLive;
            e.type = typeByte(GlobalEffectType::HitSpark);
            e.subtype = 0;
            e.variant = 0;
            e.runFlags |= kActorWarpKept;
            e.pos.x = int16_t(uint16_t(sparkSpread(rngStep() & 0x1ff) + uint16_t(a.pos.x.whole)));
            e.pos.y = int16_t(uint16_t(sparkRise(rngStep() & 0x1ff) + uint16_t(a.pos.y.whole)));
            e.pos.z = int16_t(uint16_t(sparkSpread(rngStep() & 0x1ff) + uint16_t(a.pos.z.whole)));
            e.params[0] = uint8_t((rngStep() & 0x7f) + 0x40);
        }
    }
    if (!(gameState().frame & 0x3f)) {
        if (const uint32_t at = allocActor60(kActorStageClass)) { // exe 0x5e12f0: a fire (static stage 3 variant 0x1a)
            StaticActor& e = staticActorAt(at);
            FireEmitterParams p(e);
            e.flags = kActorLive;
            e.type = kFireEmitterType;
            e.subtype = 0;
            e.variant = kFireVariant;
            e.spawnByte = 0;
            e.pos.x = int32_t(((rngStep() & 0x7f) - 0x40) * 0x10000u + uint32_t(a.pos.x));
            e.pos.y = int32_t(uint32_t(a.pos.y) - ((rngStep() & 0x1f) << 16) - 0x1000000u);
            const uint32_t r = rngStep();
            p.paramA = 0;
            p.paramB = 0;
            p.lifetime = 0x1e;
            e.pos.z = int32_t(((r & 0x7f) - 0x40) * 0x10000u + uint32_t(a.pos.z));
        }
    }
    if (a.velocity.z < 0) {
        a.velocity.z = int16_t(a.velocity.z + 4);
        drive(a);
        return;
    }
    a.velocity.z = 0;
    integrateVelocity(a, 0, 0, 0);
}
void run(StaticActor& a, DefendedFields& f) { // exe 0x5b6000
    if (a.hitPoints < 0 && !storyFlag(kFlagDefendedDown)) {
        setStoryFlag(kFlagDefendedDown);
        setStoryFlag(kFlagDefendedDownRecord);
        a.subState = uint8_t(DefendedSub::Destroyed);
        vehicleVoice(0x28c);
    }
    if (!(f.voicesSaid & kVoiceLowHpSaid) && int(a.hitPoints) < int(a.maxHitPoints) / 3) {
        vehicleVoice(0x28b);
        f.voicesSaid |= kVoiceLowHpSaid;
    }
    switch (DefendedSub(a.subState)) { // the exe's table 0x8d09e0
    case DefendedSub::Wait: waitSub(a, f); break;
    case DefendedSub::NextRound: nextRoundSub(a, f); break;
    case DefendedSub::CloseIn: closeInSub(a, f); break;
    case DefendedSub::Linger:
    case DefendedSub::Front: lingerSub(a, f); break;
    case DefendedSub::HoldBack: holdBackSub(a, f); break;
    case DefendedSub::Drive: driveSub(a, f); break;
    case DefendedSub::SecondCloseIn: secondCloseInSub(a, f); break;
    case DefendedSub::SwapLeft: swapLeftSub(a, f); break;
    case DefendedSub::SwapRight: swapRightSub(a, f); break;
    case DefendedSub::Destroyed: destroyedSub(a); break;
    default: notPortedAt(kSt0ePort, "0x5b6000 sub-state", a.subState);
    }
    actorCollisionSubmitSolid(a, SolidType::StandOn, kNoBone, 0); // exe 0x414050
}
void bind(StaticActor& a, DefendedFields& f) { // exe 0x5b5ec0
    if (!bindActorModel(a, a.variant)) {        // exe 0x5e0bc0(a, &variant)
        freeActor(a);                           // exe 0x5e1540
        return;
    }
    a.flags |= uint8_t(kActorDrawn | kActorScenery);
    a.runFlags |= kActorWarpKept;
    a.maxHitPoints = int16_t(hasStaticStats(a.type) ? uint32_t(staticStats(a.type, 0).hitPoints) << 4 : 0u);
    a.hitPoints = int16_t(storyFlag(kFlagSecondRoom) ? uint16_t(stageBlock().part<VehicleChaseBlock>().carriedHp) : uint16_t(a.maxHitPoints));
    hudAddTargetGauge(a, 0, 2, 0x2222aa); // exe 0x415e50
    a.velocity.setXY(0, 0);
    a.collisionBox = nullptr;
    a.interactionBox = nullptr;
    a.velocity.setZSpare(0, 0);
    a.rotation.z = 0;
    a.rotation.x = 0;
    a.velocity.y = 0;
    a.acceleration.y = 0x40;
    a.acceleration.z = 0x10;
    a.airborne = 0;
    a.onPlatform = 0;
    a.platform = nullptr;
    a.actorHit = 0;
    f.timer = 3;
    f.voicesSaid = 0;
    a.state = uint8_t(DefendedState::Run);
    if (storyFlag(kFlagSecondRoom)) {
        a.subState = uint8_t(DefendedSub::Drive);
        a.step = 0;
        run(a, f);
        return;
    }
    a.subState = uint8_t(DefendedSub::Wait);
    a.step = 0;
    // exe 0x5e6cd0(a, 0) with the vehicle posing as the turret (class 0x20, type 0x7b): the turret's group index
    const uint8_t slot = a.slot;
    a.type = kTurretType;
    a.slot = kTurretSlotClass;
    f.turretGroup = uint8_t(enemyGroupFindFor(a, 0));
    a.type = 0x4e;
    a.slot = slot;
    run(a, f);
}
void defendedVehicle(StaticActor& a) { // exe 0x5b5e70
    DefendedFields& f = classState<DefendedFields>(a);
    a.tint = kTintNeutral;
    a.savePreviousPose();
    a.savePreviousContact();
    switch (DefendedState(a.state)) { // the exe's table 0x8d09d4
    case DefendedState::Bind: bind(a, f); return;
    case DefendedState::Run: run(a, f); return;
    case DefendedState::Free: freeActor(a); return; // exe 0x5afdd0 -> 0x5e1540
    default: notPortedAt(kSt0ePort, "0x5b5e70 state", a.state);
    }
}

} // namespace

void defendedVehicleUpdate(const PlayerEnv&, uint32_t actor) { defendedVehicle(staticActorAt(actor)); }

} // namespace od::port
