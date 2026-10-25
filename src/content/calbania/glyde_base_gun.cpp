// The gun emplacements of Glyde's base (ST30 sub 1, pool 0x20 type 0x41; see glyde_base_gun.hpp), transcribed from
// dash2.exe one function per exe function. It sets no flag: the ST30 sub 1 room handler (exe 0x585670) counts its wave
// on the wave counter. States [0x894b5c]: 0 init 0x4c04c0, 1 0x4c05d0 (the hurt test 0x4c0d60, sub-states [0x894b68]:
// 0 0x4c0610 reset -- the first behaviour by variant [0x894b74] --, 1 0x4c0670 lob, 2 0x4c0790 shoot), 2 destroyed
// 0x4c0c20. The barrel's pitch (the rotation's spare lane) is added to the barrel part's decoded pitch before the pose;
// the fire mode asked this tick fires after it (0x4c0910 the shell, 0x4c0b30 the shots).
#include "content/calbania/glyde_base_gun.hpp"
#include "content/calbania/glyde_base_gun_state.hpp"

#include "content/calbania/appo_dah_fight.hpp"

#include "engine/data_source.hpp"
#include "game/actor_animation.hpp"
#include "game/actor_model.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actor_stats.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/distances.hpp"
#include "game/encounter.hpp"
#include "game/enemy_neighbours.hpp"
#include "game/enemy_shot.hpp"
#include "game/explosions.hpp"
#include "game/global_class_types.hpp"
#include "game/hit_effects.hpp"
#include "game/hit_list.hpp"
#include "game/hit_points.hpp"
#include "game/lobbed_shot.hpp"
#include "game/pickups.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/story_flags.hpp"
#include "game/vectors.hpp"
#include "game/visibility.hpp"

namespace od::port {

using namespace glyde_base_gun_state;

namespace {

constexpr const char* kGunPort = "glyde base gun port"; // the area of its port-gap errors

constexpr uint8_t kArcMask = 0x1f;
// +0x161
constexpr int8_t kGunNoHitEntry = 0x01;
constexpr int8_t kGunNoDamage = 0x02;
constexpr int32_t kGunHitFlag = 0x80; // an owner bit of its hit entry (set in its last dying ticks)
// +0x15d / +0x15e
constexpr uint8_t kTargetPlayer = 1, kTargetAppo = 2, kTargetDah = 4;
// +0x15f
constexpr uint8_t kIgnoreAppo = 0x01, kIgnoreDah = 0x02;
constexpr uint8_t kCountdownFull = 0x80;

enum class GunState : uint8_t { Init = 0, Active = 1, Destroyed = 2 };
enum class GunSub : uint8_t { Reset = 0, Lob = 1, Shoot = 2 };
enum class FireMode : int8_t { None = 0, Lob = 1, ShotsNear = 2, ShotsFar = 3 };

const DataTable<ScalarTable<uint8_t, 2>> firstSubByVariant{TableId::GlydeBaseGunFirstSubByVariant};
constexpr uint32_t kFlagAppoCarried = 0x581, kFlagDahCarried = 0x582;
constexpr int kArcFar = 0x1400;    // a target out of the arc (or too near): out of reach
constexpr int kFreeFar = 0x3000;   // a target given up (no arc)
constexpr int kTooNear = 0x400;    // variant 0 does not aim nearer
constexpr int kGiveUpNear = 0xc00; // a given-up target stays so within this
constexpr int kLobMin = 0x400, kLobMax = 0x1400;
constexpr int kShotRange = 0xc00, kNearShotRange = 0xa00;
constexpr uint16_t kHurtSound = 0x8d, kShellSound = 0xad;
constexpr uint32_t kHurtAttacks = 0x1c0000; // the hit kinds that hurt it (the player's attacks)
constexpr uint8_t kShotVariant = 1, kShellVariant = 0x82, kMuzzleFlashVariant = 3;
constexpr int16_t kShotTicks = 0x23;
constexpr uint16_t kMuzzleFlashSize = 0xc0;
constexpr uint32_t kMuzzleBone = 1;
constexpr size_t kBarrelAngles = 2; // the decoded frame's entry of the barrel's angles (part 1)
constexpr uint16_t kFlashTint = 0x4214;

void enterSub(AnimatedActor& actor, GunSub s) {
    actor.subState = uint8_t(s);
    actor.restartStep();
}
AnimatedActorStats statsOf(const AnimatedActor& actor) { return animatedStats(actor.type); }

// exe 0x4c0d20: the scale by variant (0 0x280, 1 0x300; the others keep theirs)
void applyScale(AnimatedActor& actor) {
    uint16_t s;
    switch (actor.variant) {
    case 0: s = 0x280; break;
    case 1: s = 0x300; break;
    default: return;
    }
    actor.scale.x = int16_t(s);
    actor.scale.y = int16_t(s);
    actor.scale.z = int16_t(s);
}
// exe 0x4c0d60: a hit by the player's attacks -> the flash, sound 0x8d, the hit points; out -> destroyed (-1)
int hurt(AnimatedActor& actor, GunFields& f) {
    const uint32_t v = f.hitBox.result, d = v & kHitDamageMask;
    if (d == 0) return 0;
    uint32_t n = 0;
    if (v & kHurtAttacks) {
        actor.tint = kTintFlash;
        soundPlayAtPos(kHurtSound, actor.pos);
        n = d;
    }
    if (hitPointsAdd(actor, -int(n)) != 0) {
        actor.enterState(uint8_t(GunState::Destroyed));
        return -1;
    }
    return 0;
}
// a downed Appo / Dah (no arc): given up once its countdown runs out, until it is out of kGiveUpNear
int allyDistance(AnimatedActor& actor, GunFields& f, const Actor& ally, uint8_t ignoreBit, uint8_t kind) {
    int d = planarDistance(actor.pos, ally.pos);
    if (classState<AppoDahFightFields>(asAnimated(ally)).carried != 0) {
        f.ignored &= uint8_t(~ignoreBit);
        return kFreeFar;
    }
    const uint8_t ignored = f.ignored;
    if (ignored & ignoreBit) {
        if (d <= kGiveUpNear) d = kFreeFar;
        else f.ignored = uint8_t(ignored & ~ignoreBit);
    } else if (f.targetKind & kind) {
        if (ally.hitPoints >= 0) {
            f.downCountdown = kCountdownFull;
        } else {
            --f.downCountdown;
            if (f.downCountdown == 0) f.ignored = uint8_t(ignored | ignoreBit);
        }
    }
    return d;
}
// exe 0x4c0dc0(actor, out): the nearest target -> out (the player / the actor of the Appo slot / of the Dah slot), its
// planar distance returned; the target's kind kept, the countdown reset when it changed
int nearestTarget(AnimatedActor& actor, GunFields& f, uint32_t& out) {
    const Player& p = player();
    int toPlayer, toAppo, toDah;
    if (f.arc & kArcMask) {
        auto inArc = [&](int ang) { return ang > f.arcMin && ang < f.arcMax; };
        auto inReach = [&](const Fixed16Vec3& pos) {
            const int d = planarDistance(actor.pos, pos);
            return (actor.variant == 0 && d < kTooNear) ? kArcFar : d;
        };
        toPlayer = kArcFar;
        if (inArc(angleOf(p.pos, actor.pos))) toPlayer = inReach(p.pos);
        toAppo = kArcFar;
        if (!storyFlag(kFlagAppoCarried) && f.appoSlot != 0) {
            const Actor& o = actorAt(animatedPoolSlot(f.appoSlot));
            if (inArc(angleOf(o.pos, actor.pos)) && o.hitPoints >= 0) toAppo = inReach(o.pos);
        }
        toDah = kArcFar;
        if (!storyFlag(kFlagDahCarried) && f.dahSlot != 0) {
            const Actor& o = actorAt(animatedPoolSlot(f.dahSlot));
            if (inArc(angleOf(o.pos, actor.pos)) && o.hitPoints >= 0) toDah = inReach(o.pos);
        }
    } else {
        toPlayer = planarDistance(actor.pos, p.pos);
        toAppo = kFreeFar;
        if (!storyFlag(kFlagAppoCarried) && f.appoSlot != 0)
            toAppo = allyDistance(actor, f, actorAt(animatedPoolSlot(f.appoSlot)), kIgnoreAppo, kTargetAppo);
        toDah = kFreeFar;
        if (!storyFlag(kFlagDahCarried) && f.dahSlot != 0)
            toDah = allyDistance(actor, f, actorAt(animatedPoolSlot(f.dahSlot)), kIgnoreDah, kTargetDah);
    }
    f.prevTargetKind = f.targetKind;
    int d;
    if (toPlayer > toAppo) {
        d = toAppo;
        out = animatedPoolSlot(f.appoSlot);
        f.targetKind = kTargetAppo;
    } else {
        out = p.at;
        d = toPlayer;
        f.targetKind = kTargetPlayer;
    }
    if (d > toDah) {
        d = toDah;
        out = animatedPoolSlot(f.dahSlot);
        f.targetKind = kTargetDah;
    }
    if (f.prevTargetKind != f.targetKind) f.downCountdown = kCountdownFull;
    return d;
}
// the barrel's pitch eased toward t by 0x10
void pitchToward(GunFields& f, int t) {
    const int c = f.barrelPitch;
    if (t > c) f.barrelPitch = int16_t(t - c > 0x10 ? c + 0x10 : t);
    else f.barrelPitch = int16_t(c - t > 0x10 ? c - 0x10 : t);
}
// the barrel's pitch kept within [lo, hi]
void clampPitch(GunFields& f, int16_t hi, int16_t lo) {
    if (f.barrelPitch > hi) f.barrelPitch = hi;
    if (f.barrelPitch < lo) f.barrelPitch = lo;
}
// exe 0x4c0640
void resetMotion(AnimatedActor& actor, GunFields& f) {
    actor.anim = 0;
    actor.velocity.setXY(0, 0);
    actor.velocity.z = 0;
    actor.acceleration.z = 0;
    applyScale(actor);
    f.timer = 0x80;
}
// exe 0x4c0610
void reset(AnimatedActor& actor, GunFields& f) {
    resetMotion(actor, f);
    f.bits = 0;
    actor.restartStep();
    actor.subState = firstSubByVariant[actor.variant];
}
// exe 0x4c0670: tracks the target; every (0x80 >> ((7 - the script slot's alive) >> 1)) + random ticks with the target
// between kLobMin and kLobMax: a lobbed shell
void lob(AnimatedActor& actor, GunFields& f) {
    uint32_t t = 0;
    const int dist = nearestTarget(actor, f, t);
    f.target = t;
    turnYawTo(actor.rotation.y, uint32_t(angleOf(actorAt(t).pos, actor.pos)), 0x20); // exe 0x5e3df0
    if (f.arc & kArcMask) {
        if (actor.rotation.y < f.arcMin) actor.rotation.y = f.arcMin;
        if (actor.rotation.y > f.arcMax) actor.rotation.y = f.arcMax;
    }
    pitchToward(f, -(dist >> 3));
    clampPitch(f, int16_t(-0x100), int16_t(-0x200));
    if (dist > kLobMin && dist < kLobMax) {
        --f.timer;
        if (f.timer == 0) {
            f.fireMode = int8_t(FireMode::Lob);
            rngStep();
            const int n = encounterScriptSlotAlive();
            const uint32_t sh = uint32_t((7 - n) >> 1) & 0x1f;
            f.timer = uint16_t((0x80u >> sh) + (rngLastValue() & 0x1fu));
        }
    }
}
// exe 0x4c0790: aims at the target's place 1/8 tick ahead; facing it within reach: shots (near: every 4th tick, far:
// every 8th) in the first 0x12 ticks of each 0x2a
void shoot(AnimatedActor& actor, GunFields& f) {
    uint32_t t = 0;
    const int dist = nearestTarget(actor, f, t);
    const Actor& target = actorAt(t);
    const int32_t ahead[3] = {int32_t(uint32_t(target.pos.x) * 9u - (uint32_t(target.prevPos.x) << 3)),
                              int32_t(uint32_t(target.pos.y) * 9u - (uint32_t(target.prevPos.y) << 3)),
                              int32_t(uint32_t(target.pos.z) * 9u - (uint32_t(target.prevPos.z) << 3))};
    const int32_t self[3] = {actor.pos.x, actor.pos.y, actor.pos.z};
    f.shotPitch = int16_t(kHalfTurn - pitchOf(ahead, self)); // exe 0x5e3ca0
    const int ang = angleOf(ahead[0], ahead[2], actor.pos.x, actor.pos.z);
    turnYawTo(actor.rotation.y, uint32_t(ang), 0x20);
    const int dy = int(target.pos.y.whole) - int(actor.pos.y.whole) - 0xc8;
    pitchToward(f, dy / ((dist >> 10) + 1));
    clampPitch(f, int16_t(-0xc8), int16_t(-0x200));
    int d = int(uint16_t(actor.rotation.y) & kAngleMask) - int(uint32_t(ang) & kAngleMask);
    if (uint32_t(d + 0x800) > 0x1000u) d = (-d ^ kAngleMask) + 1;
    if (uint32_t(d + 0x40) >= 0x80u) return;
    --f.timer;
    if (f.timer == 0) f.timer = 0x2a;
    if (dist >= kShotRange || int16_t(f.timer) >= 0x12) return;
    const uint8_t ticks = uint8_t(f.timer);
    if (dist > kNearShotRange) {
        if (!(ticks & 7)) f.fireMode = int8_t(FireMode::ShotsFar);
    } else if (!(ticks & 3)) {
        f.fireMode = int8_t(FireMode::ShotsNear);
    }
}
// exe 0x4c0c20: an explosion (static global type 8), flashing (settled first when on the ground), the drops, freed
void destroyed(AnimatedActor& actor, GunFields& f) {
    actor.tint = kTintFlash;
    if (actor.step == 0) {
        actor.step = 1;
        if (actor.airborne == 0) {
            actor.velocity.x = 0;
            actor.velocity.y = 0;
            actor.velocity.z = 0;
            f.timer = 0x14;
        } else {
            f.timer = 8;
        }
        actor.acceleration.y = 0x40;
        countDefeat(); // exe 0x5e53e0
        if (const uint32_t address = allocActor60(0)) { // exe 0x5e1300
            StaticActor& e = staticActorAt(address);
            e.flags = kActorLive;
            e.type = typeByte(GlobalStaticType::Explosion);
            e.variant = 0;
            e.loopFlags |= kActorSweepGroup2;
            e.pos.x.whole = actor.pos.x.whole;
            e.pos.y.whole = int16_t(actor.pos.y.whole - 0x60);
            e.pos.z.whole = actor.pos.z.whole;
            ExplosionParams explosion(actorHeaderAt(address));
            explosion.spread = 0x30;
            explosion.flashes = f.timer;
        }
    }
    if (uint8_t(f.timer) & 1) actor.tint = kFlashTint;
    if (int16_t(f.timer) < 5) f.bits = int8_t(uint8_t(f.bits) | uint8_t(kGunHitFlag));
    --f.timer;
    if (f.timer != 0) return;
    spawnDrops(actor.pos, statsOf(actor).dropTable); // exe 0x5f1270
    freeActor(actor);
}
// exe 0x4c0b30(actor, pitch): a shot (pool 0x40 stage type 9 variant 1) from the muzzle, left / right by the cycle
void fireShot(AnimatedActor& actor, GunFields& f, int pitch) {
    const bool left = ((int16_t(f.timer) >> (f.fireMode & 0x1f)) & 1) != 0;
    const int16_t in[3] = {int16_t(left ? 0xa : -0xa), 0, -200};
    int16_t out[3];
    bonePointOffset(actor, kMuzzleBone, in, out); // exe 0x5e1970
    const uint32_t address = allocActor40(kActorStageClass); // exe 0x5e1280
    if (!address) return;
    ProjectileActor& s = projectileActorAt(address);
    s.flags = kActorLive;
    s.type = kEnemyShotType;
    s.variant = kShotVariant;
    s.loopFlags |= kActorSweepGroup2;
    s.pos.x.whole = out[0];
    s.pos.y.whole = out[1];
    s.pos.z.whole = out[2];
    s.rotation.x = int16_t(pointVisibleOrNear(actor.pos, 0) != 0 ? pitch + 0x10 : pitch); // exe 0x5e59a0
    s.rotation.y = actor.rotation.y;
    s.rotation.z = 0;
    EnemyShotFields shot(projectileActorAt(address));
    shot.ticks = kShotTicks;
    shot.attack = statsOf(actor).shotAttack;
}
// the integer part (the high half) of a 16.16 value + d
void addWhole(int32_t& v, int d) {
    v = int32_t((uint32_t(v) & 0xffffu) | (uint32_t(uint16_t(uint16_t(uint32_t(v) >> 16) + uint16_t(d))) << 16));
}
// exe 0x4c0910(actor, target): a shell (pool 0x40 stage type 7 variant 0x82, ending in a blast) lobbed at a random
// point near the target (nearer when the gun is on screen), and the muzzle flash (the hit spark, variant 3)
void fireShell(AnimatedActor& actor, uint32_t t) {
    const int16_t in[3] = {0, 0, -200};
    int16_t out[3];
    bonePointOffset(actor, kMuzzleBone, in, out);
    const Actor& target = actorAt(t);
    int32_t aim[3] = {target.pos.x, int32_t(uint32_t(target.pos.y) + 0xffa60000u), target.pos.z}; // y whole - 0x5a
    const uint32_t address = allocActor40(kActorStageClass);
    if (!address) return;
    ProjectileActor& s = projectileActorAt(address);
    s.flags = kActorLive;
    s.type = kLobbedShotStageType;
    s.variant = kShellVariant;
    s.loopFlags |= kActorSweepGroup2;
    s.pos.x.whole = out[0];
    s.pos.y.whole = out[1];
    s.pos.z.whole = out[2];
    s.rotation.x = 0;
    s.rotation.y = actor.rotation.y;
    s.rotation.z = 0;
    const uint32_t mask = pointVisibleOrNear(actor.pos, 0) != 0 ? 0x01552900u : 0x084ff208u;
    const uint32_t r = rngStep();
    if (int32_t(mask << (r & 0x1f)) >= 0) {
        addWhole(aim[0], int((rngStep() & 0xf) - 8) << 5);
        addWhole(aim[2], int((rngStep() & 0xf) - 8) << 5);
    } else {
        addWhole(aim[0], int(rngStep() & 0xf) * 8 - 0x40);
        addWhole(aim[2], int(rngStep() & 0xf) * 8 - 0x40);
    }
    s.velocity.x = 0;
    const int32_t from[3] = {s.pos.x, s.pos.y, s.pos.z};
    s.velocity.y = int16_t(launchSpeed(from, aim, 0x600, 0x30, 0x1770)); // exe 0x5e3ee0
    s.velocity.z = int16_t(0xfa00);
    LobbedShotParams shell(projectileActorAt(address));
    shell.blastSize = 0x100;
    s.acceleration.y = 0x30;
    shell.size = 0x20;
    shell.damage = int16_t(statsOf(actor).strongAttack);
    shell.launchSound = int16_t(kShellSound);
    if (const uint32_t flash = allocActorA0(0)) { // exe 0x5e13b0
        EffectActor& e = effectActorAt(flash);
        e.flags = kActorLiveDrawn;
        e.type = typeByte(GlobalEffectType::HitSpark);
        e.variant = kMuzzleFlashVariant;
        e.loopFlags |= kActorSweepGroup2;
        e.pos.x = s.pos.x.whole;
        e.pos.y = s.pos.y.whole;
        e.pos.z = s.pos.z.whole;
        classState<HitSparkFields>(effectActorAt(flash)).size = kMuzzleFlashSize;
    }
}
// exe 0x4c04c0
void init(AnimatedActor& actor, GunFields& f) {
    actor.setDrawn(true);
    if (!bindActorModel(actor, actor.variant)) { // exe 0x5e0bc0(actor, &variant)
        freeActor(actor);
        return;
    }
    actor.runFlags |= kActorRunMarked;
    actor.rotation.x = 0;
    actor.rotation.z = 0;
    actor.anim = 0;
    actor.prevAnim = 0xff;
    const uint16_t hp = hasAnimatedStats(actor.type) ? statsOf(actor).hitPoints : 0;
    actor.maxHitPoints = int16_t(hp);
    actor.hitPoints = int16_t(hp);
    actor.collisionBox.pointAt(TableId::GlydeBaseGunBox);
    actor.interactionBox.pointAt(TableId::GlydeBaseGunInteractionBox);
    actor.interaction = 0;
    actor.airborne = 0;
    actor.onPlatform = 0;
    actor.clearClassBlock();
    f.hitBox.previousX = actor.pos.x.whole;
    f.hitBox.previousZ = actor.pos.z.whole;
    f.hitBox.result = 0;
    actor.rotation.y = int16_t(uint16_t(actor.rotation.y) & kAngleMask);
    const uint16_t arc = uint16_t((f.arc & kArcMask) << 6);
    f.arcMin = int16_t(uint16_t(actor.rotation.y) - arc);
    f.arcMax = int16_t(uint16_t(actor.rotation.y) + arc);
    f.unused160 = 0;
    f.downCountdown = kCountdownFull;
    f.targetKind = 0;
    f.prevTargetKind = 0;
    registerEnemy(actor.slot, 2); // exe 0x5e5c10
    actor.enterState(uint8_t(GunState::Active));
    applyScale(actor);
}

// exe 0x4c0350's body
void gun(AnimatedActor& actor) {
    GunFields& f = classState<GunFields>(actor);
    actor.prevAnim = actor.anim;
    actor.savePreviousPose();
    actor.savePreviousContact();
    f.fireMode = int8_t(FireMode::None);
    switch (GunState(actor.state)) {
    case GunState::Init: init(actor, f); break;
    case GunState::Active: // exe 0x4c05d0
        actor.tint = kTintNeutral;
        if (!(f.bits & kGunNoDamage)) hurt(actor, f);
        switch (GunSub(actor.subState)) {
        case GunSub::Reset: reset(actor, f); break;
        case GunSub::Lob: lob(actor, f); break;
        case GunSub::Shoot: shoot(actor, f); break;
        default: notPortedAt(kGunPort, "class 0x4c0350 sub-state", actor.subState);
        }
        break;
    case GunState::Destroyed: destroyed(actor, f); break;
    default: notPortedAt(kGunPort, "class 0x4c0350 state", actor.state);
    }
    if (!(actor.flags & kActorLive)) return;
    if (actor.anim == actor.prevAnim) animStep(actor); // exe 0x5e1ea0
    else animSet(actor, actor.anim, 0);                // exe 0x5e1c50
    LaneVec4 barrel = decodedFrame().entries[kBarrelAngles];
    barrel.x = int16_t(uint16_t(f.barrelPitch) + uint16_t(barrel.x));
    animPose(actor); // exe 0x5e2d90
    const FireMode mode = FireMode(f.fireMode);
    if (mode == FireMode::Lob) fireShell(actor, f.target);
    else if (mode == FireMode::ShotsNear || mode == FireMode::ShotsFar)
        fireShot(actor, f, int(f.shotPitch) + int(int16_t(f.timer)));
    const AnimatedActorStats stats = statsOf(actor);
    const int16_t pos[3] = {actor.pos.x.whole, int16_t(actor.pos.y.whole - 0x32), actor.pos.z.whole};
    f.hitBox.result = 0;
    f.hitBox.previousX = actor.prevPos.x.whole;
    f.hitBox.previousZ = actor.prevPos.z.whole;
    const int32_t b = f.bits;
    if (b & kGunNoHitEntry) return;
    const uint32_t attack =
        GunState(actor.state) == GunState::Destroyed ? kHitAttackBump : (stats.attack | kHitAttackSolid);
    hitListSubmit(pos, f.hitBox, hitOwner(actor.slot) | (uint32_t(b & kGunHitFlag) << 12) | 0x400037u,
                  attack); // exe 0x5e4120
}

} // namespace

void glydeBaseGunUpdate(const PlayerEnv&, uint32_t actor) { gun(animatedActorAt(actor)); }

} // namespace od::port
