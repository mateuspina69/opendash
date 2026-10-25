// ST0E's boss vehicle and its fireball (see st0e_boss_vehicle.hpp).
//
// The vehicle (exe 0x501d70; ST0E subs 2 / 5 records). States (the exe's table 0x897ca0): 0 bind 0x502510, 1 run
// 0x5026f0 (the sub-states 0x897cac), 2 free 0x468fa0. It keeps its block in a bone block of its own (block 3, 0x70
// bytes): ten hit boxes -- 0 / 1 the two front weak points (their owners: the two e0 watchers), 2 the charge point on
// bone 1 (its own HP), 3 the cab, 4..9 the side panels -- the charging fireball (static 0x5c), two fire emitters, the
// fireball's timers and the burning sequence of flag 0x5a1.
//   Sub-states: 0 exe 0x502730 the turret spawned 0x6900 ahead, 1 exe 0x5027e0 the drive (the turret kept 0x6900
//   ahead), 2 exe 0x502870 the voices, a carrier crate (static 0x24) when none is alive, 3 exe 0x502960 / 4 exe 0x502a30
//   the soldiers (pool 0x20 type 0xb) from bone 2 while at most 2 pool-0x20 actors of the upper half live, 5 exe
//   0x502b10 the end (sparks, fire, animations 2 / 3, event 0x12 message 2, the kill counters). Flag 0x585 enables the
//   hits (exe 0x502d90): the front points (2/3 HP -> broken, flag 0x582, two fire emitters), then the side panels (1/3
//   HP -> sub 4, the flags 0x582 / 0x586), then the front again unless flag 0x587 (dead -> sub 5, the flags 0x582 /
//   0x587). Flag 0x584 + the defended vehicle 0x2801 away -> both vehicles freed. Flag 0x5a1 (the second room): the
//   gauge, sub 2 directly and the burning sequence exe 0x503720.
// The fireball (exe 0x5bc3f0; static 0x5c, its states 0x8d0c98: 0x5bc420 / 0x5bc560 / 0x5bc9c0): grows on bone 1 with
// five orbiting sparks, then homes on the player (pitch / yaw), hurts the defended vehicle by its damage on contact,
// bursts (16 sprites).
// The shadows (exe 0x5f20f0) are not called by these classes. The exe writes through null pointers when an allocation
// fails (the watchers of the bind, the turret pointer): port gaps there. Exe 0x40bf80 is game/matrix rotationZYX for
// the fireball's sparks: its only angles are (0, yaw, 0), where both orders give Ry(yaw).
#include "content/yosyonke/st0e_boss_vehicle.hpp"
#include "content/yosyonke/st0e_boss_vehicle_state.hpp"

#include "content/yosyonke/st0e_vehicles.hpp"
#include "engine/data_source.hpp"
#include "game/actor_animation.hpp"
#include "game/actor_collision.hpp"
#include "game/actor_model.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actor_stats.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/distances.hpp"
#include "game/draw_commands.hpp"
#include "game/event.hpp"
#include "game/game_state.hpp"
#include "game/hit_list.hpp"
#include "game/hit_points.hpp"
#include "game/hud.hpp"
#include "game/matrix.hpp"
#include "game/player/player_state.hpp"
#include "game/player/player_weapons.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/story_flags.hpp"

namespace od::port {

using namespace st0e_boss_vehicle_state;

namespace {

int16_t t16(int64_t v) { return int16_t(uint16_t(uint64_t(v))); }

constexpr uint32_t kNoFireball = 0xffffffffu;
constexpr size_t kFrontBoxA = 0, kFrontBoxB = 1, kChargeBox = 2, kCabBox = 3, kFirstPanelBox = 4, kPanelBoxes = 6;
constexpr size_t kVehicleBoxes = 10;
// flags (+0x15e)
constexpr uint8_t kVehicleFrontBroken = 0x01;
constexpr uint16_t kVehicleCrateWanted = 0x02;
constexpr uint8_t kVehicleFireballOn = 0x04;
constexpr uint16_t kVehicleCharging = 0x08;
constexpr uint16_t kVehicleChargeBroken = 0x10;
constexpr uint8_t kVehicleCabOff = 0x20;
constexpr uint8_t kVehicleVoice290Said = 0x40, kVehicleVoice291Said = 0x80;
constexpr uint16_t kVehicleVoice27eSaid = 0x100, kVehicleVoice27cSaid = 0x200; // (+0x15f bits 0 / 1)
enum class VehicleState : uint8_t { Bind = 0, Run = 1, Free = 2 };
enum class VehicleSub : uint8_t { SpawnTurret = 0, Drive = 1, DropCrates = 2, Soldiers = 3, LateSoldiers = 4, End = 5 };
enum class EndStep : uint8_t { Stop = 0, Fall = 1, Wait = 2, Done = 3 };
constexpr uint32_t kFlagFrontBroken = 0x582, kFlagTrainLost = 0x584, kFlagHitsOn = 0x585, kFlagPanelsBroken = 0x586,
                   kFlagVehicleDead = 0x587, kFlagSecondRoom = 0x5a1;
constexpr uint32_t kHitByPlayer = kHitAttackByPlayer;
constexpr int kTurretAhead = 0x6900;
constexpr uint8_t kCrateType = 0x24, kSoldierType = 0xb, kTurretType = 0x7b, kFireballType = 0x5c;
const DataTable<ScalarTable<int16_t, 16>> soldierWalkTicks{TableId::St0eBossVehicleSoldierWalkTicks};
// the burning sequence: the HP thresholds (ninths of the maximum) and the emitters' points
const DataTable<ScalarTable<uint8_t, 4>> burnThresholds{TableId::St0eBossVehicleBurnThresholds};
struct BurnPoint : DataRecord {
    int16_t x = 0, y = 0, z = 0;
    static BurnPoint load(const TableReader& r) {
        BurnPoint rec;
        rec.x = r.s16(0);
        rec.y = r.s16(2);
        rec.z = r.s16(4);
        return rec;
    }
};
const DataTable<RecordTable<BurnPoint, 2, 8>> burnPoints{TableId::St0eBossVehicleBurnPoints};
constexpr uint32_t kHarmlessSolid = kHitAttackHarmless | kHitAttackSolid;

// the stat row (the exe reads the row pointer at every use); its +8: the charge point's hit points
AnimatedActorStats stats(const AnimatedActor& a) { return animatedStats(a.type); }
// the owner word of an entry: the owner's slot, the row's height bits, the shape
uint32_t ownerWord(uint8_t slot, const AnimatedActorStats& row, uint32_t shape) {
    return hitOwner(slot) | hitHeightLevel(row.hitHeight) | shape;
}
uint8_t watcherSlot(uint32_t watcher) { return controllerActorAt(watcher).slot; }
void submit(const BonePoint& p, HitBoxHead box, uint32_t owner, uint32_t attack) { hitListSubmit(p.v, box, owner, attack); }

// live pool-0x20 actors among the slots 16..31 (the live mask's low half)
int liveUpperAnimated() {
    const uint32_t m = actorSystem().animatedLive >> 16;
    int n = 0;
    for (uint32_t k = 0; k < 16; ++k)
        if (m & (1u << k)) ++n;
    return n;
}
// the random fire of the dying vehicle: variant 0x15, mode 8, x / z +-0x100, y -0x100 - (0..0x3f), lifetime 0x1e
void randomFire(AnimatedActor& a) {
    const uint32_t at = allocActor60(kActorStageClass); // exe 0x5e12f0
    if (!at) return;
    StaticActor& e = staticActorAt(at);
    FireEmitterParams f(e);
    e.flags = kActorLive;
    e.type = kFireEmitterType;
    e.subtype = 0;
    e.variant = 0x15;
    e.spawnByte = 8;
    e.pos.x = int32_t(((rngStep() & 0x1ff) - 0x100) * 0x10000u);
    e.pos.y = int32_t((0xffffff00u - (rngStep() & 0x3f)) * 0x10000u);
    const uint32_t r = rngStep();
    f.paramA = 0;
    e.pos.z = int32_t(((r & 0x1ff) - 0x100) * 0x10000u);
    f.paramB = 0;
    f.lifetime = 0x1e;
    f.owner = a.at;
}
// the velocity tail of the sub-states: the speed - the acceleration down to -0x280, then the move along the yaw
void drive(AnimatedActor& a) {
    if (a.velocity.z >= -0x27f) a.velocity.z = int16_t(a.velocity.z - a.acceleration.z);
    else a.velocity.z = -0x280;
    integrateVelocity(a, 0, 0, a.velocity.z);
}

// exe 0x503330(actor, v): a soldier (pool 0x20 type 0xb sub 1 variant v) at bone 2 + ((v ? 0 : -0x80), (v ? 0x140 :
// 0x2b), 0x240), the vehicle's yaw, its walk ticks [0x897cc4 + (slot & 0xf) * 2] + 0x78 (its spin's x lane)
void spawnSoldier(AnimatedActor& a, uint8_t v) {
    const uint32_t at = allocActor20(); // exe 0x5e1160
    if (!at) return;
    AnimatedActor& e = animatedActorAt(at);
    e.flags = kActorLive;
    e.type = kSoldierType;
    e.subtype = 1;
    e.variant = v;
    e.spawnByte = 0;
    e.loopFlags |= kActorSweepGroup2;
    e.runFlags |= kActorWarpKept;
    const BonePoint p = v ? bonePointAt(a, 2, 0, 0x140, 0x240) : bonePointAt(a, 2, -0x80, 0x2b, 0x240);
    int32_t pos[3];
    fixedOfPoint(p, pos);
    e.pos.x = pos[0];
    e.pos.y = pos[1];
    e.pos.z = pos[2];
    e.rotation.z = 0;
    e.rotation.x = 0;
    e.rotation.y = a.rotation.y;
    e.spin.x = int16_t(soldierWalkTicks[e.slot & 0xfu] + 0x78);
}
// exe 0x5031e0: at most 9 pool-0x20 actors of the upper half -> a carrier crate (static stage 0x24 variant 0, its
// stat row 1) at bone 0 + (-0xb, -0x262, 0x333), its pitch -(rng & 0xff) but on the first of every 5, its yaw + 0xa00;
// the next one in (rng & 0x3f) + 0x78 ticks; sound 0x266
void dropCrate(AnimatedActor& a, VehicleFields& f) {
    if (liveUpperAnimated() > 9) return;
    const uint32_t at = allocActor60(kActorStageClass); // exe 0x5e12f0
    if (!at) return;
    StaticActor& e = staticActorAt(at);
    e.loopFlags |= uint8_t(kActorSweepGroup2 | kActorLateLoop);
    e.flags = kActorLive;
    e.type = kCrateType;
    e.subtype = 0;
    e.variant = 0;
    e.spawnByte = 1;
    e.runFlags |= kActorWarpKept;
    int32_t pos[3];
    fixedOfPoint(bonePointAt(a, 0, -0xb, -0x262, 0x333), pos);
    e.pos.x = pos[0];
    e.pos.y = pos[1];
    e.pos.z = pos[2];
    if (f.cratesBeforeTilt != 0) {
        --f.cratesBeforeTilt;
        e.rotation.x = int16_t(0u - (rngStep() & 0xff));
    } else {
        f.cratesBeforeTilt = 4;
        e.rotation.x = 0;
    }
    e.rotation.y = int16_t(a.rotation.y + 0xa00);
    f.timer = int16_t((rngStep() & 0x3f) + 0x78);
    soundPlayAtPos(0x266, e.pos);
}
// exe 0x503400 (kVehicleFireballOn): the fireball. Its aim turns bone 1 (0, -0xba, 0) toward the player. Charging: the
// first tick spawns the static 0x5c; it rides bone 1 (0, -0xe7, -0xde) with the aimed yaw; released after the charge's
// ticks (the rest 0x5a, sound 0x268) or burst when the charge point broke (the fireball's state 2, sounds 0x288 / 0x27a,
// the rest 0xb4). Idle: after the rest, the player within 0x100 of straight behind -> the charge (0x3c ticks, voice
// 0x280) unless the charge point broke; within 0x300 -> voice 0x27e once.
void fireball(AnimatedActor& a, VehicleFields& f) {
    VehicleBlock& blk = *f.block;
    aimStep(a, f.fireballAim, angleFromPlayer(bonePointAt(a, 1, 0, -0xba, 0)));
    const uint16_t flags = f.flags;
    if (flags & kVehicleCharging) {
        if (f.fireballsSpawned != 0) {
            if (flags & kVehicleChargeBroken) {
                if (blk.fireball != kNoFireball) {
                    StaticActor& ball = staticActorAt(blk.fireball);
                    ball.state = 2;
                    soundPlayAtPos(0x288, ball.pos);
                    soundPlayAtPos(0x27a, a.pos);
                    f.flagsLow &= uint8_t(~(kVehicleCharging | kVehicleChargeBroken));
                    blk.restTicks = 0xb4;
                    blk.fireball = kNoFireball;
                }
            } else if (blk.chargeTicks != 0) {
                --blk.chargeTicks;
            } else {
                f.flags = uint16_t(flags & ~kVehicleCharging);
                blk.restTicks = 0x5a;
                blk.fireball = kNoFireball;
                soundPlayAtPos(0x268, a.pos);
            }
        } else {
            blk.fireball = kNoFireball;
            const uint32_t at = allocActor60(kActorStageClass); // exe 0x5e12f0
            if (at) {
                StaticActor& e = staticActorAt(at);
                e.flags = kActorLive;
                e.loopFlags |= kActorSweepGroup2;
                e.type = kFireballType;
                e.subtype = 0;
                e.variant = 0;
                e.spawnByte = 0;
                e.runFlags |= kActorWarpKept;
                e.rotation.z = 0;
                e.rotation.x = 0;
                blk.fireball = e.at;
                ++f.fireballsSpawned;
            }
        }
        if (blk.fireball == kNoFireball) return;
        int32_t pos[3];
        fixedOfPoint(bonePointAt(a, 1, 0, -0xe7, -0xde), pos);
        StaticActor& e = staticActorAt(blk.fireball);
        e.pos.x = pos[0];
        e.pos.y = pos[1];
        e.pos.z = pos[2];
        e.rotation.y = int16_t(a.rotation.y + f.fireballAim);
        return;
    }
    if (flags & kVehicleCabOff) return;
    if (blk.restTicks != 0) {
        --blk.restTicks;
        return;
    }
    const uint32_t xy = a.rotation.xyWord(), zSpare = a.rotation.zSpareWord();
    a.rotation.y = int16_t(a.rotation.y + f.fireballAim);
    if (facesAwayFrom(a, player().pos, 0x100)) {
        if (!(f.flags & kVehicleChargeBroken)) {
            f.flags |= kVehicleCharging;
            blk.chargeTicks = 0x3c;
            f.fireballsSpawned = 0;
            vehicleVoice(0x280);
            a.rotation.setWords(xy, zSpare);
            return;
        }
    } else if (facesAwayFrom(a, player().pos, 0x300) && !(f.flags & kVehicleVoice27eSaid)) {
        vehicleVoice(0x27e);
        f.flagsHigh |= uint8_t(kVehicleVoice27eSaid >> 8);
    }
    a.rotation.setWords(xy, zSpare);
}
// exe 0x503720 (flag 0x5a1): the burning sequence: 0 / 2 wait for HP < [0x897cf8 + i] * max / 9; 1 / 3 a fire emitter
// (variant 0x15) at [0x897ce8 + (i >> 1) * 8]; 4 below max / 3: the burn's ticks of sparks + random fires every other
// tick; 5 the emitters end, flag 0x582 cleared, animation 1, the parts 2 / 3 hidden
void burning(AnimatedActor& a, VehicleFields& f) {
    const int16_t mx = a.maxHitPoints;
    VehicleBlock& blk = *f.block;
    const uint8_t i = blk.burnStep;
    switch (i) {
    case 0:
    case 2:
        if (int(burnThresholds[i]) * int(t16(mx / 9)) <= a.hitPoints) return;
        blk.burnStep = uint8_t(i + 1);
        return;
    case 1:
    case 3: {
        const BurnPoint t = burnPoints[i >> 1];
        StaticActor* e = spawnFireEmitter(a, 0x15, 0, uint32_t(int32_t(t.x)) << 16,
                                          uint32_t(int32_t(t.y)) << 16, uint32_t(int32_t(t.z)) << 16, 0);
        if (e) blk.emitters[i >> 1] = e->at;
        ++blk.burnStep;
        return;
    }
    case 4:
        if (int(mx) / 3 <= a.hitPoints) return;
        --blk.burnTicks;
        if (uint16_t(blk.burnTicks) == 0) blk.burnStep = uint8_t(i + 1);
        if (!(uint16_t(blk.burnTicks) & 1)) return;
        spawnBoneSpark(a, 0, 0xcc, -0x1ef, 8, true);
        randomFire(a);
        return;
    case 5:
        if (blk.emitters[0] == 0u || blk.emitters[1] == 0u)
            notPorted(kSt0ePort, "0x503720: an emitter of the block's +0x5c / +0x60 missing");
        staticActorAt(blk.emitters[0]).subState = kFireEmitterEnd;
        staticActorAt(blk.emitters[1]).subState = kFireEmitterEnd;
        ++blk.burnStep;
        clearStoryFlag(kFlagFrontBroken);
        a.anim = 1;
        a.parts[2].flags |= kPartHidden;
        a.parts[3].flags |= kPartHidden;
        return;
    default: return;
    }
}
// the front weak point hit (exe 0x502d90): the flash of bone 0, the damage; below 2/3 HP -> the front is broken (flag
// 0x582, the next sub-state, the fireball enabled, two fire emitters at the points, the voice)
void frontHit(AnimatedActor& a, VehicleFields& f, const HitBoxHead& box, int16_t third, uint16_t voiceId) {
    flashBone(a, 0);
    hitPointsAdd(a, -int(box.result & kHitDamageMask));
    if (int(a.hitPoints) >= int(third) * 2) return;
    f.flagsLow |= kVehicleFrontBroken;
    setStoryFlag(kFlagFrontBroken);
    ++a.subState;
    f.flagsLow |= kVehicleFireballOn;
    spawnFireEmitter(a, 0x15, 0, 0x510000, 0xfd9e0000u, 0x4000000, 0);
    spawnFireEmitter(a, 0x14, 0, 0xff980000u, 0xfd9e0000u, 0x4000000, 0);
    vehicleVoice(voiceId);
}
// exe 0x502d90 (flag 0x585): the hits
void vehicleHits(AnimatedActor& a, VehicleFields& f) {
    const int16_t third = t16(a.maxHitPoints / 3);
    VehicleBlock& blk = *f.block;
    int dead = 0;
    if (!(f.flagsLow & kVehicleFrontBroken)) {
        if (blk.boxes[kFrontBoxA].result & kHitByPlayer) frontHit(a, f, blk.boxes[kFrontBoxA], third, 0x282);
        else if (blk.boxes[kFrontBoxB].result & kHitByPlayer) frontHit(a, f, blk.boxes[kFrontBoxB], third, 0x284);
        if (!(f.flagsLow & kVehicleFrontBroken)) return;
    }
    if (blk.boxes[kChargeBox].result & kHitByPlayer) { // the charge point: its own HP
        const uint16_t keep = uint16_t(a.hitPoints);
        a.hitPoints = int16_t(f.chargePointHp);
        flashBone(a, 1);
        if (hitPointsAdd(a, -int(blk.boxes[kChargeBox].result & kHitDamageMask)) != 0) {
            f.chargePointHp = stats(a).bodyAttack;
            if (f.flags & kVehicleCharging) f.flags |= kVehicleChargeBroken;
        } else {
            f.chargePointHp = uint16_t(a.hitPoints);
        }
        a.hitPoints = int16_t(keep);
    }
    if (f.damageCoolDown != 0) --f.damageCoolDown;
    for (size_t k = 0; k < kPanelBoxes; ++k) {
        HitBoxHead& box = blk.boxes[kFirstPanelBox + k];
        if (!(box.result & kHitByPlayer)) continue;
        if (f.damageCoolDown == 0) {
            hitPointsAdd(a, -int(box.result & kHitDamageMask));
            f.damageCoolDown = 4;
        }
        if (third <= a.hitPoints) {
            flashBone(a, 2);
        } else {
            a.subState = uint8_t(VehicleSub::LateSoldiers);
            a.anim = 1;
            setStoryFlag(kFlagFrontBroken);
            setStoryFlag(kFlagPanelsBroken);
            f.flagsLow |= uint8_t(kVehicleChargeBroken | kVehicleCabOff);
            vehicleVoice(0x282);
        }
    }
    if (a.anim == 0) return;
    if (f.hurtVoiceTimer != 0) --f.hurtVoiceTimer;
    if (storyFlag(kFlagVehicleDead) || !(blk.boxes[kFrontBoxA].result & kHitByPlayer)) return;
    flashBone(a, 8);
    if (f.damageCoolDown == 0) {
        dead = hitPointsAdd(a, -int(blk.boxes[kFrontBoxA].result & kHitDamageMask));
        f.damageCoolDown = 4;
    }
    if (f.hurtVoiceTimer == 0) {
        vehicleVoice(0x279);
        f.hurtVoiceTimer = 0x78;
    }
    if (dead) {
        a.subState = uint8_t(VehicleSub::End);
        a.step = 0;
        setStoryFlag(kFlagFrontBroken);
        setStoryFlag(kFlagVehicleDead);
        vehicleVoice(0x279);
    }
}
// the turret kept kTurretAhead ahead of the vehicle along its yaw (the vehicle's position put back)
void placeTurretAhead(AnimatedActor& a, AnimatedActor& turret) {
    const int32_t x = a.pos.x, y = a.pos.y, z = a.pos.z;
    integrateVelocity(a, 0, 0, kTurretAhead);
    turret.pos = a.pos;
    a.pos.x = x;
    a.pos.y = y;
    a.pos.z = z;
}
// exe 0x502730: the turret (pool 0x20 type 0x7b) spawned kTurretAhead ahead along the yaw, the same yaw; Drive
void spawnTurret(AnimatedActor& a, VehicleFields& f) {
    const uint32_t at = allocActor20(); // exe 0x5e1160
    if (at) {
        AnimatedActor& e = animatedActorAt(at);
        e.loopFlags |= kActorSweepGroup2;
        e.flags = kActorLive;
        e.type = kTurretType;
        e.subtype = 0;
        e.variant = 0;
        e.spawnByte = 0;
        e.rotation.z = 0;
        e.rotation.x = 0;
        e.rotation.y = a.rotation.y;
        placeTurretAhead(a, e);
        f.turret = e.at;
    }
    a.subState = uint8_t(VehicleSub::Drive);
}
// exe 0x5027e0: the drive, the turret kept ahead
void driveWithTurret(AnimatedActor& a, VehicleFields& f) {
    if (a.velocity.z >= -0x27f) a.velocity.z = int16_t(a.velocity.z - a.acceleration.z);
    else a.velocity.z = -0x280;
    integrateVelocity(a, 0, 0, a.velocity.z);
    if (!f.turret) notPorted(kSt0ePort, "0x5027e0: no turret (+0x158)");
    placeTurretAhead(a, animatedActorAt(f.turret));
}
// exe 0x502870: the voices 0x290 (then 0x28 ticks) / 0x291; a crate wanted when the timer ran out and no static type
// 0x24 lives
void dropCratesSub(AnimatedActor& a, VehicleFields& f) {
    if (f.voiceDelay != 0) {
        --f.voiceDelay;
    } else if (f.flags & kVehicleVoice290Said) {
        if (!(f.flags & kVehicleVoice291Said)) {
            vehicleVoice(0x291);
            f.flagsLow |= kVehicleVoice291Said;
        }
    } else {
        vehicleVoice(0x290);
        f.flagsLow |= kVehicleVoice290Said;
        f.voiceDelay = 0x28;
    }
    bool found = false;
    for (size_t k = 0; k < StaticPool::size(); ++k)
        if (staticPool()[k].flags != 0 && staticPool()[k].type == kCrateType) {
            found = true;
            break;
        }
    if (f.timer != 0) --f.timer;
    else if (!found) f.flagsLow |= uint8_t(kVehicleCrateWanted);
    drive(a);
}
// exe 0x502960: the player within 0x7ff: voice 0x27c once; every 0x40 ticks (unless flag 0x582) a soldier of variant 0
// while at most 2 pool-0x20 actors of the upper half live
void soldiersSub(AnimatedActor& a, VehicleFields& f) {
    if (planarDistance(a.pos, player().pos) <= 0x7ff && !(f.flags & kVehicleVoice27cSaid)) {
        vehicleVoice(0x27c);
        f.flagsHigh |= uint8_t(kVehicleVoice27cSaid >> 8);
    }
    if (!storyFlag(kFlagFrontBroken) && !(gameState().frame & 0x3f) && liveUpperAnimated() <= 2) spawnSoldier(a, 0);
    drive(a);
}
// exe 0x502a30: the soldiers of variant 1 (voice 0x27b one time in 4), voice 0x278 every 0x4c ticks
void lateSoldiersSub(AnimatedActor& a, VehicleFields& f) {
    if (!(gameState().frame & 0x3f) && liveUpperAnimated() <= 2) {
        spawnSoldier(a, 1);
        if (!(rngStep() & 3)) vehicleVoice(0x27b);
    }
    drive(a);
    if (f.voiceTimer == 0x4b) {
        f.voiceTimer = 0;
        vehicleVoice(0x278);
        return;
    }
    ++f.voiceTimer;
}
// exe 0x502b10: the end: sparks / fire on every other tick, animation 2 -> 3 (once the key ends), 0x1e ticks, then the
// event 0x12 message 2 and the kill counters (exe 0x5e53e0)
void endSub(AnimatedActor& a, VehicleFields& f) {
    if (gameState().frame & 1) {
        spawnBoneSpark(a, 0, 0xcc, -0x1ef, 8, true);
        randomFire(a);
    }
    switch (EndStep(a.step)) {
    case EndStep::Stop:
        a.anim = 2;
        a.step = uint8_t(EndStep::Fall);
        break;
    case EndStep::Fall:
        if (int8_t(a.animKey.flags) <= -1) {
            a.anim = 3;
            a.step = uint8_t(EndStep::Wait);
            f.timer = 0x1e;
        }
        break;
    case EndStep::Wait:
        if (f.timer != 0) {
            --f.timer;
        } else {
            eventStartAtActor(a, 0x12, 0, 2); // exe 0x543af0(actor, 0x12, 0, 2)
            a.step = uint8_t(EndStep::Done);
            countDefeat(); // exe 0x5e53e0
        }
        break;
    default: break;
    }
    drive(a);
}
// exe 0x502510
void vehicleBind(AnimatedActor& a, VehicleFields& f) {
    if (!bindActorModel(a, 0)) { // exe 0x5e0ba0
        freeActor(a);            // exe 0x5e1490
        return;
    }
    a.runFlags |= kActorWarpKept;
    a.flags |= kActorDrawn;
    a.anim = 0;
    a.prevAnim = 0xff;
    if (storyFlag(kFlagSecondRoom)) {
        const uint16_t h = uint16_t(uint32_t(stats(a).hitPoints) << 6);
        a.maxHitPoints = int16_t(h);
        a.hitPoints = int16_t(h);
        f.chargePointHp = stats(a).bodyAttack;
        hudAddBossGauge(a, 0, 6, 0x2222aa); // exe 0x415d30
    }
    a.velocity.setXY(0, 0);
    a.collisionBox = nullptr;
    a.interactionBox = nullptr;
    a.interaction = kInteractLift;
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
    allocBoneBlocks(a, 1); // exe 0x5e16c0
    f.block.pointAt(boneBlockAddress(a, 3));
    VehicleBlock& blk = *f.block;
    a.state = uint8_t(VehicleState::Run);
    if (storyFlag(kFlagSecondRoom)) {
        a.subState = uint8_t(VehicleSub::DropCrates);
        f.timerLow = 0x3c;
        f.timerHigh = 0;
        f.damageCoolDown = 0;
        blk.restTicks = 0;
        blk.chargeTicks = 0;
        blk.burnStep = 0;
        blk.burnTicks = 0x5a;
    } else {
        a.subState = uint8_t(VehicleSub::SpawnTurret);
        f.damageCoolDown = 0;
        blk.restTicks = 0;
        blk.chargeTicks = 0;
        blk.burnStep = 0;
    }
    a.step = 0;
    f.clearHead();
    f.clearFlagsWord();
    f.voiceTimer = 0;
    f.resetVoiceWord();
    for (size_t k = 0; k < 2; ++k) {
        blk.watchers[k] = spawnHitWatcher(a).at;
        clearStoryFlag(kFlagFrontBroken);
    }
    for (size_t k = 0; k < kVehicleBoxes; ++k) blk.boxes[k].result = 0;
}
// exe 0x5026f0
void vehicleRun(AnimatedActor& a, VehicleFields& f) {
    if (storyFlag(kFlagHitsOn)) vehicleHits(a, f);
    switch (VehicleSub(a.subState)) {
    case VehicleSub::SpawnTurret: spawnTurret(a, f); return;
    case VehicleSub::Drive: driveWithTurret(a, f); return;
    case VehicleSub::DropCrates: dropCratesSub(a, f); return;
    case VehicleSub::Soldiers: soldiersSub(a, f); return;
    case VehicleSub::LateSoldiers: lateSoldiersSub(a, f); return;
    case VehicleSub::End: endSub(a, f); return;
    default: notPortedAt(kSt0ePort, "0x501d70 sub-state", a.subState);
    }
}
// exe 0x501d70
void bossVehicle(AnimatedActor& a) {
    VehicleFields& f = classState<VehicleFields>(a);
    const AnimatedActorStats row = stats(a);
    a.prevAnim = a.anim;
    a.tint = kTintNeutral;
    a.savePreviousPose();
    a.savePreviousContact();
    if (storyFlag(kFlagTrainLost) && planarDistance(a.pos, actorAt(defendedVehicleAddress()).pos) >= 0x2801) {
        a.state = uint8_t(VehicleState::Free);
        if (!f.turret) notPorted(kSt0ePort, "0x501d70: no turret (+0x158) to free");
        animatedActorAt(f.turret).state = 2;
    }
    switch (VehicleState(a.state)) {
    case VehicleState::Bind:
        vehicleBind(a, f);
        if (!(a.flags & kActorLive)) return; // freed by the bind (the exe goes on with the freed actor's tail)
        vehicleRun(a, f);                    // the bind ends with exe 0x5026f0
        break;
    case VehicleState::Run: vehicleRun(a, f); break;
    case VehicleState::Free: freeActor(a); break; // exe 0x468fa0
    default: notPortedAt(kSt0ePort, "0x501d70 state", a.state);
    }
    if (a.anim != a.prevAnim) animSet(a, a.anim, 0); // exe 0x5e1c50
    else animStep(a);                                // exe 0x5e1ea0
    LaneVec4 headAngles = decodedFrame().entries[2];
    headAngles.y = int16_t(f.fireballAim + headAngles.y);
    animPose(a); // exe 0x5e2d90
    VehicleBlock& blk = *f.block;
    for (size_t k = 0; k < kVehicleBoxes; ++k) {
        HitBoxHead& box = blk.boxes[k];
        box.result = 0;
        box.previousX = a.prevPos.x.whole;
        box.previousZ = a.prevPos.z.whole;
    }
    actorCollisionSubmitSolid(a, SolidType::StandOn, 0, 0); // exe 0x414050
    if (f.flagsLow & kVehicleFireballOn) fireball(a, f);
    if (f.flags & kVehicleCrateWanted) {
        f.flags = uint16_t(f.flags & ~kVehicleCrateWanted);
        dropCrate(a, f);
    }
    if (storyFlag(kFlagSecondRoom)) burning(a, f);
    if (!storyFlag(kFlagSecondRoom)) return;
    const uint32_t attack = row.attack | kHarmlessSolid;
    const uint32_t front = hitGroup(kHitGroupBodies) | hitRadius(0x68);
    if (f.flagsLow & kVehicleFrontBroken) {
        if (a.anim != 0)
            submit(bonePointAt(a, 4, 0, 0, 0), blk.boxes[kFrontBoxA], ownerWord(watcherSlot(blk.watchers[0]), row, front),
                   attack);
    } else {
        submit(bonePointAt(a, 0, 0x51, -0x262, 0x400), blk.boxes[kFrontBoxA],
               ownerWord(watcherSlot(blk.watchers[0]), row, front), attack);
        submit(bonePointAt(a, 0, -0x68, -0x262, 0x400), blk.boxes[kFrontBoxB],
               ownerWord(watcherSlot(blk.watchers[1]), row, front), attack);
    }
    if (f.flagsLow & kVehicleCabOff) return;
    const uint8_t own = a.slot;
    const uint32_t panel = hitGroup(kHitGroupBodies) | kHitNoLockOn | hitRadius(0x70);
    const uint32_t cabBig = hitGroup(kHitGroupBodies) | kHitNoLockOn | hitRadius(0x90);
    const uint32_t cabSmall = hitGroup(kHitGroupBodies) | kHitNoLockOn | hitRadius(0x54);
    submit(bonePointAt(a, 1, 0, -0xba, 0), blk.boxes[kChargeBox],
           ownerWord(own, row, hitGroup(kHitGroupBodies) | hitRadius(0xa0)), attack);
    submit(bonePointAt(a, 0, 0xcc, -0x154, -0xcd), blk.boxes[4], ownerWord(own, row, panel), attack);
    submit(bonePointAt(a, 0, 0xcc, -0x154, 8), blk.boxes[5], ownerWord(own, row, panel), attack);
    submit(bonePointAt(a, 0, 0xcc, -0x154, 0xdc), blk.boxes[6], ownerWord(own, row, panel), attack);
    submit(bonePointAt(a, 0, 0xcc, -0x1ef, -0xcd), blk.boxes[7], ownerWord(own, row, panel), attack);
    submit(bonePointAt(a, 0, 0xcc, -0x1ef, 8), blk.boxes[8], ownerWord(own, row, panel), attack);
    submit(bonePointAt(a, 0, 0xcc, -0x1ef, 0xdc), blk.boxes[9], ownerWord(own, row, panel), attack);
    HitBoxHead& cab = blk.boxes[kCabBox];
    submit(bonePointAt(a, 0, 0x128, -0x2a5, -0x29a), cab, ownerWord(own, row, cabBig), attack);
    submit(bonePointAt(a, 0, 0x128, -0x2a5, -0x18f), cab, ownerWord(own, row, cabBig), attack);
    submit(bonePointAt(a, 0, 0x128, -0x1ad, -0x29a), cab, ownerWord(own, row, cabBig), attack);
    submit(bonePointAt(a, 0, 0x128, -0x1ad, -0x18f), cab, ownerWord(own, row, cabBig), attack);
    submit(bonePointAt(a, 0, 0x93, -0x2c4, -0x18f), cab, ownerWord(own, row, cabSmall), attack);
    submit(bonePointAt(a, 0, 0x3c, -0x2c4, -0x18f), cab, ownerWord(own, row, cabSmall), attack);
    submit(bonePointAt(a, 0, -0x5f, -0x2c4, -0x18f), cab, ownerWord(own, row, cabBig), attack);
}

enum class FireballState : uint8_t { Init = 0, Fly = 1, Burst = 2 };
enum class FlySub : uint8_t { Grow = 0, Home = 1 };
const DataTable<CollisionBoxRecord> fireballBox{TableId::St0eBossVehicleFireballBox};
const DataTable<ScalarTable<uint16_t, 16>> burstAngles{TableId::St0eBossVehicleBurstAngles};
const DataTable<ScalarTable<uint16_t, 16>> burstShifts{TableId::St0eBossVehicleBurstShifts};
const DataTable<ScalarTable<int16_t, 16>> burstRises{TableId::St0eBossVehicleBurstRises};
constexpr uint16_t kBallBurstSound = 0x288, kBallLaunchSound = 0x267;
constexpr int16_t kBallSpeedLimit = -0x600;

// exe 0x5bc9e0: the ball (the 0x2c billboard of its colour and size)
void ballSprite(StaticActor& a, FireballFields& f) {
    dc::emit({0, f.colour, uint32_t(uint16_t(a.pos.x.whole)) | uint32_t(uint16_t(a.pos.y.whole)) << 16, 0x7d902020u,
              uint16_t(a.pos.z.whole), 0x2f203fu, uint32_t(int32_t(f.size)), 0x3f20u, 0, 0x3f3fu},
             a.at, false);
}
// exe 0x5bca60(actor, p, k): spark k at p
void ballSpark(StaticActor& a, FireballFields& f, const int16_t* p, size_t k) {
    dc::emit({0, f.colour, uint32_t(uint16_t(p[0])) | uint32_t(uint16_t(p[1])) << 16, 0x7d902020u,
              uint32_t(uint16_t(p[2])), 0x2f203fu, uint32_t(int32_t(f.sparkSize[k])), 0x3f20u, 0, 0x3f3fu},
             a.at, false);
}
// exe 0x5bcae0: the burst -- 16 sprites spreading with the step (the angles burstAngle + [0x8d0cb4], the shifts
// [0x8d0cd4], the rises [0x8d0d54]), the sizes 0x30 - 2k; the step + 1
void ballBurst(StaticActor& a, FireballFields& f) {
    for (size_t k = 0; k < 16; ++k) {
        const int32_t c = f.burstStep - 1;
        const uint32_t ang = uint32_t(int32_t(int16_t(f.burstAngle)) + int32_t(burstAngles[k])) & kAngleMask;
        const int32_t cs = fixedCos(int(ang)), sn = fixedSin(int(ang)); // exe 0x40b940 / 0x40b920
        const uint32_t sh = uint8_t(burstShifts[k]) & 31;
        const uint16_t x = uint16_t(t16((cs * c) >> sh) + a.pos.x.whole);
        const uint16_t y = uint16_t(burstRises[k] * 0x10 * c + a.pos.y.whole);
        const uint16_t z = uint16_t(t16((sn * c) >> sh) + a.pos.z.whole);
        dc::emit({0, f.colour, uint32_t(x) | uint32_t(y) << 16, 0x7d902020u, z, 0x2f203fu,
                  uint32_t(uint16_t(t16(int32_t(k) * -2 + 0x30))), 0x3f20u, 0, 0x3f3fu},
                 a.at, false);
    }
    ++f.burstStep;
}
// exe 0x5bc560: Grow -- the size by 6 to 0x100, then Home; five sparks orbiting (each: a delay, a fall from 0x180 by
// 0x20, a size re-randomised; the point -0x10 * fall along (its pitch, its yaw) turned by the ball's yaw). Home: the
// shield / a hit box end it; the player within 0x400 of straight behind and farther than 0x200 -> a turn of 0x20 and the
// pitch +-0x10 at the player; the speed -0x180 a tick to -0x600; else after the homing ticks it ends; the walls / an
// actor contact end it (the contact hurts the defended vehicle by the row's damage)
void ballFly(const PlayerEnv& env, StaticActor& a, FireballFields& f) {
    const uint32_t target = defendedVehicleAddress();
    const StaticActorStats row = staticStats(a.type, 0);
    if (FlySub(a.subState) == FlySub::Grow) {
        if (f.size <= 0xff) {
            const int16_t v = t16(f.size + 6);
            f.size = v;
            if (v >= 0x101) {
                f.size = 0x100;
                ++a.subState;
            }
        }
        ballSprite(a, f);
        for (size_t k = 0; k < 5; ++k) {
            if (f.sparkDelay[k] != 0) {
                --f.sparkDelay[k];
                continue;
            }
            int16_t& fall = f.sparkFall[k];
            if (fall != 0) {
                const int16_t v = t16(fall - 0x20);
                fall = v;
                if (v <= -1) fall = 0;
            } else {
                fall = 0x180;
                f.sparkSize[k] = int16_t((rngStep() & 0xf) + 0x20);
            }
            // exe 0x5e3810 on a zero point: the spark's offset along (its pitch, its yaw)
            const int16_t rot[3] = {f.sparkPitch[k], f.sparkYaw[k], 0};
            int32_t d[3];
            rotateVector(rot, 0, 0, fall * -0x10, d);
            const int16_t in[3] = {int16_t(uint32_t(d[0]) >> 16), int16_t(uint32_t(d[1]) >> 16),
                                   int16_t(uint32_t(d[2]) >> 16)};
            int16_t out[3];
            transformShortVector(rotationZYX(0, a.rotation.y, 0), in, out); // exe 0x40bf80(0, yaw, 0), exe 0x40bb30
            const int16_t p[3] = {t16(out[0] + a.pos.x.whole), t16(out[1] + a.pos.y.whole),
                                  t16(out[2] + a.pos.z.whole)};
            ballSpark(a, f, p, k);
        }
        actorCollisionSubmitMover(a); // exe 0x4140b0
        return;
    }
    auto end = [&]() {
        a.state = uint8_t(FireballState::Burst);
        ballSprite(a, f);
        soundPlayAtPos(kBallBurstSound, a.pos);
    };
    if (playerShieldBlocks(a, 0x6b) || (f.box.result & kHitAttackBump)) {
        end();
        return;
    }
    if (facesAwayFrom(a, player().pos, 0x400)) {
        if (planarDistance(player().pos, a.pos) >= 0x201) {
            turnYawTo(a.rotation.y, uint32_t(int32_t(t16(angleOf(player().pos, a.pos)))), 0x20); // exe 0x5e3df0
            int16_t p = a.rotation.x;
            if (t16((0x800 - pitchOf(player().pos, a.pos)) - p) >= 1) p = t16(p + 0x10);
            else p = t16(p - 0x10);
            a.rotation.x = p;
        }
        if (a.velocity.z >= kBallSpeedLimit) a.velocity.z = int16_t(a.velocity.z - 0x180);
        else a.velocity.z = kBallSpeedLimit;
    } else {
        if (a.velocity.z >= kBallSpeedLimit) a.velocity.z = int16_t(a.velocity.z - 0x180);
        else a.velocity.z = kBallSpeedLimit;
        if (f.homingTicks != 0) --f.homingTicks;
        else a.state = uint8_t(FireballState::Burst);
    }
    moveAlongRotation(a.pos, a.rotation, 0, 0, a.velocity.z); // exe 0x5e3810
    const uint32_t owner = hitOwner(a.slot) | hitHeightLevel(row.hitHeight) | hitGroup(3) | kHitNoLockOn | hitRadius(0x68);
    const uint32_t attack = row.damage | kHitAttackStrong;
    auto entry = [&]() {
        f.box.result = 0;
        f.box.previousX = a.prevPos.x.whole;
        f.box.previousZ = a.prevPos.z.whole;
        const int16_t p[3] = {a.pos.x.whole, a.pos.y.whole, a.pos.z.whole};
        hitListSubmit(p, f.box, owner, attack);
    };
    entry();
    if (int32_t(actorTestMove(env, a)) <= -1) { // exe 0x40f570
        end();
        return;
    }
    if (a.actorHit != 0) {
        a.state = uint8_t(FireballState::Burst);
        hitPointsAdd(actorAt(target), -int(row.damage));
        ballSprite(a, f);
        soundPlayAtPos(kBallBurstSound, a.pos);
        return;
    }
    entry();
    ballSprite(a, f);
    actorCollisionSubmitMover(a); // exe 0x4140b0
}
// exe 0x5bc3f0
void vehicleFireball(const PlayerEnv& env, StaticActor& a) {
    FireballFields& f = classState<FireballFields>(a);
    a.prevPos = a.pos;
    switch (FireballState(a.state)) {
    case FireballState::Init: { // exe 0x5bc420
        f.size = 0;
        f.colour = 0x2e808080u;
        a.collisionBox.pointAt(fireballBox.stateAddress());
        ++a.state;
        for (size_t k = 0; k < 5; ++k) {
            f.sparkDelay[k] = uint8_t(k * 2);
            f.sparkSize[k] = int16_t((rngStep() & 0xf) + 0x20);
            f.sparkYaw[k] = int16_t(((k - 2) & 0xfu) << 8);
            f.sparkPitch[k] = int16_t(int32_t(rngStep() & 0x1ff) - 0xff);
            f.sparkFall[k] = 0x180;
        }
        f.burstStep = 1;
        const uint32_t r = rngStep();
        f.box.result = 0;
        f.burstAngle = uint16_t(r & 0xfff);
        f.box.previousX = a.prevPos.x.whole;
        f.box.previousZ = a.prevPos.z.whole;
        f.homingTicks = 0x3c;
        ballFly(env, a, f);
        soundPlayAtPos(kBallLaunchSound, a.pos);
        return;
    }
    case FireballState::Fly: ballFly(env, a, f); return;
    case FireballState::Burst: // exe 0x5bc9c0
        if (f.burstStep >= 0x15) freeActor(a); // exe 0x5e1540
        else ballBurst(a, f);
        return;
    default: notPortedAt(kSt0ePort, "0x5bc3f0 state", a.state);
    }
}

} // namespace

void bossVehicleUpdate(const PlayerEnv&, uint32_t actor) { bossVehicle(animatedActorAt(actor)); }
void vehicleFireballUpdate(const PlayerEnv& env, uint32_t actor) { vehicleFireball(env, staticActorAt(actor)); }

} // namespace od::port
