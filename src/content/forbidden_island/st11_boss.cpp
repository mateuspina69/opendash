// ST11 (story 1, sub 0): the fight actor exe 0x4b3b10 (pool 0x20 type 0x3b sub 0), one routine per exe routine
// (addresses in the comments); the names are hypotheses from the observed behaviour. Script 0x2a
// (content/forbidden_island/st11_script2a) spawns it through the records 0x8a4a58 (slot 0); the room handler 0x57e180
// (content/forbidden_island/island_visit) waits for it to go.
//
// The update (exe 0x4b3b10): the neutral tint, the previous pose; the states [0x8945cc] (bind 0x4b3be0, active
// 0x4b3cf0, free 0x468fa0); alive, the animation, the hit capsule 0x4b46f0 and the player's planar distance. The
// active state takes the damage (0x4b4820) while hittable, runs its sub-state [0x8945d8], then the wall / ground pass
// 0x4b4940 and the step sound of the animation's cue. Sub-states: wait 0x4b3d50 (for the player within 0x1000, rises:
// animations 0 / 4), approach 0x4b3dc0 (faces the player, walks at 0x400: a wall -> turn, within 0xc00 -> circle,
// beyond 0xfff -> back off), circle 0x4b3e60 ([0x8945fc]: the side hop then circling while on screen for 0x78 ticks
// (300 at most) 0x4b3e80, the strafe arc 0x4b4100 (no entry in this class), the recovery 0x4b4170), turn 0x4b41e0
// (0x40 a tick for 0x10 ticks), knockback 0x4b4220 (from the hit's angle: animations 0x13 / 0x14 / 0x15), rise
// 0x4b4380 (no entry in this class), leap 0x4b43f0 (two side hops then a leap at the player: exe 0x5e3ee0), back off
// 0x4b45b0 (until within 0x1000: animations 0x17 / 0x18 / 0x19), dying 0x4b4660 (the defeat count, the explosion
// 0x47a480, the drops of exe 0x5f1270 by the row's drop table, 0xf ticks then freed). The shadow exe 0x5f20f0 is
// render output and left out.
#include "content/forbidden_island/st11_boss.hpp"
#include "content/forbidden_island/st11_boss_state.hpp"

#include "game/actor_animation.hpp"
#include "game/actor_model.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actor_stats.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/collision.hpp"
#include "game/cutscene_actor.hpp"
#include "game/distances.hpp"
#include "game/explosions.hpp"
#include "game/global_class_types.hpp"
#include "game/hit_list.hpp"
#include "game/hit_points.hpp"
#include "game/hud.hpp"
#include "game/pickups.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/story.hpp"
#include "game/visibility.hpp"

namespace od::port {

using namespace st11_boss_state;

namespace {

constexpr uint8_t kFighterDying = 0x02;
constexpr uint8_t kFighterDyingMask = 0x06; // dying, or the bit above it (its setter is not traced)
constexpr uint8_t kFighterHittable = 0x10;
constexpr uint8_t kFighterLanded = 0x40;
constexpr uint8_t kFighterArmed = 0x70;      // hittable, landed and the bit between them (its reader is not traced)
constexpr uint8_t kFighterKnockedMask = 0x8f; // the knockback clears the three

enum class FighterState : uint8_t { Bind = 0, Active = 1, Free = 2 };
enum class FighterSub : uint8_t {
    Wait = 0, Approach = 1, Circle = 2, Turn = 3, Knockback = 4, Rise = 5, Leap = 6, BackOff = 7, Dying = 8
};
enum class CircleStep : uint8_t { Circle = 0, Strafe = 1, Recover = 2 };
enum class HopPhase : uint8_t { TakeOff = 0, Air = 1, Landed = 2 };

constexpr uint16_t kSoundStep = 0x16b, kSoundHop = 0x16c, kSoundLand = 0x170, kSoundLeap = 0x16e;
constexpr uint16_t kSoundKnocked = 0x16f, kSoundKnockLand = 0x16d, kSoundHit = 0x8d, kSoundHeavyHit = 0x8e;
constexpr int16_t kHopSpeedZ = -0x400, kHopGravity = 0x100;

// the class row of the spawn byte (no null check, as the exe outside the bind)
AnimatedActorStats row(const AnimatedActor& a) { return animatedStats(a.type, a.spawnByte); }
// a sub-state at its first step (the exe's word store of the step bytes)
void enterSub(AnimatedActor& a, FighterSub s) {
    a.subState = uint8_t(s);
    a.restartStep();
}
void turnBy(AnimatedActor& a, int d) { a.rotation.y += d; }
int playerAngle(const AnimatedActor& a) { return angleOf(player().pos, a.pos); }
void moveForward(AnimatedActor& a) { moveAlongYaw(a.pos, a.rotation.y, 0, 0, a.velocity.z); }

// exe 0x47a480: a death explosion (global static type 8) 0x60 above the actor, spread 0x30, 0x3c flashes
void spawnExplosion(const AnimatedActor& a) {
    const uint32_t address = allocActor60(0); // exe 0x5e1300
    if (!address) return;
    StaticActor& e = staticActorAt(address);
    ExplosionParams params(e);
    e.flags = kActorLive;
    e.type = typeByte(GlobalStaticType::Explosion);
    e.loopFlags |= kActorSweepGroup2;
    e.variant = 0;
    e.pos.x.whole = a.pos.x.whole;
    e.pos.y.whole = int16_t(uint16_t(a.pos.y.whole) - 0x60u);
    e.pos.z.whole = a.pos.z.whole;
    params.spread = 0x30;
    params.flashes = 0x3c;
}

// exe 0x4b46f0: the hit capsule (box +0x14c) between the points 0x40 ahead / behind at y - 0x40, radius 0x10 each;
// harmless once dead
void submitHitCapsule(const AnimatedActor& a, FighterFields& fields) {
    const AnimatedActorStats r = row(a);
    fields.hit.previousX = a.prevPos.x.whole;
    fields.hit.previousZ = a.prevPos.z.whole;
    fields.hit.result = 0;
    uint32_t hi, immunity, compare;
    uint32_t attack;
    if (fields.dead == 0) {
        hi = 0xa0000;
        immunity = 0x80000000u;
        attack = r.attack;
        compare = kHitCompareHeights;
    } else {
        hi = 0x10000;
        immunity = 0xff000000u;
        attack = 0;
        compare = 0;
    }
    const int16_t ang = a.rotation.y;
    int16_t dx, dz;
    angleVector(ang, 0x40, dx, dz); // exe 0x5e40c0
    const int16_t front[3] = {int16_t(dx + a.pos.x.whole), int16_t(a.pos.y.whole - 0x40), int16_t(dz + a.pos.z.whole)};
    angleVector(ang, -0x40, dx, dz);
    const int16_t back[3] = {int16_t(dx + a.pos.x.whole), int16_t(a.pos.y.whole - 0x40), int16_t(dz + a.pos.z.whole)};
    const uint32_t ownerShape = ((uint32_t(a.slot) << 0xf | uint32_t(r.hitHeight) | 0x2048u) << 9) | compare;
    const uint32_t attackWord = uint32_t(int32_t(int16_t(attack))) | immunity | hi;
    hitListSubmitCapsule(front, 0x10, back, 0x10, fields.hit, ownerShape, attackWord); // exe 0x5e4190
}

// exe 0x4b4820: the damage of the box (a shot's attack kinds 0x1c0000; a contact from below while hittable): a strong
// enough hit knocks it back, the last one starts the dying
int takeDamage(AnimatedActor& a, FighterFields& fields) {
    const uint32_t v = fields.hit.result;
    const AnimatedActorStats r = row(a);
    if (v & (kHitAttackByPlayer | kHitAttackThrown | kHitAttackStrong)) {
        a.tint = kTintFlash;
        if (hitPointsAdd(a, -int(v & kHitDamageMask)) != 0) { // exe 0x5e52f0
            if ((fields.flags & kFighterDying) == 0) {
                enterSub(a, FighterSub::Dying);
                fields.flags |= kFighterDying;
                return -1;
            }
        } else {
            if (uint32_t(r.bodyAttack) <= (v & kHitDamageMask)) {
                soundPlayAtPos(kSoundHeavyHit, a.pos);
                fields.knockbackAngle = uint16_t(fields.hit.contact.x);
                enterSub(a, FighterSub::Knockback);
                return 0;
            }
            soundPlayAtPos(kSoundHit, a.pos);
        }
    }
    if ((fields.flags & kFighterHittable) == 0) return 0;
    if ((v & (kHitLevelAbove | kHitLevelWithin | kHitLevelBelow)) == 0) return 0;
    if ((v & kHitLevelBelow) == 0) return 0;
    if (hitPointsAdd(a, -int(v & kHitDamageMask)) != 0) {
        if (fields.flags & kFighterDyingMask) return 0;
        enterSub(a, FighterSub::Dying);
        fields.flags |= kFighterDying;
        return 0;
    }
    enterSub(a, FighterSub::Knockback);
    return 0;
}

// exe 0x4b4940: the walls (exe 0x40f570, the push-out exe 0x410960, blocked: back to the previous position) and the
// ground
void collide(const PlayerEnv& env, AnimatedActor& a, FighterFields& fields) {
    int32_t pushed = 0;
    fields.wall = 0;
    if (int32_t(actorTestMove(env, a)) <= -1) {
        pushed = int32_t(actorPushOut(env, a));
        if (pushed <= -1) a.pos = a.prevPos;
        if (pushed & kWallOnSides) fields.wall = 1;
    }
    const uint16_t g = uint16_t(actorGround(env, a)); // exe 0x40f190
    if (a.airborne) {
        if ((pushed & kWallOnY) == 0) return;
        a.airborne = 0;
    }
    a.pos.y.whole = int16_t(g);
}

// exe 0x4b49c0(a, alt): one hop along the yaw (the velocity's y / z, the gravity); 1 once landed and the animation
// ended
int hop(AnimatedActor& a, FighterFields& fields, bool alt) {
    switch (HopPhase(fields.hopPhase)) {
    case HopPhase::TakeOff:
        a.anim = alt ? 0xf : 0xc;
        soundPlayAtPos(kSoundHop, a.pos);
        a.airborne = kAirborneFalling;
        ++fields.hopPhase;
        fields.hopApex = 0;
        fields.flags &= uint8_t(~kFighterLanded);
        moveAlongYaw(a.pos, a.rotation.y, 0, a.velocity.y, a.velocity.z);
        return 0;
    case HopPhase::Air:
        if (fields.hopApex == 0 && a.velocity.y >= 1) {
            a.anim = alt ? 0x10 : 0xd;
            ++fields.hopApex;
        }
        if (a.airborne == 0) {
            a.anim = 0xe;
            soundPlayAtPos(kSoundLand, a.pos);
            fields.flags |= kFighterLanded;
            ++fields.hopPhase;
            return 0;
        }
        moveAlongYaw(a.pos, a.rotation.y, 0, a.velocity.y, a.velocity.z);
        a.velocity.y += a.acceleration.y;
        return 0;
    case HopPhase::Landed: return a.animEnded() ? 1 : 0;
    default: return 0;
    }
}

// exe 0x4b3be0: model variant 0 (none: freed), animation 1 (waiting: the parameter set) or 3, the hit points of the
// class row, the box 0x8945c0, the class block cleared, armed, the boss gauge (exe 0x415d30); waiting or approaching
void bind(AnimatedActor& a, FighterFields& fields) {
    if (!bindActorModel(a, 0)) { // exe 0x5e0ba0
        freeActor(a);            // exe 0x5e1490
        return;
    }
    a.flags |= kActorDrawn;
    a.anim = a.params[0] ? 1 : 3;
    a.prevAnim = kNoAnimShown;
    const int16_t hp = hasAnimatedStats(a.type) ? int16_t(animatedStats(a.type, a.spawnByte).hitPoints) : 0;
    a.maxHitPoints = hp;
    a.hitPoints = hp;
    a.velocity.setXY(0, 0);
    a.velocity.setZSpare(0, 0);
    a.rotation.z = 0;
    a.rotation.x = 0;
    a.collisionBox.pointAt(TableId::St11BossBox);
    a.interactionBox = nullptr;
    a.interaction = 0;
    a.airborne = 0;
    a.onPlatform = 0;
    a.platform = nullptr;
    a.actorHit = 0;
    a.clearClassBlock(); // exe 0x40dc40: +0x14c..+0x16b
    fields.hopPhase = 0;
    fields.hopApex = 0;
    fields.distance = planarDistance(a.pos, player().pos);
    fields.dead = 0;
    fields.knockbackAngle = 0;
    fields.flags |= kFighterArmed;
    hudAddBossGauge(a, 1, 1, 0xff); // exe 0x415d30
    a.state = uint8_t(FighterState::Active);
    if (a.params[0] == 0) {
        a.clearStepBytes();
        a.subState = uint8_t(FighterSub::Approach);
        return;
    }
    a.subState = uint8_t(FighterSub::Wait);
    a.clearStepBytes();
}

// exe 0x4b3d50: waits for the player within 0x1000, then rises (animations 0, 4) and approaches
void wait(AnimatedActor& a, const FighterFields& fields) {
    switch (a.step) {
    case 0:
        if (fields.distance <= 0x1000) {
            a.anim = 0;
            a.step = 1;
        }
        return;
    case 1:
        if (a.animEnded()) {
            a.anim = 4;
            a.step = 2;
        }
        return;
    case 2:
        if (a.animEnded()) {
            a.subState = uint8_t(FighterSub::Approach);
            a.anim = 3;
            a.restartStep();
        }
        return;
    default: return;
    }
}

// exe 0x4b3dc0: faces the player and walks at 0x400; at the animation's end a wall -> turn, within 0xc00 -> circle,
// beyond 0xfff -> back off
void approach(AnimatedActor& a, const FighterFields& fields) {
    const int ang = playerAngle(a);
    const int d = angleDiff(a.rotation.y, ang);
    const uint16_t yaw = uint16_t((uint32_t(uint16_t(a.rotation.y)) + uint32_t(d)) & 0xfff);
    a.rotation.y = int16_t(yaw);
    a.velocity.z = kHopSpeedZ;
    moveAlongYaw(a.pos, yaw, 0, 0, -0x400);
    if (!a.animEnded()) return;
    if (fields.wall & 1) {
        a.subState = uint8_t(FighterSub::Turn);
        a.step = 0;
        return;
    }
    if (fields.distance <= 0xc00) {
        a.subState = uint8_t(FighterSub::Circle);
        a.clearStepBytes();
        return;
    }
    if (fields.distance <= 0xfff) return;
    enterSub(a, FighterSub::BackOff);
}

// exe 0x4b3e80: the side hop (a quarter turn away from the player's side), then circling (animations 9 / 6, 7 / 10)
// while on screen for 0x78 ticks (300 at most): close -> leap, the time out -> back off; off screen it keeps turning
void circle(AnimatedActor& a, FighterFields& fields) {
    const uint8_t phase = a.stepCounter;
    switch (phase) {
    case 0: {
        const int ang = playerAngle(a);
        if (angleDiff(a.rotation.y, ang) >= 1) {
            fields.hopSide = 0;
            turnBy(a, -0x400);
        } else {
            fields.hopSide = 1;
            turnBy(a, 0x400);
        }
        a.velocity.x = 0;
        a.velocity.y = -0x280;
        a.velocity.z = kHopSpeedZ;
        a.acceleration.y = kHopGravity;
        a.stepCounter = uint8_t(phase + 1u);
        fields.hopPhase = 0;
        return;
    }
    case 1:
        if (!hop(a, fields, false)) return;
        (void)angleDiff(a.rotation.y, playerAngle(a)); // computed and unused in the exe
        a.anim = fields.hopSide ? 9 : 6;
        a.velocity.z = kHopSpeedZ;
        a.stepCounter = uint8_t(phase + 1u);
        return;
    case 2:
        moveForward(a);
        turnBy(a, fields.hopSide ? -0x20 : 0x20);
        if (!a.animEnded()) return;
        a.anim = fields.hopSide ? 7 : 10;
        fields.timer = 0x78;
        fields.timeout = 300;
        a.stepCounter = uint8_t(phase + 1u);
        return;
    case 3:
        moveForward(a);
        turnBy(a, fields.hopSide ? -0x20 : 0x20);
        if (pointVisibleOrNear(a.pos, 0) != 0) { // exe 0x5e59a0(pos, 0)
            --fields.timer;
            if (fields.timer != 0) return;
            ++a.stepCounter;
            return;
        }
        if (fields.distance <= 0x800) {
            enterSub(a, FighterSub::Leap);
            fields.timer = 0;
            fields.timeout = 0;
        }
        --fields.timeout;
        if (fields.timeout != 0) return;
        enterSub(a, FighterSub::BackOff);
        fields.timer = 0;
        fields.timeout = 0;
        return;
    case 4:
        moveForward(a);
        turnBy(a, fields.hopSide ? -8 : 8);
        if (pointVisibleOrNear(a.pos, 0) != 0) return;
        fields.timer = 0x78;
        fields.timeout = 300;
        a.stepCounter = 3;
        return;
    default: return;
    }
}

// exe 0x4b4100: an arc along the strafe yaw (the turn rates of the spin) while 0x800..0xfff away, then the recovery
void strafe(AnimatedActor& a, FighterFields& fields) {
    if (a.stepCounter == 0) {
        a.stepCounter = 1;
        a.spin.y = a.spin.y > 0 ? int16_t(0x10) : int16_t(-0x10);
    }
    turnBy(a, a.spin.y);
    fields.strafeYaw += uint16_t(a.spin.x);
    moveAlongYaw(a.pos, fields.strafeYaw, 0, 0, a.velocity.z);
    if (fields.distance >= 0x801 && fields.distance <= 0xfff) return;
    a.step = uint8_t(CircleStep::Recover);
    a.stepCounter = 0;
}

// exe 0x4b4170: the recovery's animation (0xb / 8 by the turn), then leap (close) or back off
void recover(AnimatedActor& a, const FighterFields& fields) {
    const uint8_t phase = a.stepCounter;
    if (phase == 0) {
        a.anim = a.spin.y >= 1 ? 0xb : 8;
        a.stepCounter = 1;
        return;
    }
    if (phase == 1 && a.animEnded()) enterSub(a, fields.distance <= 0x800 ? FighterSub::Leap : FighterSub::BackOff);
}

// exe 0x4b3e60: [0x8945fc + step * 4]
void circleState(AnimatedActor& a, FighterFields& fields) {
    switch (CircleStep(a.step)) {
    case CircleStep::Circle: circle(a, fields); return;
    case CircleStep::Strafe: strafe(a, fields); return;
    case CircleStep::Recover: recover(a, fields); return;
    default: notPortedAt(kStoryPort, "0x4b3e60 sub-state (table 0x8945fc)", a.step);
    }
}

// exe 0x4b41e0: turns 0x40 a tick for 0x10 ticks, then approaches
void turnAround(AnimatedActor& a, FighterFields& fields) {
    if (a.step == 0) {
        fields.timer = 0x10;
        ++a.step;
    }
    turnBy(a, 0x40);
    --fields.timer;
    if (fields.timer != 0) return;
    a.subState = uint8_t(FighterSub::Approach);
    a.step = 0;
}

// exe 0x4b4220: knocked back from the hit's angle (animation 0x13, a short wait, the flight 0x14, the landing 0x15),
// then armed again and approaching
void knockback(AnimatedActor& a, FighterFields& fields) {
    const uint8_t phase = a.step;
    switch (phase) {
    case 0:
        a.anim = 0x13;
        soundPlayAtPos(kSoundKnocked, a.pos);
        fields.flags &= kFighterKnockedMask;
        a.step = uint8_t(phase + 1u);
        fields.timer = 2;
        return;
    case 1:
        --fields.timer;
        if (fields.timer != 0) return;
        a.velocity.y = -0x1e0;
        a.velocity.z = 0x100;
        a.acceleration.y = 0x40;
        a.rotation.y = int16_t(uint16_t(fields.knockbackAngle + 0x800u));
        a.airborne = kAirborneFalling;
        ++a.step;
        integrateVelocity(a, 0, -0x1e0, 0x100);
        return;
    case 2:
        integrateVelocity(a, 0, a.velocity.y, a.velocity.z);
        a.velocity.y += a.acceleration.y;
        if (a.airborne) return;
        a.anim = 0x14;
        soundPlayAtPos(kSoundKnockLand, a.pos);
        ++a.step;
        return;
    case 3:
        if (!a.animEnded()) return;
        a.anim = 0x15;
        a.step = uint8_t(phase + 1u);
        return;
    case 4:
        if (!a.animEnded()) return;
        fields.flags |= kFighterArmed;
        a.subState = uint8_t(FighterSub::Approach);
        a.anim = 3;
        a.restartStep();
        return;
    default: return;
    }
}

// exe 0x4b4380: rises (0x40 a tick up to 0x200), back on the ground the circle's recovery
void rise(AnimatedActor& a) {
    const uint8_t phase = a.step;
    if (phase == 0) {
        a.velocity.y = 0;
        a.step = uint8_t(phase + 1u);
    } else if (phase == 1) {
        a.velocity.y += 0x40u;
        if (a.velocity.y > 0x200) a.step = uint8_t(phase + 1u);
    } else if (phase == 2 && a.airborne == 0) {
        a.step = 0;
        a.subState = uint8_t(FighterSub::Circle);
        a.stepCounter = uint8_t(CircleStep::Recover);
    }
    moveAlongYaw(a.pos, 0, 0, a.velocity.y, a.velocity.z);
}

// the speeds of the leap's side hops
void sideHopSpeeds(AnimatedActor& a, FighterFields& fields) {
    a.velocity.x = 0;
    a.velocity.y = -0xa0;
    a.velocity.z = kHopSpeedZ;
    a.acceleration.y = kHopGravity;
    ++a.step;
    fields.hopPhase = 0;
}

// exe 0x4b43f0: two side hops around the player, then the leap at it (exe 0x5e3ee0's launch speed), a run, circling
void leap(AnimatedActor& a, FighterFields& fields) {
    switch (a.step) {
    case 0:
        a.rotation.y = int16_t(uint32_t(playerAngle(a) - 0x120));
        sideHopSpeeds(a, fields);
        hop(a, fields, false);
        return;
    case 1:
        if (!hop(a, fields, false)) return;
        a.rotation.y = int16_t(uint32_t(playerAngle(a) + 0x120));
        sideHopSpeeds(a, fields);
        return;
    case 2: {
        if (!hop(a, fields, false)) return;
        soundPlayAtPos(kSoundLeap, a.pos);
        a.rotation.y = int16_t(uint32_t(playerAngle(a)));
        a.velocity.x = 0;
        a.velocity.z = -0x800;
        a.acceleration.y = kHopGravity;
        const uint16_t v = uint16_t(launchSpeed(a.pos, player().pos, 0x800, 0x100, 0x400)); // exe 0x5e3ee0
        a.velocity.y = int16_t(v);
        if (v <= 63999) a.velocity.y = -0x100;
        ++a.step;
        fields.hopPhase = 0;
        return;
    }
    case 3:
        if (!hop(a, fields, true)) return;
        a.anim = 0x20;
        a.velocity.z = kHopSpeedZ;
        ++a.step;
        fields.timer = 0x1e;
        fields.hopPhase = 0;
        return;
    case 4:
        moveForward(a);
        --fields.timer;
        if (fields.timer != 0) return;
        enterSub(a, FighterSub::Circle);
        return;
    default: return;
    }
}

// exe 0x4b45b0: backs off (animations 0x17 / 0x18) until the player is within 0x1000, then 0x19 and approaches
void backOff(AnimatedActor& a, const FighterFields& fields) {
    const uint8_t phase = a.step;
    switch (phase) {
    case 0:
        a.anim = 0x17;
        a.velocity.z = 4;
        a.step = uint8_t(phase + 1u);
        return;
    case 1:
        moveAlongYaw(a.pos, 0, 0, 0, a.velocity.z);
        if (!a.animEnded()) return;
        a.anim = 0x18;
        ++a.step;
        return;
    case 2:
        if (fields.distance >= 0x1000) return;
        a.anim = 0x19;
        a.step = uint8_t(phase + 1u);
        return;
    case 3:
        if (!a.animEnded()) return;
        a.subState = uint8_t(FighterSub::Approach);
        a.anim = 3;
        a.restartStep();
        return;
    default: return;
    }
}

// exe 0x4b4660: the defeat counted, hidden, the explosion, the drops, the capsule harmless; 0xf ticks later freed
void dying(AnimatedActor& a, FighterFields& fields) {
    if (a.step == 0) {
        countDefeat();
        a.flags &= uint8_t(~kActorDrawn);
        spawnExplosion(a); // exe 0x47a480
        spawnDrops(a.pos, row(a).dropTable); // exe 0x5f1270
        fields.dead = 1;
        fields.timer = 0xf;
        ++a.step;
        return;
    }
    if (a.step == 1) {
        --fields.timer;
        if (fields.timer == 0) {
            a.enterStateBytes(uint8_t(FighterState::Free));
            a.clearStepBytes();
        }
    }
}

// exe 0x4b3cf0: the damage while hittable, the sub-state [0x8945d8], the walls / ground, the step sound
void active(const PlayerEnv& env, AnimatedActor& a, FighterFields& fields) {
    if (a.hitPoints >= 0 && (fields.flags & kFighterHittable)) takeDamage(a, fields);
    switch (FighterSub(a.subState)) {
    case FighterSub::Wait: wait(a, fields); break;
    case FighterSub::Approach: approach(a, fields); break;
    case FighterSub::Circle: circleState(a, fields); break;
    case FighterSub::Turn: turnAround(a, fields); break;
    case FighterSub::Knockback: knockback(a, fields); break;
    case FighterSub::Rise: rise(a); break;
    case FighterSub::Leap: leap(a, fields); break;
    case FighterSub::BackOff: backOff(a, fields); break;
    case FighterSub::Dying: dying(a, fields); break;
    default: notPortedAt(kStoryPort, "0x4b3cf0 sub-state (table 0x8945d8)", a.subState);
    }
    collide(env, a, fields);
    if (a.animKey.events) soundPlayAtPos(kSoundStep, a.pos);
}

// exe 0x4b3b10
void fighter(const PlayerEnv& env, AnimatedActor& a) {
    FighterFields& fields = classState<FighterFields>(a);
    a.prevAnim = a.anim;
    fields.tintLow = uint8_t(kTintNeutral);
    fields.tintHigh = uint8_t(kTintNeutral >> 8);
    a.savePreviousPose();
    a.savePreviousContact();
    switch (FighterState(a.state)) {
    case FighterState::Bind: bind(a, fields); break;
    case FighterState::Active: active(env, a, fields); break;
    case FighterState::Free: freeActor(a); break; // exe 0x468fa0 -> 0x5e1490
    default: notPortedAt(kStoryPort, "0x4b3b10 state (table 0x8945cc)", a.state);
    }
    if ((a.flags & kActorLive) == 0) return;
    animateAsked(a, 0); // exe 0x5e1c30 / 0x5e1e80
    submitHitCapsule(a, fields); // exe 0x4b46f0
    fields.distance = planarDistance(a.pos, player().pos);
    // drawn: the shadow exe 0x5f20f0(a, 0x80, 0xc0, 0) -- render output, left out
}

} // namespace

void st11FighterUpdate(const PlayerEnv& env, uint32_t actor) { fighter(env, animatedActorAt(actor)); }

} // namespace od::port
