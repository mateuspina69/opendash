// ST3B (the Saul Kada pirate base, story 12): the fixed bomb turret (pool 0x20 type 0x12 sub 0, ST3B table 0x8feac8, exe
// 0x472100; hypothesis names), one routine per exe routine.
//
// States [0x8903c4]: 0 bind 0x472240 (the hit points of its row, the yaw offset from its parameter), 1 run 0x4722f0 (a
// hit -> sound 0x8d / 0x8e, the hit points; dead -> state 2; else the sub-state [0x8903d0]), 2 death 0x4727a0 (a death
// explosion, 0x1e ticks later its row's drops and freed); the table goes on with the three sub-states as states 3..5.
// Sub-states: 0 sweep 0x472370 (the yaw offset swings by +-0x10 between +-0x280 with 0x2d-tick pauses at each end; the
// player in front within +-0x400 and nearer than 0x640 -> 1), 1 track 0x472440 (the yaw offset turns 0x20 per tick
// toward the player; beyond 0x9c4 -> 0, the yaw limit -> 2), 2 fire 0x472520 (3 bombs 10 ticks apart, aimed 0x40 / 0 /
// -0x40 above the player's height, then 0x1e ticks -> 1; beyond 0x9c4 -> 0). 0x4724d0: the aim test (within +-0x10 of
// the player -> 2), 0x472840: the +-0x280 yaw clamp. The yaw offset turns the gun (part 2's decoded yaw).
// The shadow (exe 0x5f20f0) is render output. Spawned and ported elsewhere: the death explosion (game/explosions), the
// lobbed bomb (pool 0x40 ST3B type 7, game/lobbed_shot), the hit spark (game/hit_effects).
#include "content/saul_kada/st3b_bomb_turret.hpp"
#include "content/saul_kada/st3b_bomb_turret_state.hpp"

#include "game/actor_animation.hpp"
#include "game/actor_model.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actor_stats.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/cutscene_actor.hpp"
#include "game/distances.hpp"
#include "game/explosions.hpp"
#include "game/field_alias.hpp"
#include "game/global_class_types.hpp"
#include "game/hit_effects.hpp"
#include "game/hit_list.hpp"
#include "game/hit_points.hpp"
#include "game/lobbed_shot.hpp"
#include "game/pickups.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/story.hpp"

namespace od::port {

using namespace st3b_bomb_turret_state;

namespace {

// [0x8903c4] (the table continues with the sub-states as states 3..5)
enum class TurretState : uint8_t { Bind = 0, Run = 1, Death = 2, Sweep = 3, Track = 4, Fire = 5 };
// [0x8903d0]
enum class TurretSub : uint8_t { Sweep = 0, Track = 1, Fire = 2 };
enum class SweepStep : uint8_t { Start = 0, Pause = 1, Wait = 2, Swing = 3 };
enum class FireStep : uint8_t { Start = 0, Shoot = 1, Reload = 2, Cool = 3 };
// The turret's parameter: its starting yaw offset
struct TurretParams {
    explicit TurretParams(const AnimatedActor& actor) : TurretParams(const_cast<AnimatedActor&>(actor)) {}
    explicit TurretParams(AnimatedActor& actor) : yawOffset(actor.params, 0) {}
    BytesField<int16_t, 4> yawOffset;
};
constexpr int16_t kYawLimit = 0x280;
constexpr int16_t kSweepStep = 0x10;
constexpr uint8_t kSweepPause = 0x2d;
constexpr int kSeeDistance = 0x640, kLoseDistance = 0x9c4, kNearDistance = 0x100;
constexpr uint8_t kVolleyBombs = 3, kBombInterval = 0xa, kVolleyCooldown = 0x1e, kDeathTicks = 0x1e;
constexpr int16_t kBombAimStep = 0x40;
constexpr uint32_t kGunPart = 2; // the gun's part (its decoded angles: the decode buffer's entry 1 + 2)
constexpr uint32_t kMuzzleBone = 2;
constexpr int16_t kMuzzleOffset[3] = {0, int16_t(0xff24), int16_t(0xff80)};
constexpr uint16_t kHurtSound = 0x8d, kStrongHurtSound = 0x8e;
constexpr int16_t kBombSize = 0x20;
constexpr uint16_t kBombBlastSize = 0x40;
constexpr int16_t kBombLaunchSound = 0xad;
constexpr uint8_t kSparkVariant = 3;
constexpr uint16_t kSparkSize = 0xc0;
constexpr uint16_t kExplosionSpread = 0x60;
constexpr uint8_t kExplosionVariant = 1;
constexpr int kHitHeight = 0xb4;
// its hit entry: a body (radius 0x80) handing the other's last move to its box
constexpr uint32_t kTurretShape = hitGroup(kHitGroupBodies) | kHitContactMotion | hitRadius(0x80);

// the row of its type, read without a null test by the exe
AnimatedActorStats requiredStats(const AnimatedActor& actor) {
    if (!hasAnimatedStats(actor.type)) notPorted(kStoryPort, "0x472100: null type row (the exe faults)");
    return animatedStats(actor.type);
}
// the gun's yaw: the turret's yaw + the yaw offset (the exe adds the sign-extended shorts)
int gunYaw(const AnimatedActor& actor, const TurretFields& turret) { return int(actor.rotation.y) + turret.yawOffset; }
void enterSub(AnimatedActor& actor, TurretSub sub) {
    actor.subState = uint8_t(sub);
    actor.restartStep();
}

// exe 0x472840: the yaw offset clamped to +-0x280: 1 at the top, -1 at the bottom, else 0
int clampYaw(TurretFields& turret) {
    const int16_t y = turret.yawOffset;
    if (y >= kYawLimit) {
        turret.yawOffset = kYawLimit;
        return 1;
    }
    if (y <= -kYawLimit) {
        turret.yawOffset = -kYawLimit;
        return -1;
    }
    return 0;
}
// exe 0x4724d0: aimed within +-0x10 of the player -> fire, 1
int aimTest(AnimatedActor& actor, const TurretFields& turret) {
    const int bearing = angleOf(player().pos, actor.pos);
    const int d = angleDiff(uint32_t(gunYaw(actor, turret)), uint32_t(bearing));
    if (uint32_t(d + 0x10) <= 0x20u) {
        enterSub(actor, TurretSub::Fire);
        return 1;
    }
    return 0;
}

// exe 0x472370: the sweep
void sweep(AnimatedActor& actor, TurretFields& turret) {
    switch (SweepStep(actor.step)) {
    case SweepStep::Start:
        actor.spin.y = kSweepStep;
        actor.step = uint8_t(SweepStep::Pause);
        [[fallthrough]];
    case SweepStep::Pause:
        turret.timer = kSweepPause;
        actor.step = uint8_t(SweepStep::Wait);
        [[fallthrough]];
    case SweepStep::Wait:
        --turret.timer;
        if (turret.timer != 0) break;
        actor.step = uint8_t(SweepStep::Swing);
        [[fallthrough]];
    case SweepStep::Swing:
        turret.yawOffset += uint16_t(actor.spin.y);
        if (clampYaw(turret)) {
            actor.step = uint8_t(SweepStep::Pause);
            actor.spin.y = int16_t(uint16_t(-int(uint16_t(actor.spin.y))));
        }
        break;
    default: break;
    }
    const int bearing = angleOf(player().pos, actor.pos);
    const int d = angleDiff(uint32_t(gunYaw(actor, turret)), uint32_t(bearing));
    if (uint32_t(d + 0x400) > 0x800u) return;
    if (planarDistance(actor.pos, player().pos) > kSeeDistance) return;
    enterSub(actor, TurretSub::Track);
    aimTest(actor, turret); // exe 0x4724d0 (its result dropped)
}
// exe 0x472440: turn toward the player
void track(AnimatedActor& actor, TurretFields& turret) {
    const int bearing = angleOf(player().pos, actor.pos);
    const int d = angleDiff(uint32_t(gunYaw(actor, turret)), uint32_t(bearing));
    if (d > 0) turret.yawOffset += 0x20u;
    if (d < 0) turret.yawOffset -= 0x20u;
    if (planarDistance(actor.pos, player().pos) > kLoseDistance) enterSub(actor, TurretSub::Sweep);
    if (clampYaw(turret)) enterSub(actor, TurretSub::Fire);
    aimTest(actor, turret); // exe 0x4724d0 (its result dropped)
}
// exe 0x472520 step 1: a bomb from the muzzle (bone 2's point (0, -0xdc, -0x80)) lobbed (pitch -0x180, gravity 0x40, at
// most 0x9c4) at the player (or, within 0x100, 0x1000 ahead along the gun's yaw), the bomb's aim above his height, and a
// hit spark at the muzzle
void fireBomb(AnimatedActor& actor, TurretFields& turret) {
    int16_t muzzlePoint[3];
    bonePointOffset(actor, kMuzzleBone, kMuzzleOffset, muzzlePoint); // exe 0x5e1970
    const int32_t muzzle[3] = {int32_t(uint32_t(int32_t(muzzlePoint[0])) << 16),
                               int32_t(uint32_t(int32_t(muzzlePoint[1])) << 16),
                               int32_t(uint32_t(int32_t(muzzlePoint[2])) << 16)};
    // the exe's stack target: the low half of its y is never written (0 here; the lob reads the integer parts)
    int32_t target[3];
    const Player& p = player();
    if (planarDistance(actor.pos, p.pos) > kNearDistance) {
        target[0] = p.pos.x;
        target[2] = p.pos.z;
    } else {
        const int yaw = gunYaw(actor, turret) & 0xfff;
        target[0] = int32_t((uint32_t(int32_t(fixedCos(yaw))) << 12) + uint32_t(actor.pos.x));
        target[2] = int32_t((uint32_t(int32_t(fixedSin(yaw))) << 12) + uint32_t(actor.pos.z));
    }
    target[1] = int32_t(uint32_t(uint16_t(uint16_t(turret.bombHeight) + uint16_t(p.pos.y.whole))) << 16);
    if (const uint32_t slot = allocActor40(kActorStageClass)) { // exe 0x5e1280
        ProjectileActor& bomb = projectileActorAt(slot);
        LobbedShotParams shot(projectileActorAt(slot));
        bomb.flags = kActorLive;
        bomb.type = kLobbedShotStageType;
        bomb.subtype = 0;
        bomb.variant = 0;
        bomb.pos.x = muzzle[0];
        bomb.pos.y = muzzle[1];
        bomb.pos.z = muzzle[2];
        int16_t vy, vz;
        lobVelocity(muzzle, target, -0x180, 0x40, 0x9c4, vy, vz); // exe 0x5e3f50
        bomb.velocity.x = 0;
        bomb.velocity.y = vy;
        bomb.velocity.z = int16_t(-vz);
        shot.blastSize = kBombBlastSize;
        bomb.rotation.z = 0;
        bomb.rotation.x = 0;
        shot.size = kBombSize;
        bomb.acceleration.y = 0x40;
        bomb.rotation.y = int16_t(uint16_t(turret.yawOffset) + uint16_t(actor.rotation.y));
        shot.damage = int16_t(requiredStats(actor).strongAttack);
        shot.launchSound = kBombLaunchSound;
        if (const uint32_t sparkSlot = allocActorA0(0)) { // exe 0x5e13b0
            EffectActor& spark = effectActorAt(sparkSlot);
            spark.flags = kActorLiveDrawn;
            spark.loopFlags |= kActorSweepGroup2;
            spark.type = typeByte(GlobalEffectType::HitSpark);
            spark.variant = kSparkVariant;
            spark.pos.x = muzzlePoint[0];
            spark.pos.y = muzzlePoint[1];
            spark.pos.z = muzzlePoint[2];
            classState<HitSparkFields>(effectActorAt(sparkSlot)).size = kSparkSize;
        }
    }
    actor.step = uint8_t(FireStep::Reload);
    turret.timer = kBombInterval;
}
// exe 0x472520: the volley
void fire(AnimatedActor& actor, TurretFields& turret) {
    switch (FireStep(actor.step)) {
    case FireStep::Start:
        actor.stepCounter = kVolleyBombs;
        turret.bombHeight = kBombAimStep;
        actor.step = uint8_t(FireStep::Shoot);
        [[fallthrough]];
    case FireStep::Shoot: fireBomb(actor, turret); break;
    case FireStep::Reload:
        --turret.timer;
        if (turret.timer != 0) break;
        actor.step = uint8_t(FireStep::Shoot);
        turret.bombHeight -= uint16_t(kBombAimStep);
        --actor.stepCounter;
        if (actor.stepCounter != 0) break;
        actor.step = uint8_t(FireStep::Cool);
        turret.timer = kVolleyCooldown;
        break;
    case FireStep::Cool:
        --turret.timer;
        if (turret.timer != 0) break;
        enterSub(actor, TurretSub::Track);
        break;
    default: break;
    }
    if (planarDistance(actor.pos, player().pos) > kLoseDistance) enterSub(actor, TurretSub::Sweep);
}
void runSub(AnimatedActor& actor, TurretFields& turret) {
    switch (TurretSub(actor.subState)) {
    case TurretSub::Sweep: sweep(actor, turret); return;
    case TurretSub::Track: track(actor, turret); return;
    case TurretSub::Fire: fire(actor, turret); return;
    default: notPortedAt(kStoryPort, "0x472100 sub-state", actor.subState);
    }
}

// exe 0x472240
void bind(AnimatedActor& actor, TurretFields& turret) {
    if (!bindActorModel(actor, 0)) { // exe 0x5e0ba0
        freeActor(actor);            // exe 0x5e1490
        return;
    }
    actor.flags |= kActorDrawn;
    actor.anim = 0;
    actor.prevAnim = kNoAnimShown;
    const uint16_t hp = hasAnimatedStats(actor.type) ? uint16_t(animatedStats(actor.type).hitPoints) : uint16_t(0);
    actor.velocity.clearWords();
    actor.spin.setXY(0, 0);
    actor.rotation.z = 0;
    actor.rotation.x = 0;
    actor.interactionBox = nullptr;
    actor.interaction = 0;
    actor.airborne = 0;
    actor.onPlatform = 0;
    actor.maxHitPoints = int16_t(hp);
    actor.hitPoints = int16_t(hp);
    actor.collisionBox.pointAt(TableId::St3bBombTurretBox);
    actor.clearClassBlock(); // exe 0x40dc40
    turret.yawOffset = TurretParams(actor).yawOffset;
    actor.enterState(uint8_t(TurretState::Run));
}
// exe 0x4727a0: a death explosion 0xb4 up; 0x1e ticks later the drops, freed
void death(AnimatedActor& actor, TurretFields& turret) {
    if (actor.subState == 0) {
        turret.timer = kDeathTicks;
        if (const uint32_t slot = allocActor60(0)) { // exe 0x5e1300
            StaticActor& explosion = staticActorAt(slot);
            explosion.flags = kActorLive;
            explosion.type = typeByte(GlobalStaticType::Explosion);
            explosion.variant = kExplosionVariant;
            explosion.loopFlags |= kActorSweepGroup2;
            explosion.pos.x.whole = actor.pos.x.whole;
            explosion.pos.y.whole = int16_t(uint16_t(actor.pos.y.whole) - uint16_t(kHitHeight));
            explosion.pos.z.whole = actor.pos.z.whole;
            ExplosionParams params(actorHeaderAt(slot));
            params.spread = kExplosionSpread;
            params.flashes = turret.timer;
        }
        actor.subState = 1;
    }
    --turret.timer;
    if (turret.timer != 0) return;
    spawnDrops(actor.pos, requiredStats(actor).dropTable); // exe 0x5f1270
    freeActor(actor);                                       // exe 0x5e1490
}
// exe 0x4722f0
void run(AnimatedActor& actor, TurretFields& turret) {
    const uint32_t hit = turret.hitBox.result;
    const uint32_t damage = hit & kHitDamageMask;
    if (damage) {
        actor.tint = kTintFlash;
        soundPlayAtPos((hit & kHitAttackStrongPush) ? kStrongHurtSound : kHurtSound, actor.pos);
        if (hitPointsAdd(actor, -int(damage))) { // exe 0x5e52f0
            actor.enterState(uint8_t(TurretState::Death));
            death(actor, turret);
            return;
        }
    }
    runSub(actor, turret);
}

// exe 0x472100
void bombTurret(AnimatedActor& actor) {
    TurretFields& turret = classState<TurretFields>(actor);
    actor.prevAnim = actor.anim;
    actor.prevPos = actor.pos;
    actor.tint = kTintNeutral;
    actor.prevRotation = actor.rotation;
    actor.savePreviousContact();
    switch (TurretState(actor.state)) {
    case TurretState::Bind: bind(actor, turret); break;
    case TurretState::Run: run(actor, turret); break;
    case TurretState::Death: death(actor, turret); break;
    case TurretState::Sweep: sweep(actor, turret); break;
    case TurretState::Track: track(actor, turret); break;
    case TurretState::Fire: fire(actor, turret); break;
    default: notPortedAt(kStoryPort, "0x472100 state", actor.state);
    }
    if (!(actor.flags & kActorLive)) return;
    if (actor.anim == actor.prevAnim) animStep(actor); // exe 0x5e1ea0
    else animSet(actor, actor.anim, 0);                // exe 0x5e1c50
    decodedFrame().entries[1 + kGunPart].y += uint16_t(turret.yawOffset); // the yaw offset onto the gun
    animPose(actor);                                                       // exe 0x5e2d90
    turret.hitBox.result = 0;
    turret.hitBox.previousX = actor.prevPos.x.whole;
    turret.hitBox.previousZ = actor.prevPos.z.whole;
    const int16_t pos[3] = {actor.pos.x.whole, int16_t(actor.pos.y.whole - kHitHeight), actor.pos.z.whole};
    hitListSubmit(pos, turret.hitBox, hitOwner(actor.slot) | kTurretShape,
                  requiredStats(actor).attack | kHitAttackSolid); // exe 0x5e4120
    // exe 0x5f20f0(a, 0x40, 0x100, 0): the shadow, render output
}

} // namespace

void bombTurretUpdate(const PlayerEnv&, uint32_t actor) { bombTurret(animatedActorAt(actor)); }

} // namespace od::port
