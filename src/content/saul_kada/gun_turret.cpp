// The pirates' gun turret (pool 0x20 type 0xf: ST29 / ST2B, exe 0x46bad0; hypothesis names), one routine per exe
// routine.
//
// States [0x8901d4]: 0 bind 0x46bbf0, 1 the sub-states [0x8901e0] with its hit entry 0x46bca0, 2 destroyed 0x46bd70
// (a Servbot jumps out), 3 / 4 the sub-states 0 / 1 directly. Sub-states: 0 idle until the player is within 0x1200,
// 1 aim (the gun's pitch / yaw toward the player, a pause) -> 3 fire four shots (pool 0x40 type 9) from the two barrels
// (bones 3 / 4), 2 a sweep, 4 recoil on a heavy hit. The gun's pitch / yaw go onto parts 2 / 1; damaged below 0x20 hit
// points it smokes now and then.
#include "content/saul_kada/gun_turret.hpp"
#include "content/saul_kada/gun_turret_state.hpp"

#include <cstdlib>

#include "game/actor_animation.hpp"
#include "game/actor_model.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actor_stats.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/cutscene_actor.hpp"
#include "game/distances.hpp"
#include "game/explosions.hpp"
#include "game/field_alias.hpp"
#include "game/vectors.hpp"
#include "game/global_class_types.hpp"
#include "game/hit_list.hpp"
#include "game/hit_points.hpp"
#include "game/pickups.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/projection.hpp"
#include "game/random.hpp"
#include "game/spark_emitter.hpp"
#include "game/spark_emitter_state.hpp"
#include "game/story.hpp"

namespace od::port {

using namespace gun_turret_state;

namespace {

// [0x8901d4]
enum class TurretState : uint8_t { Bind = 0, Run = 1, Destroyed = 2, Idle = 3, Aim = 4 };
// [0x8901e0]
enum class TurretSub : uint8_t { Idle = 0, Aim = 1, Sweep = 2, Fire = 3, Recoil = 4 };
enum class AimStep : uint8_t { Start = 0, Pause = 1, Count = 2, Lock = 3 };
enum class FireStep : uint8_t { BurstA = 0, GapA = 1, BurstB = 2, Check = 3, BurstC = 4, Done = 5 };
enum class RecoilStep : uint8_t { Start = 0, Shake = 1 };
enum class DestroyedStep : uint8_t { Start = 0, Explode = 1, Wait = 2 };
// The shot's parameters (pool 0x40 stage type 9: its class is not this file's)
struct TurretShotParams {
    explicit TurretShotParams(const ProjectileActor& actor) : TurretShotParams(const_cast<ProjectileActor&>(actor)) {}
    explicit TurretShotParams(ProjectileActor& actor) : size(actor.params, 0), damage(actor.params, 2) {}
    BytesField<uint16_t, 4> size;
    BytesField<uint16_t, 4> damage;
};
constexpr uint8_t kShotType = 9;
constexpr uint16_t kShotSize = 0x20;
constexpr uint32_t kFirstBarrelBone = 3;
constexpr uint8_t kServbotType = 0xb;
constexpr uint32_t kGunPitchPart = 2, kGunYawPart = 1;
constexpr uint32_t kFarDistance = 0x1200;
constexpr int8_t kGiveUpTicks = 0x30;
constexpr int16_t kHeavyDamage = 0x20;
constexpr int16_t kDamagedHitPoints = 0x20;
constexpr uint16_t kHurtSound = 0x8d, kRecoilSound = 0x8e;
constexpr uint16_t kExplosionSpread = 0x80;
constexpr uint8_t kExplosionVariant = 1;
constexpr uint8_t kDestroyedTicks = 0x20;
// its hit entry: a body (radius 0x60) with its row's height level; solid while it has hit points, a bump after
constexpr uint32_t kTurretShape = hitGroup(kHitGroupBodies) | hitRadius(0x60);

enum HitOutcome : int { kDestroyed = -1, kNoHit = 0, kHit = 1, kHeavyHit = 2 };

// its row ([*0xa714c0 + type * 4] + spawn byte * 16); 0 = none
bool hasRow(const AnimatedActor& actor) { return hasAnimatedStats(actor.type); }
AnimatedActorStats row(const AnimatedActor& actor) { return animatedStats(actor.type, actor.spawnByte); }
void enterSub(AnimatedActor& actor, TurretSub sub) {
    actor.subState = uint8_t(sub);
    actor.restartStep();
}
// destroyed: the state word (the sub-state cleared, the step kept)
void destroy(AnimatedActor& actor) { actor.enterStateKeepingStep(uint8_t(TurretState::Destroyed)); }

// exe 0x46c330: exe 0x5e3a10(turret, player) > 0x1200
bool playerFar(const AnimatedActor& actor) { return uint32_t(distance3(actor.pos, player().pos)) > kFarDistance; }
// exe 0x46c3e0: the gun (0x80 above) projects in front, inside 320 x 240
bool gunOnScreen(const AnimatedActor& actor) {
    const int16_t p[3] = {actor.pos.x.whole, int16_t(actor.pos.y.whole - 0x80), actor.pos.z.whole};
    int16_t screen[2] = {0, 0};
    if (projectOnScreen(p, screen) != 0) return false; // exe 0x5f2dd0
    return uint16_t(screen[0]) < 0x140 && uint16_t(screen[1]) < 0xf0;
}
// exe 0x46c350: the hits (the player's): the flash, sound 0x8d, the damage adds up (heavy past 0x20); else it decays
int takeHit(AnimatedActor& actor, TurretFields& turret) {
    const uint32_t hit = turret.hitBox.result, damage = hit & kHitDamageMask;
    if ((hit & kHitAttackByPlayer) && damage) {
        int outcome = kHit;
        actor.tint = kTintFlash;
        soundPlayAtPos(kHurtSound, actor.pos);
        turret.recentDamage += damage;
        if (turret.recentDamage > kHeavyDamage) outcome = kHeavyHit;
        if (hitPointsAdd(actor, -int(damage))) return kDestroyed; // exe 0x5e52f0
        return outcome;
    }
    --turret.recentDamage;
    if (turret.recentDamage < 0) turret.recentDamage = 0;
    return kNoHit;
}
// exe 0x46c440: the gun turns toward the player (pitch 0x20 / yaw 0x40 a tick; the pitch held within 0x200 up /
// 0xe00 down); true once both are on target
bool aim(const AnimatedActor& actor, TurretFields& turret) {
    const Player& p = player();
    // the exe's stack points: the gun (0x100 above the turret), the player's chest (0x80 above him)
    const int32_t gun[3] = {actor.pos.x, int32_t(uint32_t(actor.pos.y) - 0x1000000u), actor.pos.z};
    const int32_t chest[3] = {p.pos.x, int32_t(uint32_t(p.pos.y) + 0xff800000u), p.pos.z};
    const int pitch = pitchOf(gun, chest); // exe 0x5e3ca0
    int done = 0;
    const int targetPitch = pitch + (-0x7e0 - int(int8_t(turret.shots)) * 4);
    const int dp = angleDiff(uint32_t(int32_t(turret.gunPitch)), uint32_t(targetPitch)); // exe 0x5e3d80
    if (std::abs(dp) > 0x20) {
        turret.gunPitch += dp > 0 ? 0x20u : 0xffe0u;
    } else {
        turret.gunPitch = int16_t(targetPitch);
        done = 1;
    }
    const uint32_t angle = uint16_t(turret.gunPitch) & 0xfff;
    if (angle < 0x800) {
        if (angle > 0x200) {
            turret.gunPitch = 0x200;
            done = 0;
        }
    } else if (angle < 0xe00) {
        turret.gunPitch = 0xe00;
        done = 0;
    }
    const int targetYaw = angleOf(gun[0], gun[2], chest[0], chest[2]) + (-0x800 - int(actor.rotation.y)); // 0x5e3bc0
    const int dy = angleDiff(uint32_t(int32_t(turret.gunYaw)), uint32_t(targetYaw));
    if (std::abs(dy) > 0x40) {
        turret.gunYaw += dy > 0 ? 0x40u : 0xffc0u;
        return done == 3;
    }
    turret.gunYaw = int16_t(targetYaw);
    done |= 2;
    return done == 3;
}
// the hit outcome the idle / sweep sub-states share
void onHit(AnimatedActor& actor, TurretFields& turret, int outcome) {
    if (outcome == kNoHit) return;
    if (outcome < 0) {
        destroy(actor);
        return;
    }
    if (outcome == kHit) {
        turret.farTicks = 0;
        actor.subState = uint8_t(TurretSub::Aim);
        actor.enterStep(uint8_t(AimStep::Count));
        return;
    }
    enterSub(actor, TurretSub::Recoil);
}
// the hit outcome of the aim / fire sub-states
void onHitWhileAiming(AnimatedActor& actor, TurretFields& turret) {
    const int outcome = takeHit(actor, turret);
    if (outcome < 0) destroy(actor);
    else if (outcome > kHit) enterSub(actor, TurretSub::Recoil);
}

// exe 0x46be70
void idle(AnimatedActor& actor, TurretFields& turret) {
    if (!playerFar(actor)) enterSub(actor, TurretSub::Aim);
    onHit(actor, turret, takeHit(actor, turret));
}
// exe 0x46bed0: aim, then fire
void aimAtPlayer(AnimatedActor& actor, TurretFields& turret) {
    aim(actor, turret);
    switch (AimStep(actor.step)) {
    case AimStep::Start:
        turret.farTicks = 0;
        turret.aimTimer = 0x20;
        ++actor.step;
        break;
    case AimStep::Pause:
        --turret.aimTimer;
        if (turret.aimTimer == 0) ++actor.step;
        break;
    case AimStep::Count:
        if (turret.shots == 0) {
            turret.aimTimer = 0x20;
            ++actor.step;
        } else {
            --turret.shots;
        }
        break;
    case AimStep::Lock:
        if (turret.aimTimer == 0) {
            if (aim(actor, turret)) {
                turret.aimTimer = 8;
                turret.shots = 0;
                enterSub(actor, TurretSub::Fire);
            }
        } else {
            --turret.aimTimer;
        }
        break;
    default: break;
    }
    if (playerFar(actor)) {
        ++turret.farTicks;
        if (turret.farTicks > kGiveUpTicks) enterSub(actor, TurretSub::Sweep);
    } else {
        turret.farTicks = 0;
    }
    onHitWhileAiming(actor, turret);
}
// exe 0x46bfc0: the sweep (the yaw 0x10 a tick, the pitch nodding) until the player is near again
void sweep(AnimatedActor& actor, TurretFields& turret) {
    switch (actor.step) {
    case 0:
        turret.sweepTimer = 0x40;
        ++actor.step;
        [[fallthrough]];
    case 1: {
        turret.gunYaw += 0x10u;
        const int16_t pitch = turret.gunPitch;
        if (pitch != 0) turret.gunPitch = int16_t(pitch > 0 ? pitch - 8 : pitch + 8);
        --turret.sweepTimer;
        if (turret.sweepTimer == 0) {
            turret.sweepTimer = 0x10;
            ++actor.step;
        }
        break;
    }
    case 2: case 4: case 6: case 8: case 10:
        --turret.sweepTimer;
        if (turret.sweepTimer == 0) {
            turret.sweepTimer = 0x20;
            ++actor.step;
        }
        break;
    case 3: case 9:
        turret.gunPitch -= 8u;
        --turret.sweepTimer;
        if (turret.sweepTimer == 0) {
            turret.sweepTimer = 8;
            ++actor.step;
        }
        break;
    case 5: case 7:
        turret.gunPitch += 8u;
        --turret.sweepTimer;
        if (turret.sweepTimer == 0) {
            turret.sweepTimer = 8;
            ++actor.step;
        }
        break;
    case 11: actor.step = 0; break;
    default: break;
    }
    if (!playerFar(actor)) enterSub(actor, TurretSub::Aim);
    onHit(actor, turret, takeHit(actor, turret));
}
// a shot (pool 0x40 stage type 9) from the barrel of the count's bit 1, the barrels alternating
void fireShot(AnimatedActor& actor, TurretFields& turret, int8_t count) {
    if (const uint32_t slot = allocActor40(kActorStageClass)) { // exe 0x5e1280
        ProjectileActor& shot = projectileActorAt(slot);
        TurretShotParams params(projectileActorAt(slot));
        shot.flags = kActorLive;
        shot.loopFlags |= kActorSweepGroup2;
        shot.type = kShotType;
        shot.variant = 0;
        params.size = kShotSize;
        params.damage = hasRow(actor) ? uint16_t(row(actor).strongAttack) : uint16_t(0);
        // exe 0x5e1810: the barrel's bone point, read unchecked as the exe does
        const ShortVec4& barrel = *bonePointOrNull(actor, uint32_t(((count >> 1) & 1) + int(kFirstBarrelBone)));
        shot.pos.x = int32_t(uint32_t(int32_t(barrel.x)) << 16);
        shot.pos.y = int32_t(uint32_t(int32_t(barrel.y)) << 16);
        shot.pos.z = int32_t(uint32_t(int32_t(barrel.z)) << 16);
        shot.rotation.x = turret.gunPitch;
        shot.rotation.y = int16_t(((count & 2) << 3) + actor.rotation.y + turret.gunYaw - 8);
        shot.rotation.z = turret.gunRoll;
    }
    ++turret.shots;
}
// exe 0x46c100: three bursts (shots at the odd counts down from 8), re-aim or give up
void fire(AnimatedActor& actor, TurretFields& turret) {
    aim(actor, turret);
    switch (FireStep(actor.step)) {
    case FireStep::BurstA:
    case FireStep::BurstB:
    case FireStep::BurstC: {
        const int8_t count = int8_t(turret.aimTimer - 1u);
        turret.aimTimer = uint8_t(count);
        if (!(count & 1)) fireShot(actor, turret, count);
        if (count == 0) {
            turret.aimTimer = 0x10;
            ++actor.step;
        }
        break;
    }
    case FireStep::GapA:
        --turret.aimTimer;
        if (turret.aimTimer == 0) {
            turret.aimTimer = 8;
            ++actor.step;
        }
        break;
    case FireStep::Check:
        --turret.aimTimer;
        if (turret.aimTimer == 0) {
            if (!playerFar(actor) && gunOnScreen(actor)) {
                turret.aimTimer = 0x28;
                ++actor.step;
            } else {
                actor.step += 2u;
            }
        }
        break;
    case FireStep::Done: enterSub(actor, TurretSub::Aim); break;
    default: break;
    }
    onHitWhileAiming(actor, turret);
}
// exe 0x46c2a0: recoil
void recoil(AnimatedActor& actor, TurretFields& turret) {
    if (RecoilStep(actor.step) == RecoilStep::Start) {
        actor.anim = 1;
        actor.animKey.frame = 0;
        turret.recoiled = 0;
        turret.recentDamage = 0;
        soundPlayAtPos(kRecoilSound, actor.pos);
        ++actor.step;
    } else if (RecoilStep(actor.step) == RecoilStep::Shake && actor.animEnded()) {
        if (turret.recoiled != 0) {
            actor.anim = 0;
            actor.animKey.frame = 0;
            enterSub(actor, TurretSub::Aim);
        } else {
            turret.recoiled = 1;
        }
    }
    if (takeHit(actor, turret) < 0) destroy(actor);
}
void runSub(AnimatedActor& actor, TurretFields& turret, TurretSub sub) {
    switch (sub) {
    case TurretSub::Idle: idle(actor, turret); return;
    case TurretSub::Aim: aimAtPlayer(actor, turret); return;
    case TurretSub::Sweep: sweep(actor, turret); return;
    case TurretSub::Fire: fire(actor, turret); return;
    case TurretSub::Recoil: recoil(actor, turret); return;
    default: notPortedAt(kStoryPort, "0x46bad0 sub-state", uint32_t(sub));
    }
}

// exe 0x46bbf0
void bind(AnimatedActor& actor, TurretFields& turret) {
    if (!bindActorModel(actor, 0)) { // exe 0x5e0ba0
        freeActor(actor);            // exe 0x5e1490
        return;
    }
    actor.anim = 0;
    actor.prevAnim = kNoAnimShown;
    actor.animKey.frame = 0;
    const uint16_t hp = hasRow(actor) ? uint16_t(row(actor).hitPoints) : uint16_t(0);
    actor.maxHitPoints = int16_t(hp);
    actor.hitPoints = int16_t(hp);
    turret.hitBox.previousX = actor.prevPos.x.whole;
    turret.hitBox.previousZ = actor.prevPos.z.whole;
    turret.hitBox.result = 0;
    turret.gunPitch = 0x200;
    turret.gunYaw = 0;
    turret.gunRoll = 0;
    turret.recentDamage = 0;
    turret.recoiled = 0;
    turret.smokeTicks = 0;
    turret.aimTimer = 0;
    turret.shots = 0;
    turret.farTicks = 0;
    turret.sweepTimer = 0;
    actor.enterStateKeepingStep(uint8_t(TurretState::Run));
}
// exe 0x46bca0
void run(AnimatedActor& actor, TurretFields& turret) {
    runSub(actor, turret, TurretSub(actor.subState));
    if (!(actor.flags & kActorDrawn)) return;
    turret.hitBox.previousX = actor.prevPos.x.whole;
    turret.hitBox.previousZ = actor.prevPos.z.whole;
    turret.hitBox.result = 0;
    const int16_t pos[3] = {actor.pos.x.whole, int16_t(actor.pos.y.whole - 0x80), actor.pos.z.whole};
    const bool rowed = hasRow(actor);
    const uint32_t shape = hitOwner(actor.slot) | hitHeightLevel(rowed ? row(actor).hitHeight : 0u) | kTurretShape;
    const uint32_t attack =
        (rowed ? uint32_t(row(actor).attack) : 0u) | (actor.hitPoints < 0 ? kHitAttackBump : kHitAttackSolid);
    hitListSubmit(pos, turret.hitBox, shape, attack); // exe 0x5e4120
}
// exe 0x46bd70: destroyed -- the death explosion, then the drops and a Servbot jumping out; false once freed
bool destroyed(AnimatedActor& actor) {
    switch (DestroyedStep(actor.subState)) {
    case DestroyedStep::Start:
        actor.anim = 1;
        actor.animKey.frame = 0;
        [[fallthrough]];
    case DestroyedStep::Explode: {
        actor.step = kDestroyedTicks;
        if (const uint32_t slot = allocActor60(0)) { // exe 0x5e1300
            StaticActor& explosion = staticActorAt(slot);
            explosion.flags = kActorLive;
            explosion.loopFlags |= kActorSweepGroup2;
            explosion.type = typeByte(GlobalStaticType::Explosion);
            explosion.variant = kExplosionVariant;
            explosion.pos.x = actor.pos.x;
            explosion.pos.y = int32_t(uint32_t(actor.pos.y) - 0x800000u);
            explosion.pos.z = actor.pos.z;
            ExplosionParams params(actorHeaderAt(slot));
            params.spread = kExplosionSpread;
            params.flashes = actor.step;
        }
        actor.subState = uint8_t(DestroyedStep::Wait);
        break;
    }
    case DestroyedStep::Wait: {
        --actor.step;
        if (actor.step != 0) break;
        spawnDrops(actor.pos, hasRow(actor) ? uint32_t(row(actor).dropTable) : 0u); // exe 0x5f1270
        if (const uint32_t slot = allocActor20()) { // exe 0x5e1160: the Servbot inside jumps out
            AnimatedActor& servbot = animatedActorAt(slot);
            servbot.flags = kActorLive;
            servbot.loopFlags |= kActorSweepGroup2;
            servbot.type = kServbotType;
            servbot.variant = 0;
            servbot.pos.x = actor.pos.x;
            servbot.pos.y = int32_t(uint32_t(actor.pos.y) - 0x100u);
            servbot.pos.z = actor.pos.z;
        }
        freeActor(actor);
        return false;
    }
    default: break;
    }
    return true;
}
// damaged: a smoke puff now and then (the stage's static type 3)
void smoke(const AnimatedActor& actor, TurretFields& turret) {
    if (turret.smokeTicks != 0) {
        --turret.smokeTicks;
        return;
    }
    if (rngStep() & 7) return;
    if (const uint32_t slot = allocActor60(kActorStageClass)) { // exe 0x46c5a0
        StaticActor& puff = staticActorAt(slot);
        spark_emitter_state::SparkEmitterFields& params = classState<spark_emitter_state::SparkEmitterFields>(staticActorAt(slot));
        puff.flags = kActorLive;
        puff.loopFlags |= kActorSweepGroup2;
        puff.type = kSparkEmitterStageType;
        puff.variant = 0;
        puff.pos.x = actor.pos.x;
        puff.pos.y = int32_t(uint32_t(actor.pos.y) - 0x800000u);
        puff.pos.z = actor.pos.z;
        params.driftX = 0;
        params.driftZ = 0;
        params.lifetime = int16_t((rngStep() & 0xf) + 4);
    }
    turret.smokeTicks = uint8_t((rngStep() & 0x3f) + 0x20);
}

// exe 0x46bad0
void gunTurret(AnimatedActor& actor) {
    TurretFields& turret = classState<TurretFields>(actor);
    actor.prevAnim = actor.anim;
    actor.prevPos = actor.pos;
    actor.tint = kTintNeutral;
    actor.prevRotation = actor.rotation;
    actor.savePreviousContact();
    switch (TurretState(actor.state)) {
    case TurretState::Bind: bind(actor, turret); break;
    case TurretState::Run: run(actor, turret); break;
    case TurretState::Destroyed:
        if (!destroyed(actor)) return;
        break;
    case TurretState::Idle: runSub(actor, turret, TurretSub::Idle); break;
    case TurretState::Aim: runSub(actor, turret, TurretSub::Aim); break;
    default: notPortedAt(kStoryPort, "0x46bad0 state", actor.state);
    }
    if (actor.flags & kActorLive) {
        if (actor.anim != actor.prevAnim) animSet(actor, actor.anim, actor.animKey.frame); // exe 0x5e1c50
        else animStep(actor);                                                             // exe 0x5e1ea0
        DecodedFrame& frame = decodedFrame();
        frame.entries[1 + kGunPitchPart].x += uint16_t(turret.gunPitch);
        frame.entries[1 + kGunYawPart].y += uint16_t(turret.gunYaw);
        animPose(actor); // exe 0x5e2d90
    }
    if (!(actor.flags & kActorDrawn) || actor.hitPoints >= kDamagedHitPoints) return; // (the shadow exe 0x5f20f0: render)
    smoke(actor, turret);
}

} // namespace

void pirateGunTurretUpdate(const PlayerEnv&, uint32_t actor) { gunTurret(animatedActorAt(actor)); }

} // namespace od::port
