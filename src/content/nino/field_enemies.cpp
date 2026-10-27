// The field enemies of Nino island: the harmless critter (ST15 / ST16, pool 0x20 type 0x32 sub 0) and the ST16 box
// carrier (pool 0x20 type 0x49 sub 0; respawned by the ST16 e0 spawner 0x41d320, content/nino/field_spawners), one
// routine per exe routine. Names are hypotheses from the observed behaviour; the shadows (exe 0x5f20f0) are render
// output and left out.
//
// ---- the critter 0x4a0f70 (type 0x32 sub 0; ST15 / ST16) -----------------------------------------------------------
// Its head: the previous animation / pose, states [0x893b5c] (0 bind 0x4a11c0, 1 main 0x4a1270, 2 carried 0x4a1780);
// then (live) the removal tests (its distance to the player beyond the far distance * 4 or in view while off the room
// (the move test's water bit); below the far distance * 2; in view / near (exe 0x5e59a0 0x200) for 0x1e ticks) -> gone
// (carried: the balance - its row's byte 8 * 0x1000, a message trigger e0 type 0x1b {0x16, 0x7f, -1}) and freed; the
// animation (its start key, then cleared), part 1's tilt (the decoded frame += its two tilts), the pose; its hit entry at
// y - 100 unless it has none (thrown: its row's attack | by-player; else harmless); a solid + a mover (carried: the
// mover only).
// Main: exe 0x4a1ac0 the hits (no damage: sound 0x8d, the balance - its row's byte 9 (byte 10 on a strong push), a
// reaction (sub-state 4) on a strong hit or after 0x20 accumulated), the sub-states [0x893b68] (0 0x4a12e0 drop in, 1
// 0x4a1390 wander, 2 0x4a14a0 turn off a wall, 3 0x4a1560 fall, 4 0x4a15c0 knocked, 5 0x4a1700 slide, 6 0x4a1750 get
// up), exe 0x4a1980 the collision (the ground lost -> 3 unless it keeps on), lifted -> state 2. Carried: 0x4a17a0 in the
// hands, 0x4a1880 flying.
// Dust: exe 0x4a1b90 (the stage's static type 3 = 0x592050, variant 8, life 4).
// ---- the box carrier 0x4d1700 (type 0x49 sub 0; ST16) ----------------------------------------------------------------
// Its head: the previous animation / pose, states [0x8953a8] (0 bind 0x4d1970, 1 main 0x4d1b10, 2 dying 0x4d26a0, 3 free
// 0x468fa0); then (live) the animation (exe 0x5e1c30 / 0x5e1e80), the hit cooldown (odd ticks), a footstep (0x128)
// every 0x16 ticks of animation 0, the two Birdbots kept at bones 2 / 4 ([0x895350] / [0x895358]), the carried box or
// the gripped player (offset [0x895360 + grip pose * 8]) between bones 1 / 3 (exe 0x4d2bd0), the hit entry (its row's
// attack, solid).
// Bind: model, hit points, box 0x895338, home, group (exe 0x5e6cd0), the two Birdbots (pool 0x20 type 0x2d sub 1
// variant 3, ids [0x89530a + riderRow * 8] + 0 / 1 when defeated: tracked / un-defeated).
// Main: exe 0x4d2de0 the hits (1 -> sub-state 8, 2 (dead) -> state 2), the sub-states [0x8953b8] (0 0x4d1b60 walk to
// the target, 1 0x4d1c00 drop the Birdbots, 2 0x4d1c60 fetch a box and throw it, 3 0x4d1fb0 shrug, 4 0x4d2010 turn off a
// wall, 5 0x4d20e0 stalk the player, 6 0x4d23f0 grab and throw the player (flag 0x26), 7 0x4d2580 walk home (then
// freed), 8 0x4d2610 flinch), exe 0x4d2d70 the collision. exe 0x4d2a90 picks the next sub-state (7 when sent home /
// variant 3; the nearest free box of the box spawner -> 2; else 5).
//
// A port gap where the exe would read through a null pointer (a missing bone point, a null class row or bone block) or
// index past a dispatch table.
#include "content/nino/field_enemies.hpp"
#include "content/nino/stage_timer.hpp"
#include "content/nino/field_enemies_state.hpp"
#include "engine/data_source.hpp"
#include "game/loaded_data.hpp"
#include "game/stage_smoke.hpp"

#include "content/nino/field_spawners.hpp"

#include "game/fixed_point.hpp"
#include "game/actor_animation.hpp"
#include "game/actor_collision.hpp"
#include "game/actor_model.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actor_stats.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/carry.hpp"
#include "game/distances.hpp"
#include "game/encounter.hpp"
#include "game/explosions.hpp"
#include "game/game_state.hpp"
#include "game/global_class_types.hpp"
#include "game/hit_list.hpp"
#include "game/hit_points.hpp"
#include "game/lift_box.hpp"
#include "game/player/player_motion.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/rewards.hpp"
#include "game/script_state.hpp"
#include "game/stage_state.hpp"
#include "game/story_flags.hpp"
#include "game/visibility.hpp"

namespace od::port {

using namespace field_enemies_state;

namespace {

inline constexpr const char* kEnemyPort = "nino field enemies port"; // the area of its port-gap errors

// the throw velocity of the carried states: Rx(the player's aim pitch) * (0, y, z) (exe 0x5e3e50)
void throwVelocity(AnimatedActor& actor, int y, int z) {
    int32_t v[3];
    playerAimVector(0, y, z, v);
    actor.velocity.x = 0;
    actor.velocity.y = int16_t(v[1]);
    actor.velocity.z = int16_t(v[2]);
}

// the yaw step away from the wall of the push-out result (exe 0x4a14a0 / 0x4d2010): +0x30 / -0x30
int16_t wallTurnStep(const AnimatedActor& actor) {
    const uint32_t v = actor.wallHit;
    int32_t dx = 0, dz = 0;
    if (v & 0x100) dx = -0x1000000;
    if (v & 0x200) dx = 0x1000000;
    if (v & 0x400) dz = -0x1000000;
    if (v & 0x800) dz = 0x1000000;
    // the local point {x + dx, (y uninitialised, unused), z + dz}
    const int32_t px = int32_t(uint32_t(dx) + uint32_t(actor.pos.x));
    const int32_t pz = int32_t(uint32_t(dz) + uint32_t(actor.pos.z));
    const int d = angleDiff(uint32_t(int32_t(actor.rotation.y)), uint32_t(angleOf(px, pz, actor.pos.x, actor.pos.z)));
    return d >= 0 ? 0x30 : -0x30;
}

constexpr uint8_t kTimerType = 0x1b;

// =================================================================================================================
// the critter 0x4a0f70
// =================================================================================================================

constexpr uint8_t kCritterBounces = 0x01;     // the walls reflect its flight yaw
constexpr uint8_t kCritterThrown = 0x02;      // its hit entry hurts (thrown)
constexpr uint8_t kCritterNoHitEntry = 0x04;
constexpr uint8_t kCritterKeepsOn = 0x08;     // a lost ground does not start the fall
constexpr uint8_t kCritterNoDamage = 0x10;
constexpr uint8_t kCritterHitWall = 0x01;
constexpr uint8_t kCritterLanded = 0x02;
constexpr uint8_t kCritterGroundLost = 0x04;
constexpr uint8_t kCritterOffRoom = 0x08;     // the move test's water bit (0x20000000)

// [0x893b5c]
enum class CritterState : uint8_t { Bind = 0, Main = 1, Carried = 2 };
// [0x893b68]
enum class CritterSub : uint8_t { DropIn = 0, Wander = 1, WallTurn = 2, Fall = 3, Knocked = 4, Slide = 5, GetUp = 6 };
// the carried sub-states
enum class CarriedSub : uint8_t { Hands = 0, Flying = 1 };
// the animations
constexpr uint8_t kAnimStand = 0, kAnimDrop = 1, kAnimGetUp = 2, kAnimWalk = 3, kAnimHeld = 4, kAnimKnocked = 5;
constexpr uint8_t kNoAnim = 0xff;
constexpr uint8_t kDustVariant = 8;
const DataTable<CollisionBoxRecord> critterBox{TableId::FieldEnemiesCritterBox};
const DataTable<InteractionBoxRecord> critterLiftBox{TableId::FieldEnemiesCritterLiftBox};
constexpr uint8_t kOffScreenTicks = 0x1e;
constexpr uint8_t kStaggerLimit = 0x20;
constexpr uint16_t kHurtSound = 0x8d, kKnockSound = 0x1a1;
constexpr int16_t kGravity = 0x40;

void critterSub(AnimatedActor& actor, CritterSub s) {
    actor.subState = uint8_t(s);
    actor.enterStep(0);
}

// exe 0x4a1930(actor, yaw): landed: halved, a small bounce, 1; else moved along the yaw, gravity 0x40
int critterAir(AnimatedActor& actor, const CritterFields& critter, int yaw) {
    if (critter.contact & kCritterLanded) {
        actor.velocity.z = int16_t(actor.velocity.z >> 1);
        actor.velocity.y = int16_t(uint16_t(uint32_t(-int(actor.velocity.y)) >> 2));
        return 1;
    }
    moveAlongYaw(actor.pos, yaw, 0, actor.velocity.y, actor.velocity.z); // exe 0x5e36f0
    actor.velocity.y += uint32_t(kGravity);
    return 0;
}

// exe 0x4a1980: the walls -> the contact bits, the push-out result kept; 1 = the ground was lost (more than 0x20 below)
int critterCollide(const PlayerEnv& env, AnimatedActor& actor, CritterFields& critter) {
    uint32_t r5 = 0;
    critter.contact = 0;
    const uint32_t r = actorTestMove(env, actor); // exe 0x40f570
    if (r & 0x20000000) critter.contact |= kCritterOffRoom;
    if (int32_t(r) < 0) {
        r5 = actorPushOut(env, actor); // exe 0x410960
        actor.wallHit = r5;
        if (int32_t(r5) < 0) actor.pos = actor.prevPos;
        if (r5 & 0xf00) critter.contact = kCritterHitWall;
        if (critter.behaviour & kCritterBounces) { // the local angles {pitch, flight yaw, roll}: the yaw reflected
            int16_t pitch = actor.rotation.x, yaw = critter.flightYaw;
            const int16_t roll = actor.rotation.z;
            if (reflectRotationOffWalls(pitch, yaw, r5)) critter.contact |= kCritterLanded; // exe 0x410740
            critter.flightYaw = yaw;
            actor.rotation.z = roll;
        }
    }
    if (actor.airborne == 0) {
        const int g = actorGround(env, actor); // exe 0x40f190
        if (g - actor.pos.y.whole >= 0x21) {
            critter.contact |= kCritterGroundLost;
            return 1;
        }
        actor.pos.y.whole = int16_t(g);
        return 0;
    }
    if (!(critter.behaviour & kCritterBounces) && (r5 & 0x3000)) {
        actor.airborne = 0;
        actor.pos.y.whole = int16_t(actorGround(env, actor));
        critter.contact |= kCritterLanded;
    }
    return 0;
}

// exe 0x4a1ac0: the hits do no damage; sound 0x8d, the balance (its row's byte 9, byte 10 on a strong push); 1 = react
int critterHurt(AnimatedActor& actor, CritterFields& critter) {
    const uint32_t v = critter.hitBox.result;
    const AnimatedActorStats row = animatedStats(actor.type);
    const uint32_t d = v & kHitDamageMask;
    if (critter.behaviour & kCritterNoDamage) return 0;
    int r = 0;
    if (d != 0 && (v & (kHitAttackSolid | kHitAttackByPlayer | kHitAttackThrown))) {
        const bool under = uint32_t(critter.stagger) + d < kStaggerLimit;
        if (under)
            critter.stagger = uint8_t(critter.stagger + d);
        else
            critter.stagger = 0;
        r = under ? 0 : 1;
        soundPlayAtPos(kHurtSound, actor.pos);
        if (v & kHitAttackByPlayer) {
            if (!(v & kHitAttackStrongPush)) {
                balanceAdd(-int(row.hitSize)); // exe 0x5e51d0 (its row's byte 9: the balance lost)
                return r;
            }
            balanceAdd(-int(row.balanceLossThrown)); // (its row's byte 10)
            r = 1;
            critter.stagger = 0;
        }
    }
    return r;
}

// exe 0x4a1b90(actor, dy, variant): the dust puff (the stage's static type 3, exe 0x5e12f0)
void critterDust(const AnimatedActor& actor, int16_t dy, uint8_t variant) {
    const uint32_t e = allocActor60(kActorStageClass);
    if (!e) return;
    StaticActor& dust = staticActorAt(e);
    auto&& params = SmokeParams(actorHeaderAt(e));
    dust.variant = variant;
    dust.flags = kActorLive;
    dust.loopFlags |= kActorSweepGroup2;
    dust.type = kSmokeType;
    dust.pos.x.whole = actor.pos.x.whole;
    dust.pos.y.whole = int16_t(actor.pos.y.whole + dy);
    dust.pos.z.whole = actor.pos.z.whole;
    params.driftX = 0;
    params.driftZ = 0;
    params.life = 4;
}

// exe 0x4a1bf0: carried away (state 2): the balance - its row's byte 8 * 0x1000, a message trigger (pool e0 type 0x1b)
void critterGone(const AnimatedActor& actor) {
    if (CritterState(actor.state) != CritterState::Carried) return;
    balanceAdd(-int(uint32_t(animatedStats(actor.type).bodyAttack) << 12)); // (its row's byte 8)
    const uint32_t e = allocActorE0(); // exe 0x5e1440
    if (!e) return;
    ControllerActor& timer = controllerActorAt(e);
    auto&& params = StageTimerParams(controllerActorAt(e));
    timer.flags = kActorLive;
    timer.type = kTimerType;
    timer.variant = 0;
    params.value = 0x16;
    params.limit = 0x7f;
    params.flag = -1;
}

// exe 0x4a11c0: the model, drawn, its hit points, the motion bytes cleared, the boxes (liftable), the class block
void critterBind(AnimatedActor& actor, CritterFields& critter) {
    if (!bindActorModel(actor, 0)) { // exe 0x5e0ba0
        freeActor(actor);
        return;
    }
    actor.flags |= kActorDrawn;
    actor.anim = kAnimStand;
    actor.prevAnim = kNoAnim;
    const int16_t hp = hasAnimatedStats(actor.type) ? int16_t(animatedStats(actor.type).hitPoints) : int16_t(0);
    actor.velocity.clearBytes();
    actor.spin.x = 0;
    actor.spin.y = 0;
    actor.rotation.z = 0;
    actor.rotation.x = 0;
    critter.tiltY = 0;
    critter.tiltX = 0;
    actor.airborne = 0;
    actor.onPlatform = 0;
    actor.maxHitPoints = hp;
    actor.hitPoints = hp;
    actor.collisionBox.pointAt(critterBox.stateAddress());
    actor.interactionBox.pointAt(critterLiftBox.stateAddress());
    actor.interaction = kInteractLift;
    actor.clearClassBlock(); // exe 0x40dc40
    critter.offScreenTicks = kOffScreenTicks;
    actor.enterState(uint8_t(CritterState::Main));
}

// exe 0x4a12e0: dropped in (up -0x300, bouncing once), dust every 4 ticks; landed -> gets up
void critterDropIn(AnimatedActor& actor, CritterFields& critter) {
    if (actor.step == 0) {
        critter.behaviour = kCritterBounces | kCritterKeepsOn | kCritterNoDamage;
        actor.anim = kAnimDrop;
        actor.velocity.z = 0;
        actor.velocity.y = -0x300;
        actor.airborne = 1;
        actor.enterStep(1);
    }
    if (critterAir(actor, critter, actor.rotation.y)) {
        if (actor.stepCounter != 0) {
            actor.airborne = 0;
            critterSub(actor, CritterSub::GetUp);
        } else {
            moveAlongYaw(actor.pos, actor.rotation.y, 0, actor.velocity.y, 0);
            actor.airborne = 1;
            actor.stepCounter = 1;
        }
    }
    if (!(critter.dropTicks & 3)) critterDust(actor, 0, kDustVariant);
    critter.dropTicks += 1u;
}

// exe 0x4a1390: wanders around the player at -0x140 (a random side), a wall -> the wall turn; dust now and then
void critterWander(AnimatedActor& actor, CritterFields& critter) {
    if (actor.step == 0) {
        actor.anim = kAnimWalk;
        actor.velocity.setXY(0, 0);
        actor.velocity.z = -0x140;
        critter.behaviour = 0;
        const uint32_t r = rngStep();
        actor.enterStep(1);
        actor.spin.y = int16_t(((0xaaaaaaaau >> (r & 0x1f)) & 1) ? 0x600u : 0xfa00u);
        turnToward(actor.rotation.y, int(actor.spin.y + angleOf(player().pos, actor.pos)), 0x30);
    } else if (actor.step == 1) {
        turnToward(actor.rotation.y, int(actor.spin.y + angleOf(player().pos, actor.pos)), 0x30);
    }
    integrateVelocity(actor, 0, 0, actor.velocity.z);
    if (critter.contact & kCritterHitWall) critterSub(actor, CritterSub::WallTurn);
    if (!(critter.wanderTicks & 7)) {
        const uint32_t r = rngStep();
        if (int32_t(0x15c200u << (r & 0x1f)) < 0) critterDust(actor, -0x8c, kDustVariant);
    }
    critter.wanderTicks += 1u;
}

// exe 0x4a14a0: turns away from the wall while walking until it is off it
void critterWallTurn(AnimatedActor& actor, CritterFields& critter) {
    if (actor.step == 0) {
        critter.behaviour = 0;
        const int16_t st = wallTurnStep(actor);
        actor.enterStep(1);
        actor.spin.y = st;
    }
    integrateVelocity(actor, 0, 0, actor.velocity.z);
    actor.rotation.y = int16_t(actor.rotation.y + actor.spin.y);
    if (critter.contact & kCritterHitWall) return;
    critterSub(actor, CritterSub::Wander);
}

// exe 0x4a1560: falls; landed -> wanders
void critterFall(AnimatedActor& actor, CritterFields& critter) {
    if (actor.step == 0) {
        actor.anim = kAnimWalk;
        critter.behaviour = kCritterBounces | kCritterKeepsOn;
        actor.acceleration.y = kGravity;
        actor.velocity.y = 0;
        actor.airborne = 1;
        actor.enterStep(1);
    } else if (actor.step != 1) {
        return;
    }
    if (!critterAir(actor, critter, actor.rotation.y)) return;
    actor.airborne = 0;
    critterSub(actor, CritterSub::Wander);
}

// exe 0x4a15c0: knocked back (forward when hit from behind), lands sliding, then gets up
void critterKnocked(AnimatedActor& actor, CritterFields& critter) {
    if (critter.contact & kCritterHitWall) {
        actor.acceleration.z = 0;
        actor.velocity.z = 0;
    }
    const uint8_t s = actor.step;
    if (s == 2) {
        integrateVelocity(actor, 0, 0, actor.velocity.z);
        if (critter.contact & kCritterGroundLost) {
            actor.airborne = 1;
            actor.enterStep(1);
        } else if (actor.animEnded()) {
            critterSub(actor, CritterSub::GetUp);
            critter.knockedMark = 0;
        }
    } else if (s <= 1) {
        if (s == 0) {
            critter.behaviour = kCritterBounces | kCritterKeepsOn | kCritterNoDamage;
            const int d = angleDiff(uint32_t(int32_t(actor.rotation.y)),
                                    uint32_t(int32_t(critter.hitBox.contact.x)));
            actor.anim = kAnimKnocked;
            actor.velocity.z = 0x180;
            actor.acceleration.z = -0x10;
            actor.velocity.y = int16_t((critter.hitBox.result & kHitAttackStrongPush) ? -0x180 : -0xc0);
            if (uint32_t(d + 0x400) <= 0x7ffu) {
                actor.velocity.z = -0x180;
                actor.acceleration.z = 0x10;
            }
            actor.acceleration.y = kGravity;
            soundPlayAtPos(kKnockSound, actor.pos);
            actor.airborne = 1;
            actor.enterStep(1);
        }
        if (critterAir(actor, critter, actor.rotation.y)) {
            actor.velocity.z = int16_t(actor.velocity.z << 1);
            actor.airborne = 0;
            actor.enterStep(2);
        }
    }
    if (actor.animKey.events & 1) {
        actor.velocity.z = int16_t(actor.velocity.z + actor.acceleration.z);
        if (int16_t(uint16_t(actor.velocity.z) ^ uint16_t(actor.acceleration.z)) >= 0) actor.velocity.z = 0;
    }
}

// exe 0x4a1700: slides along its flight yaw, then gets up
void critterSlide(AnimatedActor& actor, CritterFields& critter) {
    if (actor.step != 0) {
        if (actor.animEnded()) critterSub(actor, CritterSub::GetUp);
    } else {
        critter.behaviour = 0;
        actor.rotation.x = 0;
        actor.enterStep(1);
    }
    moveAlongYaw(actor.pos, critter.flightYaw, 0, 0, actor.velocity.z);
}

// exe 0x4a1750: gets up, then wanders
void critterGetUp(AnimatedActor& actor) {
    if (actor.step == 0) {
        actor.anim = kAnimGetUp;
        actor.enterStep(1);
        return;
    }
    if (!actor.animEnded()) return;
    critterSub(actor, CritterSub::Wander);
}

// exe 0x4a1270
void critterMain(const PlayerEnv& env, AnimatedActor& actor, CritterFields& critter) {
    if (critterHurt(actor, critter)) critterSub(actor, CritterSub::Knocked);
    switch (CritterSub(actor.subState)) {
    case CritterSub::DropIn: critterDropIn(actor, critter); break;
    case CritterSub::Wander: critterWander(actor, critter); break;
    case CritterSub::WallTurn: critterWallTurn(actor, critter); break;
    case CritterSub::Fall: critterFall(actor, critter); break;
    case CritterSub::Knocked: critterKnocked(actor, critter); break;
    case CritterSub::Slide: critterSlide(actor, critter); break;
    case CritterSub::GetUp: critterGetUp(actor); break;
    default: notPortedAt(kEnemyPort, "0x4a0f70 state 1 sub-state (table 0x893b68 has 7)", actor.subState);
    }
    if (critterCollide(env, actor, critter) && !(critter.behaviour & kCritterKeepsOn)) critterSub(actor, CritterSub::Fall);
    if (!playerLiftTargetCheck(actor, 0)) return; // exe 0x573a60
    actor.enterState(uint8_t(CritterState::Carried));
}

// exe 0x4a1880: thrown -- along the flight yaw (a hit solid turns it back, no hit entry), bouncing once; landed ->
// the slide
void critterFlying(const PlayerEnv& env, AnimatedActor& actor, CritterFields& critter) {
    if (critter.contact & kCritterHitWall) actor.velocity.z = int16_t(actor.velocity.z >> 1);
    if (actor.step == 0 && (critter.hitBox.result & kHitAttackSolid)) {
        actor.velocity.z = int16_t(actor.velocity.z >> 1);
        critter.flightYaw += 0x800u;
        critter.behaviour |= kCritterNoHitEntry;
        actor.enterStep(1);
    }
    if (critterAir(actor, critter, critter.flightYaw)) {
        if (actor.stepCounter == 0) {
            moveAlongYaw(actor.pos, critter.flightYaw, 0, actor.velocity.y, actor.velocity.z);
            actor.airborne = 1;
            actor.stepCounter = 1;
            critterCollide(env, actor, critter);
            return;
        }
        actor.airborne = 0;
        actor.enterState(uint8_t(CritterState::Main), uint8_t(CritterSub::Slide));
    }
    critterCollide(env, actor, critter);
}

// exe 0x4a17a0: at the player's hands until released: thrown (along the aim) or dropped
void critterHands(const PlayerEnv& env, AnimatedActor& actor, CritterFields& critter) {
    if (actor.step == 0) {
        critter.behaviour = kCritterKeepsOn;
        actor.anim = kAnimHeld;
        actor.enterStep(1);
    }
    carryAtHands(actor, 0, 0, 0); // exe 0x573ab0
    const CarryRelease r = carryReleased(); // exe 0x573950
    if (r == CarryRelease::Held) return;
    if (r != CarryRelease::Thrown) {
        actor.velocity.y = -0x1c0;
        actor.velocity.z = -0x100;
        critter.behaviour = kCritterBounces | kCritterKeepsOn;
    } else {
        throwVelocity(actor, -0xc0, -0x300);
        critter.behaviour = kCritterBounces | kCritterThrown | kCritterKeepsOn;
    }
    actor.anim = kAnimDrop;
    actor.acceleration.y = kGravity;
    const int16_t yaw = player().rotation.y;
    actor.rotation.y = yaw;
    critter.flightYaw = yaw;
    actor.rotation.x = 0;
    actor.airborne = 1;
    carryDropFromPlayer(actor); // exe 0x573990
    actor.subState = uint8_t(CarriedSub::Flying);
    actor.enterStep(0);
    critterFlying(env, actor, critter);
}

// exe 0x4a0f70
void critter(const PlayerEnv& env, AnimatedActor& actor) {
    auto&& critter = classState<CritterFields>(actor);
    actor.prevAnim = actor.anim;
    actor.tint = kTintNeutral;
    actor.savePreviousPose();
    actor.savePreviousContact();
    switch (CritterState(actor.state)) {
    case CritterState::Bind: critterBind(actor, critter); break;
    case CritterState::Main: critterMain(env, actor, critter); break;
    case CritterState::Carried: // exe 0x4a1780
        if (CarriedSub(actor.subState) == CarriedSub::Hands) critterHands(env, actor, critter);
        else critterFlying(env, actor, critter);
        break;
    default: notPortedAt(kEnemyPort, "0x4a0f70 state (table 0x893b5c has 3)", actor.state);
    }
    if (!(actor.flags & kActorLive)) return;
    const int d = distance3(actor.pos, player().pos); // exe 0x5e3a60
    const int32_t far = int32_t(uint32_t(stage().depthRange) * 4);
    if ((critter.contact & kCritterOffRoom) && (far < d || pointVisibleOrNear(actor.pos, 0) != 0)) {
        critterGone(actor);
        freeActor(actor);
        return;
    }
    if (int32_t(uint32_t(stage().depthRange) * 2) < int32_t(actor.pos.y.whole)) {
        critterGone(actor);
        freeActor(actor);
        return;
    }
    if (pointVisibleOrNear(actor.pos, 0x200) != 0) { // exe 0x5e59a0
        const uint8_t n = uint8_t(critter.offScreenTicks - 1u);
        critter.offScreenTicks = n;
        if (n == 0) {
            critterGone(actor);
            freeActor(actor);
            return;
        }
    } else {
        critter.offScreenTicks = kOffScreenTicks;
    }
    if (actor.anim != actor.prevAnim) {
        animSet(actor, actor.anim, critter.animKeyStart); // exe 0x5e1c50
        critter.animKeyStart = 0;
    } else {
        animStep(actor); // exe 0x5e1ea0
    }
    LaneVec4 partOne = decodedFrame().entries[2]; // part 1's angles, tilted
    partOne.x = int16_t(critter.tiltX + partOne.x);
    partOne.y = int16_t(critter.tiltY + partOne.y);
    animPose(actor); // exe 0x5e2d90
    critter.hitBox.result = 0;
    critter.hitBox.previousX = actor.prevPos.x.whole;
    critter.hitBox.previousZ = actor.prevPos.z.whole;
    const int16_t pos[3] = {actor.pos.x.whole, wrap16(actor.pos.y.whole - 100), actor.pos.z.whole};
    if (!(critter.behaviour & kCritterNoHitEntry)) {
        uint32_t shape, attack;
        if (critter.behaviour & kCritterThrown) {
            attack = animatedStats(actor.type).attack | kHitAttackByPlayer;
            shape = hitOwner(actor.slot) | hitGroup(kHitGroupPlayerAttacks) | kHitContactMotion | hitRadius(0x40);
        } else {
            attack = kHitAttackSolid;
            shape = hitOwner(actor.slot) | hitGroup(kHitGroupLiftable) | kHitNoLockOn | kHitContactMotion |
                    hitRadius(0x40);
        }
        hitListSubmit(pos, critter.hitBox, shape, attack); // exe 0x5e4120
    }
    if (CritterState(actor.state) == CritterState::Carried && actor.subState == 0) {
        actorCollisionSubmitMover(actor); // exe 0x4140b0
        return;
    }
    // exe 0x5f20f0(a, 0x40, 0x100, 0): the shadow (render output)
    actorCollisionSubmitSolid(actor, SolidType::StandOn, kNoBone, 0); // exe 0x414050(a, 1, 0xffff, 0)
    actorCollisionSubmitMover(actor);
}

// =================================================================================================================
// the box carrier 0x4d1700
// =================================================================================================================

constexpr uint8_t kCarrierHitWall = 0x01;
constexpr uint8_t kCarrierHitFloor = 0x02;
constexpr uint8_t kCarrierOffRoom = 0x08;

// A rider (pool 0x20 type 0x2d sub 1 variant 3): its mode, count, its carrier's slot, its tracked id, its point
struct RiderParams {
    explicit RiderParams(const AnimatedActor& actor) : RiderParams(const_cast<AnimatedActor&>(actor)) {}
    explicit RiderParams(AnimatedActor& actor) : mode(actor.params[0]), count(actor.params[1]), carrier(actor.params[2]), trackId(actor.params[3]), pointX(actor.velocity.spare), pointZ(actor.acceleration.spare) {}
    uint8_t& mode;
    uint8_t& count;
    uint8_t& carrier;
    uint8_t& trackId;
    int16_t& pointX;
    int16_t& pointZ;
};
// a row of the riders' table (exe data, 8 bytes): their mode and count, the first id, their point
struct RiderRow : DataRecord {
    static constexpr uint32_t kBytes = 0x8;
    uint8_t mode = 0; // +0x0
    uint8_t count = 0; // +0x1
    uint16_t firstId = 0; // +0x2
    int16_t pointX = 0; // +0x4
    int16_t pointZ = 0; // +0x6
    template <class R> static RiderRow load(const R& r) {
        RiderRow v;
        v.mode = r.template read<uint8_t>(0x0);
        v.count = r.template read<uint8_t>(0x1);
        v.firstId = r.template read<uint16_t>(0x2);
        v.pointX = r.template read<int16_t>(0x4);
        v.pointZ = r.template read<int16_t>(0x6);
        return v;
    }
};
// an offset from bones (exe data: s16 x / y / z and, for the riders, the bone)
struct BoneOffset : DataRecord {
    static constexpr uint32_t kBytes = 0x8;
    int16_t x = 0; // +0x0
    int16_t y = 0; // +0x2
    int16_t z = 0; // +0x4
    int16_t bone = 0; // +0x6
    template <class R> static BoneOffset load(const R& r) {
        BoneOffset v;
        v.x = r.template read<int16_t>(0x0);
        v.y = r.template read<int16_t>(0x2);
        v.z = r.template read<int16_t>(0x4);
        v.bone = r.template read<int16_t>(0x6);
        return v;
    }
};
constexpr TableId kRiderOffsets[2] = {TableId::FieldEnemiesRiderFirstOffset, TableId::FieldEnemiesRiderSecondOffset};
const DataTable<ScalarTable<int8_t, 16>> stalkTurns{TableId::FieldEnemiesStalkTurns};
const DataTable<ScalarTable<uint8_t, 16>> stalkTicks{TableId::FieldEnemiesStalkTicks};

// [0x8953a8]
enum class CarrierState : uint8_t { Bind = 0, Main = 1, Dying = 2, Gone = 3 };
// [0x8953b8]
enum class CarrierSub : uint8_t {
    ToTarget = 0, Unload = 1, Fetch = 2, Shrug = 3, WallTurn = 4, Stalk = 5, Grab = 6, Home = 7, Flinch = 8
};
enum class FetchStep : uint8_t { Init = 0, Walk = 1, Climb = 2, Snap = 3, Face = 4, Lift = 5, Lifting = 6, Throw = 7, Done = 8 };
enum class StalkStep : uint8_t { Init = 0, Follow = 1, Reach = 2, Shrug = 3, Shrugging = 4 };
enum class GrabStep : uint8_t { Grip = 0, Lift = 1, Throw = 2, Back = 3 };
// the move to a point's result
constexpr uint32_t kMoving = 0, kBraking = 1, kArrived = 2;
// the animations
constexpr uint8_t kCarrierWalk = 0, kCarrierUnload = 1, kCarrierReach = 2, kCarrierLift = 3, kCarrierThrow = 4,
                  kCarrierGrip = 5, kCarrierShrug = 6, kCarrierHurt = 7;
const DataTable<CollisionBoxRecord> carrierBox{TableId::FieldEnemiesCarrierBox};
const DataTable<CollisionBoxRecord> carrierFallBox{TableId::FieldEnemiesCarrierFallBox};
constexpr uint8_t kRiderType = 0x2d, kRiderSub = 1, kRiderVariant = 3;
constexpr uint8_t kDownedBirdbotVariant = 0x80;
constexpr uint8_t kGroupBoxTaken = 1, kGroupPlayerTaken = 2; // the enemy group's marks
constexpr uint32_t kFlagCatchScene = 0x26;
constexpr uint16_t kSoundReach = 0x114, kSoundLift = 0x112, kSoundGrip = 0x113, kSoundThrow = 0x115, kSoundShrug = 0x129,
                   kSoundStep = 0x128, kHurtSoundStrong = 0x8e;
constexpr uint8_t kDamageLimit = 0x48;
constexpr int kWalkSpeed = -0x180, kFetchSpeed = -0x100;
constexpr int32_t kBoxHeight = 0x1e00000; // the box's top above its position (16.16)
constexpr int16_t kStalkRaise = int16_t(0xfe70), kStalkRaiseHigh = int16_t(0xfda8); // the player's point raised

void carrierSub(AnimatedActor& actor, CarrierSub s) {
    actor.subState = uint8_t(s);
    actor.enterStep(0);
}

// exe 0x4d2840(actor, point, speed): accelerate to the speed, turn toward the point, move along the facing; within the
// brake distance -> brake (kBraking); stopped -> snapped to the point's x / z (kArrived)
uint32_t moveTo(AnimatedActor& actor, CarrierFields& carrier, const int32_t pt[3], int speed) {
    const uint8_t s = actor.stepCounter;
    uint32_t r = kMoving;
    bool accel = false;
    if (s == 0) {
        actor.anim = kCarrierWalk;
        actor.stepCounter = 1;
        accel = true;
    } else if (s == 1) {
        accel = true;
    } else if (s == 2) {
        actor.velocity.z = int16_t(actor.velocity.z - actor.acceleration.z);
        if (actor.velocity.z >= 0) {
            actor.pos.x = pt[0];
            actor.pos.z = pt[2];
            actor.velocity.z = 0;
            r = kArrived;
        }
    }
    if (accel) {
        actor.velocity.z = int16_t(actor.velocity.z + actor.acceleration.z);
        if (actor.velocity.z <= speed) actor.velocity.z = int16_t(speed);
        if (planarDistance(actor.pos, pt) <= carrier.brakeDistance) {
            actor.stepCounter = 2;
            r = kBraking;
        }
    }
    turnYawTo(actor.rotation.y, uint32_t(angleOf(pt[0], pt[2], actor.pos.x, actor.pos.z)), actor.spin.y);
    integrateVelocity(actor, 0, 0, actor.velocity.z);
    return r;
}
// the angles {pitch toward the point + 0x800, yaw, 0} the 3D moves go along
void towardPoint(const AnimatedActor& actor, const int32_t pt[3], int16_t rot[3]) {
    const int16_t me[3] = {actor.pos.x.whole, actor.pos.y.whole, actor.pos.z.whole};
    const int16_t to[3] = {wholePart(pt[0]), wholePart(pt[1]), wholePart(pt[2])};
    rot[1] = actor.rotation.y;
    rot[0] = int16_t(pitchOf(me, to) + 0x800);
    rot[2] = 0;
}
// exe 0x4d2900(actor, point, speed): the same in 3D (exe 0x5e3a60), moving along the pitch toward the point
uint32_t moveTo3(AnimatedActor& actor, CarrierFields& carrier, const int32_t pt[3], int speed) {
    const uint8_t s = actor.stepCounter;
    uint32_t r = kMoving;
    bool accel = false;
    if (s == 0) {
        actor.anim = kCarrierWalk;
        actor.stepCounter = 1;
        accel = true;
    } else if (s == 1) {
        accel = true;
    } else if (s == 2) {
        actor.velocity.z = int16_t(actor.velocity.z - actor.acceleration.z);
        if (actor.velocity.z >= 0) {
            actor.velocity.z = 0;
            r = kArrived;
        }
    }
    if (accel) {
        actor.velocity.z = int16_t(actor.velocity.z + actor.acceleration.z);
        if (actor.velocity.z <= speed) actor.velocity.z = int16_t(speed);
        if (uint32_t(distance3(actor.pos, pt)) < uint32_t(int32_t(carrier.brakeDistance))) {
            actor.stepCounter = 2;
            r = kBraking;
        }
    }
    turnYawTo(actor.rotation.y, uint32_t(angleOf(pt[0], pt[2], actor.pos.x, actor.pos.z)), actor.spin.y);
    int16_t rot[3];
    towardPoint(actor, pt, rot);
    moveAlongRotation(actor.pos, rot, 0, 0, actor.velocity.z); // exe 0x5e3810
    return r;
}
// exe 0x4d29e0(actor, point, step): within step / 16 -> snapped to the point (true); turn, move along the pitch
bool snapTo(AnimatedActor& actor, const int32_t pt[3], int step) {
    const uint32_t d = uint32_t(distance3(actor.pos, pt));
    const bool snap = d * 16u + uint32_t(step) <= uint32_t(step * 2);
    if (snap) {
        actor.pos.x = pt[0];
        actor.pos.y = pt[1];
        actor.pos.z = pt[2];
        actor.velocity.z = 0;
    }
    turnYawTo(actor.rotation.y, uint32_t(angleOf(pt[0], pt[2], actor.pos.x, actor.pos.z)), actor.spin.y);
    int16_t rot[3];
    towardPoint(actor, pt, rot);
    moveAlongRotation(actor.pos, rot, 0, 0, -step);
    return snap;
}

// exe 0x4d2ca0(box): 0 = a live lift box nobody holds, else -1
int boxBusy(const StaticActor& box) {
    if ((box.flags & kActorLive) && box.type == kLiftBoxType && LiftBoxHolding(box).unheld()) return 0;
    return -1;
}
StaticActor& carriedBox(const CarrierFields& carrier) { return staticPool()[carrier.boxIndex]; }

// exe 0x4d2cc0(actor, box +0x14c): the carried box let go (no speed, our yaw)
void dropBox(AnimatedActor& actor, CarrierFields& carrier) {
    StaticActor& box = carriedBox(carrier);
    if (!(box.flags & kActorLive)) return;
    LiftBoxHolding(box).holder = kLiftBoxLetGo;
    box.velocity.y = 0;
    box.velocity.z = 0;
    box.rotation.y = actor.rotation.y;
    carrier.carry &= uint8_t(~kCarrierBox);
}

// exe 0x4d2d10(actor, box +0x14c): the two riders let go (their carrier cleared)
void dropRiders(CarrierFields& carrier) {
    for (int k = 1; k >= 0; --k) {
        const uint8_t sl = carrier.riders[size_t(k)];
        if (sl != 0) {
            AnimatedActor& rider = animatedPool()[size_t(sl - kAnimatedClass)];
            if (rider.flags & kActorLive) RiderParams(rider).carrier = 0;
            carrier.riders[size_t(k)] = 0;
        }
    }
    carrier.carry &= uint8_t(~kCarrierRiders);
}

// exe 0x4d2a90(actor, box +0x14c): the next sub-state -- home when sent / variant 3; the nearest free box above it of
// the box spawner -> fetch it; else stalk the player
void next(AnimatedActor& actor, CarrierFields& carrier) {
    if (carrier.goHome != 0 || actor.variant == 3) {
        carrierSub(actor, CarrierSub::Home);
        return;
    }
    if (!(enemyGroupMarks(int32_t(carrier.group), EnemyGroupMarks::Either) & kGroupBoxTaken) && actor.variant != 2 &&
        carrier.boxSpawner != 0) {
        BoxSpawnerFields& spawner = classState<BoxSpawnerFields>(
            controllerPool().at(kControllerPool + (uint32_t(carrier.boxSpawner) - kControllerClass) * ControllerPool::stride()));
        if (spawner.lever != 0) {
            uint8_t best = 0;
            int32_t bd = 0x7fffffff;
            for (uint32_t i = 0; i < 4; ++i) {
                const uint16_t w = spawner.boxes[i];
                if (int16_t(w) < 1) continue;
                StaticActor& box = staticPool()[size_t((uint32_t(w) & 0xff) - kStaticClass)];
                auto&& hold = LiftBoxHolding(box);
                if ((box.flags & kActorLive) && hold.holder == 0 && hold.carried == 0 &&
                    actor.pos.y.whole < box.pos.y.whole) {
                    const int32_t d = distance3(actor.pos, box.pos);
                    if (d < bd) {
                        best = box.slot;
                        bd = d;
                    }
                }
            }
            if (best != 0) {
                carrier.boxIndex = uint8_t(best + 0xa0); // the static pool's index (the slot - 0x60)
                carrierSub(actor, CarrierSub::Fetch);
                return;
            }
        }
    }
    carrierSub(actor, CarrierSub::Stalk);
}

// exe 0x4d2560: a wall -> the wall turn, 1
int wallTurnIfHit(AnimatedActor& actor, const CarrierFields& carrier) {
    if (carrier.contact & kCarrierHitWall) {
        carrierSub(actor, CarrierSub::WallTurn);
        return 1;
    }
    return 0;
}

// exe 0x4d23c0: the player can be gripped (alive, no immunity, not knocked back / down)
bool playerGrippable() {
    const Player& p = player();
    if (p.hitPoints < 0 || p.immunity != 0) return false;
    const PlayerSub s = PlayerSub(p.subState);
    return s != PlayerSub::KnockBack && s != PlayerSub::KnockDown;
}

// exe 0x4d2de0: the hits (no reaction while busy or a script runs): sound 0x8d / 0x8e; 1 = flinch (a strong hit or 0x48
// accumulated), 2 = dead
int carrierHurt(AnimatedActor& actor, CarrierFields& carrier) {
    const uint32_t d = carrier.hitBox.result & kHitDamageMask;
    if ((carrier.carry & kCarrierBusy) || scriptState().flags != 0) return 0;
    int r = 0;
    if (d != 0) {
        uint32_t snd = kHurtSound;
        const bool under = uint32_t(carrier.damage) + d < kDamageLimit;
        if (under) carrier.damage = uint8_t(carrier.damage + d);
        const bool strong = (carrier.hitBox.result & kHitAttackStrongPush) != 0;
        if (strong) snd = kHurtSoundStrong;
        r = (strong || !under) ? 1 : 0;
        actor.tint = kTintFlash;
        soundPlayAtPos(uint16_t(snd), actor.pos);
        if (hitPointsAdd(actor, -int(d))) r = 2;
    }
    return r;
}

// exe 0x4d2d70: the walls -> the contact bits, the push-out result kept
void carrierCollide(const PlayerEnv& env, AnimatedActor& actor, CarrierFields& carrier) {
    carrier.contact = 0;
    const uint32_t r = actorTestMove(env, actor);
    if (r & 0x20000000) carrier.contact |= kCarrierOffRoom;
    if (int32_t(r) >= 0) return;
    const uint32_t p = actorPushOut(env, actor);
    actor.wallHit = p;
    if (int32_t(p) < 0) actor.pos = actor.prevPos;
    if (p & 0xf00) carrier.contact |= kCarrierHitWall;
    if (p & 0x3000) carrier.contact |= kCarrierHitFloor;
}

// exe 0x4d2bd0(actor, object, offset): the object between bones 1 and 3 (both offset by the vector), pitched at us
void holdBetweenHands(const AnimatedActor& actor, Actor& o, const BoneOffset& off) {
    const int16_t v[3] = {off.x, off.y, off.z};
    int16_t p1[3], p3[3];
    bonePointOffset(actor, 1, v, p1); // exe 0x5e1970
    bonePointOffset(actor, 3, v, p3);
    o.savePreviousPose();
    o.pos.x.whole = int16_t((int32_t(p3[0]) + int32_t(p1[0])) >> 1);
    o.pos.y.whole = int16_t((int32_t(p3[1]) + int32_t(p1[1])) >> 1);
    o.pos.z.whole = int16_t((int32_t(p3[2]) + int32_t(p1[2])) >> 1);
    o.rotation.x = int16_t(pitchOf(o.pos, actor.pos) & 0x3ff);
}

// exe 0x4d1970: the model, drawn, its hit points, the motion bytes cleared, the box, its home and group, its two riders
void carrierBind(AnimatedActor& actor, CarrierFields& carrier) {
    if (!bindActorModel(actor, 0)) { // exe 0x5e0ba0
        freeActor(actor);
        return;
    }
    actor.flags |= kActorDrawn;
    actor.anim = kCarrierWalk;
    actor.prevAnim = kNoAnim;
    const int16_t hp = hasAnimatedStats(actor.type) ? int16_t(animatedStats(actor.type).hitPoints) : int16_t(0);
    actor.maxHitPoints = hp;
    actor.hitPoints = hp;
    actor.velocity.clearBytes();
    actor.spin.x = 0;
    actor.spin.y = 0;
    actor.rotation.z = 0;
    actor.rotation.x = 0;
    actor.collisionBox.pointAt(carrierBox.stateAddress());
    actor.interactionBox = nullptr;
    actor.interaction = 0;
    actor.airborne = 0;
    actor.onPlatform = 0;
    actor.clearClassBlock(); // exe 0x40dc40
    carrier.homeX = actor.pos.x.whole;
    carrier.homeZ = actor.pos.z.whole;
    carrier.group = uint8_t(enemyGroupFindFor(actor, 0)); // exe 0x5e6cd0
    const RiderRow row = dataRecord<RiderRow>(stateAddress(TableId::FieldEnemiesRiderRows) + uint32_t(int32_t(carrier.riderRow) * 8), RiderRow::kBytes);
    for (int k = 1; k >= 0; --k) {
        carrier.riders[size_t(k)] = 0;
        const uint32_t id = uint32_t(row.firstId) + uint32_t(k);
        if (!enemyDefeated(id)) continue; // exe 0x5e6420
        const uint32_t c = allocActor20(); // exe 0x5e1160: a rider (type 0x2d sub 1 variant 3)
        if (!c) continue;
        AnimatedActor& rider = animatedActorAt(c);
        auto&& params = RiderParams(animatedActorAt(c));
        rider.loopFlags |= kActorSweepGroup2;
        rider.flags = kActorLive;
        rider.type = kRiderType;
        rider.subtype = kRiderSub;
        rider.variant = kRiderVariant;
        params.mode = 0;
        rider.pos = actor.pos;
        rider.rotation = actor.rotation;
        params.mode = row.mode;
        params.count = row.count;
        params.carrier = actor.slot;
        params.trackId = uint8_t(id);
        params.pointX = row.pointX;
        params.pointZ = row.pointZ;
        encounterTrackInScriptSlot(rider, uint16_t(id)); // exe 0x5e6500
        enemyClearDefeated(id);                         // exe 0x5e63f0
        carrier.riders[size_t(k)] = rider.slot;
    }
    actor.enterState(uint8_t(CarrierState::Main));
}

// the 16.16 target {x << 16, -, z << 16} of 0x4d1b60 / 0x4d2580: the exe writes only its whole halves, the rest is what
// the hit check 0x4d2de0 (called before at the same depth) pushed there: the x fraction its saved edi (the actor's
// address + 0x10, exe 0x4d1711), the y its saved esi (the actor's address), the z fraction its saved ebx (the frame
// loop's, 0)
void pointAt(const AnimatedActor& actor, int16_t x, int16_t z, int32_t out[3]) {
    out[0] = int32_t(uint32_t(uint16_t(x)) << 16 | ((actor.at + 0x10) & 0xffff));
    out[1] = int32_t(actor.at);
    out[2] = int32_t(uint32_t(uint16_t(z)) << 16);
}
// the walks' first step (0x4d1b60 / 0x4d2580): the carry bits, the turn rate, the speed step, the brake distance
void startWalk(AnimatedActor& actor, CarrierFields& carrier, uint8_t carry) {
    carrier.carry = carry;
    actor.spin.y = 0x30;
    actor.velocity.y = 0;
    actor.acceleration.z = -0x18;
    carrier.brakeDistance = int16_t(stoppingDistance(kWalkSpeed, 0x18) >> 16);
    actor.enterStep(1);
}

// exe 0x4d1b60: walks to its target with the riders, then drops them (or picks the next)
void toTarget(AnimatedActor& actor, CarrierFields& carrier) {
    int32_t pt[3];
    pointAt(actor, carrier.targetX, carrier.targetZ, pt);
    if (actor.step == 0) startWalk(actor, carrier, kCarrierRiders);
    if (moveTo(actor, carrier, pt, kWalkSpeed) != kArrived) return;
    carrierSub(actor, CarrierSub::Unload);
    if (carrier.ridersWord != 0) return;
    next(actor, carrier);
}

// exe 0x4d1c00: drops the riders on the animation's frame 10
void unload(AnimatedActor& actor, CarrierFields& carrier) {
    if (actor.step == 0) {
        carrier.carry = kCarrierRiders;
        actor.anim = kCarrierUnload;
        actor.velocity.y = 0;
        actor.enterStep(1);
        return;
    }
    if (actor.animKey.frame == 10 && (carrier.carry & kCarrierRiders)) dropRiders(carrier);
    if (!actor.animEnded()) return;
    next(actor, carrier);
}

// exe 0x4d1c60: walks to the box, climbs onto it, faces it, lifts it and throws it
void fetch(AnimatedActor& actor, CarrierFields& carrier) {
    StaticActor& box = carriedBox(carrier);
    int32_t pt[3] = {box.pos.x, box.pos.y, box.pos.z};
    enemyGroupMark(int32_t(carrier.group), kGroupBoxTaken); // exe 0x5e6d40
    int v = 0;
    switch (FetchStep(actor.step)) {
    case FetchStep::Init:
        carrier.carry = 0;
        actor.spin.y = 0x30;
        actor.velocity.y = 0;
        actor.acceleration.z = -0x10;
        carrier.brakeDistance = int16_t(stoppingDistance(kFetchSpeed, 0x10) >> 16);
        actor.enterStep(uint8_t(FetchStep::Walk));
        [[fallthrough]];
    case FetchStep::Walk: {
        const uint32_t r = moveTo(actor, carrier, pt, kFetchSpeed);
        if (actor.variant != 2) {
            if (r == kArrived) actor.enterStep(uint8_t(FetchStep::Climb));
        } else if (r != kMoving) {
            actor.stepCounter = 1;
        }
        wallTurnIfHit(actor, carrier);
        v = boxBusy(box);
        break;
    }
    case FetchStep::Climb:
        pt[1] = int32_t(uint32_t(pt[1]) - uint32_t(kBoxHeight));
        if (moveTo3(actor, carrier, pt, kFetchSpeed) == kArrived)
            actor.enterStep(uint8_t(wholePart(pt[1]) == actor.pos.y.whole ? FetchStep::Face : FetchStep::Snap));
        if (carrier.contact & kCarrierHitFloor) {
            next(actor, carrier);
            return;
        }
        v = boxBusy(box);
        break;
    case FetchStep::Snap:
        pt[1] = int32_t(uint32_t(pt[1]) - uint32_t(kBoxHeight));
        if (snapTo(actor, pt, 0x80)) actor.enterStep(uint8_t(FetchStep::Face));
        [[fallthrough]];
    case FetchStep::Face: {
        const int d = angleDiff(uint16_t(actor.rotation.y) & 0x3ff, uint16_t(box.rotation.y) & 0x3ff);
        if (uint32_t(d + 0x30) >= 0x60u) {
            actor.rotation.y = int16_t(actor.rotation.y + (d <= 0 ? -0x30 : 0x30));
        } else {
            actor.rotation.y = int16_t(actor.rotation.y + d);
            actor.enterStep(uint8_t(actor.step + 1), actor.stepCounter);
        }
        v = boxBusy(box);
        break;
    }
    case FetchStep::Lift:
        actor.anim = kCarrierReach;
        actor.enterStep(uint8_t(FetchStep::Lifting));
        soundPlayAtPos(kSoundReach, actor.pos);
        return;
    case FetchStep::Lifting: {
        if (!actor.animEnded()) return;
        const int dy = int(box.pos.y.whole) - int(actor.pos.y.whole) - 0x1e0;
        if (boxBusy(box) == 0 && dy >= -0xa9 && dy <= 0x1f) {
            LiftBoxHolding(box).holder = kLiftBoxHolds;
            box.rotation.y = actor.rotation.y;
            carrier.carry |= kCarrierBox;
            actor.velocity.y = 0;
            actor.anim = kCarrierLift;
            actor.enterStep(uint8_t(FetchStep::Throw));
            soundPlayAtPos(kSoundLift, actor.pos);
            return;
        }
        carrierSub(actor, CarrierSub::Shrug);
        return;
    }
    case FetchStep::Throw:
        if (actor.animKey.frame >= 5) {
            actor.velocity.y -= 0x20u;
            if (actor.velocity.y <= -0x201) actor.velocity.y = -0x200;
            actor.pos.y = int32_t(actor.pos.y + int32_t(actor.velocity.y) * 0x1000);
        }
        if (actor.keyIndex == 0x11) soundPlayAtPos(kSoundThrow, actor.pos);
        if (!actor.animEnded()) return;
        if ((box.flags & kActorLive) && box.type == kLiftBoxType && LiftBoxHolding(box).carried == 0) {
            LiftBoxHolding(box).holder = kLiftBoxLetGo;
            box.velocity.y = -0x480;
            box.velocity.z = 0x200;
            box.rotation.y = actor.rotation.y;
        }
        actor.anim = kCarrierThrow;
        carrier.carry &= uint8_t(~kCarrierBox);
        actor.enterStep(uint8_t(FetchStep::Done));
        return;
    case FetchStep::Done:
        if (!actor.animEnded()) return;
        next(actor, carrier);
        actor.anim = kCarrierWalk;
        return;
    default: return;
    }
    if (v == 0) return;
    next(actor, carrier);
}

// exe 0x4d1fb0: shrugs, then the next
void shrug(AnimatedActor& actor, CarrierFields& carrier) {
    if (actor.step == 0) {
        carrier.carry = 0;
        actor.anim = kCarrierShrug;
        soundPlayAtPos(kSoundShrug, actor.pos);
        actor.velocity.y = 0;
        actor.enterStep(1);
        return;
    }
    if (!actor.animEnded()) return;
    next(actor, carrier);
    actor.anim = kCarrierWalk;
}

// exe 0x4d2010: turns away from the wall while walking until it is off it, then the next
void carrierWallTurn(AnimatedActor& actor, CarrierFields& carrier) {
    if (actor.step == 0) {
        carrier.carry = 0;
        const int16_t st = wallTurnStep(actor);
        actor.velocity.y = 0;
        actor.enterStep(1);
        actor.spin.y = st;
    }
    integrateVelocity(actor, 0, 0, actor.velocity.z);
    actor.rotation.y = int16_t(actor.rotation.y + actor.spin.y);
    if (carrier.contact & kCarrierHitWall) return;
    next(actor, carrier);
}

// the stalk's random turn rate / duration
void stalkTimers(AnimatedActor& actor, CarrierFields& carrier) {
    actor.spin.y = int16_t(stalkTurns[rngStep() & 0xf]);
    carrier.timer = stalkTicks[rngStep() & 0xf];
}

// exe 0x4d20e0: follows the player's point raised by 0x190 (variant 1: 0x258); near it, in view and grippable: reaches
// for it (a miss: a shrug), the grab
void stalk(AnimatedActor& actor, CarrierFields& carrier) {
    const Player& p = player();
    const int32_t pt[3] = {p.pos.x,
                           int32_t(uint32_t(p.pos.y) +
                                   (actor.variant != 1 ? (uint32_t(uint16_t(kStalkRaise)) << 16)
                                                       : (uint32_t(uint16_t(kStalkRaiseHigh)) << 16))),
                           p.pos.z};
    switch (StalkStep(actor.step)) {
    case StalkStep::Init:
        carrier.carry = 0;
        actor.anim = kCarrierWalk;
        stalkTimers(actor, carrier);
        actor.acceleration.z = -0x18;
        actor.velocity.y = 0;
        carrier.brakeDistance = 0;
        actor.enterStep(uint8_t(StalkStep::Follow));
        [[fallthrough]];
    case StalkStep::Follow:
        if (!(enemyGroupMarks(int32_t(carrier.group), EnemyGroupMarks::Either) & kGroupPlayerTaken) &&
            actor.variant != 1 && planarDistance(actor.pos, pt) <= 0x1ff) {
            const int16_t above[3] = {actor.pos.x.whole, wholePart(int32_t(uint32_t(actor.pos.y) + 0x1900000u)),
                                      actor.pos.z.whole};
            if (pointVisibleOrNear(above, 0) == 0 && playerGrippable()) { // exe 0x5e59a0
                actor.anim = kCarrierReach;
                actor.step = uint8_t(StalkStep::Reach);
                enemyGroupMark(int32_t(carrier.group), kGroupPlayerTaken);
                soundPlayAtPos(kSoundReach, actor.pos);
            }
        }
        break;
    case StalkStep::Reach:
        enemyGroupMark(int32_t(carrier.group), kGroupPlayerTaken);
        if (actor.animEnded()) {
            if (planarDistance(actor.pos, pt) >= 0xa0 ||
                uint32_t(int(actor.pos.y.whole) - int(wholePart(pt[1])) + 0x60) >= 0xc0u) {
                actor.step = uint8_t(StalkStep::Shrug);
            } else if (playerGrippable()) {
                carrierSub(actor, CarrierSub::Grab);
                return;
            }
        }
        break;
    case StalkStep::Shrug:
        actor.anim = kCarrierShrug;
        actor.step = uint8_t(StalkStep::Shrugging);
        soundPlayAtPos(kSoundShrug, actor.pos);
        break;
    case StalkStep::Shrugging:
        if (actor.animEnded()) {
            actor.anim = kCarrierWalk;
            actor.step = uint8_t(StalkStep::Follow);
        }
        break;
    default: break;
    }
    const uint32_t r = planarDistance(actor.pos, pt) != 0 ? moveTo3(actor, carrier, pt, kWalkSpeed)
                                                         : moveTo(actor, carrier, pt, kWalkSpeed);
    if (r != kMoving) actor.stepCounter = 1;
    carrier.timer -= 1u;
    if (carrier.timer == 0) {
        stalkTimers(actor, carrier);
        if (actor.anim == kCarrierWalk) next(actor, carrier);
    }
    wallTurnIfHit(actor, carrier);
}

// exe 0x4d23f0: grips the player (held at bones 1 / 3; flag 0x26: script 0x33), lifts it, throws it, backs off
void grab(AnimatedActor& actor, CarrierFields& carrier) {
    enemyGroupMark(int32_t(carrier.group), kGroupPlayerTaken);
    switch (GrabStep(actor.step)) {
    case GrabStep::Grip: {
        carrier.carry = kCarrierHoldsPlayer;
        actor.anim = kCarrierGrip;
        const uint32_t r = rngStep();
        actor.enterStep(uint8_t(GrabStep::Lift));
        carrier.gripPose = int8_t(((0x77777777u >> (r & 0x1f)) & 1) + 1);
        soundPlayAtPos(kSoundLift, actor.pos);
        soundPlayAtPos(kSoundGrip, actor.pos);
        setStoryFlag(kFlagCatchScene); // exe 0x510e10
        carrier.timer = 0xf;
        return;
    }
    case GrabStep::Lift:
        actor.velocity.y -= 0x20u;
        actor.prevAnim = kNoAnim;
        if (actor.velocity.y <= -0x101) actor.velocity.y = -0x100;
        actor.pos.y = int32_t(actor.pos.y + int32_t(actor.velocity.y) * 0x1000);
        carrier.timer -= 1u;
        if (carrier.timer != 0) return;
        actor.enterStep(uint8_t(GrabStep::Throw));
        return;
    case GrabStep::Throw:
        if (actor.keyIndex == 0x11) soundPlayAtPos(kSoundThrow, actor.pos);
        if (!actor.animEnded()) return;
        carrier.carry &= uint8_t(~kCarrierHoldsPlayer);
        actor.anim = kCarrierThrow;
        carrier.gripPose = 0;
        actor.enterStep(uint8_t(GrabStep::Back));
        return;
    case GrabStep::Back:
        moveAlongYaw(actor.pos, angleOf(player().pos, actor.pos), 0, 0, 0x180);
        if (!actor.animEnded()) return;
        next(actor, carrier);
        return;
    default: return;
    }
}

// exe 0x4d2580: walks home, then freed (state 3)
void walkHome(AnimatedActor& actor, CarrierFields& carrier) {
    int32_t pt[3];
    pointAt(actor, carrier.homeX, carrier.homeZ, pt);
    if (actor.step == 0) startWalk(actor, carrier, 0);
    if (moveTo(actor, carrier, pt, kWalkSpeed) == kMoving) return;
    actor.enterState(uint8_t(CarrierState::Gone));
}

// exe 0x4d2610: flinches (the box and the riders let go), backing off
void flinch(AnimatedActor& actor, CarrierFields& carrier) {
    if (actor.step != 0) {
        if (actor.animEnded()) next(actor, carrier);
    } else {
        const uint8_t f = carrier.carry;
        if (f & kCarrierBox) dropBox(actor, carrier);
        if (f & kCarrierRiders) dropRiders(carrier);
        carrier.carry = kCarrierBusy;
        actor.anim = kCarrierHurt;
        carrier.damage = 0;
        actor.enterStep(1);
    }
    actor.velocity.z += 0x18u;
    if (actor.velocity.z >= 0) actor.velocity.z = 0;
    integrateVelocity(actor, 0, 0, actor.velocity.z);
}

// exe 0x4d26a0: dying -- the box and the riders let go, the fall box, a defeat counted; it spins and falls (smoke every
// 4 ticks) until the animation's frame 0x15 or a floor: a blast and a knocked-down Birdbot, freed
void dying(const PlayerEnv& env, AnimatedActor& actor, CarrierFields& carrier) {
    if (actor.subState != 0) {
        if (actor.animKey.frame > 0x15 || (carrier.contact & kCarrierHitFloor)) {
            const uint32_t b = allocActor60(0); // exe 0x5e1300: a blast (the global static type 7)
            if (b) {
                StaticActor& blast = staticActorAt(b);
                auto&& params = BlastParams(actorHeaderAt(b));
                blast.flags = kActorLive;
                blast.type = typeByte(GlobalStaticType::Blast);
                blast.loopFlags |= kActorSweepGroup2;
                blast.variant = 0;
                blast.pos = actor.pos;
                params.size = 0xc0;
                params.attack = 4;
            }
            const uint32_t c = allocActor20(); // exe 0x5e1160: the knocked-down Birdbot (type 0x2d sub 0)
            if (c) {
                AnimatedActor& bot = animatedActorAt(c);
                bot.flags = kActorLive;
                bot.loopFlags |= kActorSweepGroup2;
                bot.type = kRiderType;
                bot.subtype = 0;
                bot.variant = kDownedBirdbotVariant;
                bot.params[0] = 0;
                bot.pos = actor.pos;
                bot.rotation.y = actor.rotation.y;
            }
            freeActor(actor);
            return;
        }
    } else {
        const uint8_t f = carrier.carry;
        if (f & kCarrierBox) dropBox(actor, carrier);
        if (f & kCarrierRiders) dropRiders(carrier);
        carrier.carry = kCarrierBusy;
        actor.prevAnim = kNoAnim;
        actor.anim = kCarrierHurt;
        carrier.fallYaw = actor.rotation.y;
        actor.subState = 1;
        actor.collisionBox.pointAt(carrierFallBox.stateAddress());
        countDefeat();
    }
    actor.velocity.y += 0x10u;
    actor.rotation.y += 0x18u;
    actor.rotation.z += 0x18u;
    moveAlongYaw(actor.pos, carrier.fallYaw, 0, actor.velocity.y, actor.velocity.z); // exe 0x5e36f0
    if (!(carrier.timer & 3)) {
        const uint32_t e = allocActor60(kActorStageClass); // exe 0x5e12f0: smoke (the stage's static type 3 variant 0)
        if (e) {
            StaticActor& smoke = staticActorAt(e);
            auto&& params = SmokeParams(actorHeaderAt(e));
            smoke.flags = kActorLive;
            smoke.type = kSmokeType;
            smoke.loopFlags |= kActorSweepGroup2;
            smoke.variant = 0;
            smoke.pos = actor.pos;
            params.driftX = 0;
            params.driftZ = 0;
            params.life = 4;
        }
    }
    carrier.timer += 1u;
    carrierCollide(env, actor, carrier);
}

// exe 0x4d1b10
void carrierMain(const PlayerEnv& env, AnimatedActor& actor, CarrierFields& carrier) {
    const int h = carrierHurt(actor, carrier);
    if (h == 1) {
        carrierSub(actor, CarrierSub::Flinch);
    } else if (h == 2) {
        actor.enterState(uint8_t(CarrierState::Dying));
        dying(env, actor, carrier);
        return;
    }
    switch (CarrierSub(actor.subState)) {
    case CarrierSub::ToTarget: toTarget(actor, carrier); break;
    case CarrierSub::Unload: unload(actor, carrier); break;
    case CarrierSub::Fetch: fetch(actor, carrier); break;
    case CarrierSub::Shrug: shrug(actor, carrier); break;
    case CarrierSub::WallTurn: carrierWallTurn(actor, carrier); break;
    case CarrierSub::Stalk: stalk(actor, carrier); break;
    case CarrierSub::Grab: grab(actor, carrier); break;
    case CarrierSub::Home: walkHome(actor, carrier); break;
    case CarrierSub::Flinch: flinch(actor, carrier); break;
    default: notPortedAt(kEnemyPort, "0x4d1700 state 1 sub-state (table 0x8953b8 has 9)", actor.subState);
    }
    carrierCollide(env, actor, carrier);
}

// exe 0x4d1700
void boxCarrier(const PlayerEnv& env, AnimatedActor& actor) {
    auto&& carrier = classState<CarrierFields>(actor);
    actor.prevAnim = actor.anim;
    actor.tint = kTintNeutral;
    actor.savePreviousPose();
    actor.savePreviousContact();
    switch (CarrierState(actor.state)) {
    case CarrierState::Bind: carrierBind(actor, carrier); break;
    case CarrierState::Main: carrierMain(env, actor, carrier); break;
    case CarrierState::Dying: dying(env, actor, carrier); break;
    case CarrierState::Gone: freeActor(actor); break; // exe 0x468fa0
    default: notPortedAt(kEnemyPort, "0x4d1700 state (table 0x8953a8 has 4)", actor.state);
    }
    if (!(actor.flags & kActorLive)) return;
    if (actor.anim != actor.prevAnim)
        animSetPose(actor, actor.anim, 0); // exe 0x5e1c30
    else
        animStepPose(actor); // exe 0x5e1e80
    if ((gameState().frame & 1) && carrier.damage != 0) carrier.damage -= 1u;
    if (actor.anim == kCarrierWalk) {
        if (carrier.footstepTicks != 0) {
            carrier.footstepTicks -= 1u;
        } else {
            soundPlayAtPos(kSoundStep, actor.pos);
            carrier.footstepTicks = 0x15;
        }
    }
    const uint8_t f = carrier.carry;
    if (f & kCarrierRiders) { // the riders at bones 4 / 2 (the offsets [0x895358] / [0x895350], their bone after them)
        for (int k = 1; k >= 0; --k) {
            const uint8_t sl = carrier.riders[size_t(k)];
            if (sl == 0) continue;
            AnimatedActor& rider = animatedPool()[size_t(sl - kAnimatedClass)];
            if (!(rider.flags & kActorLive)) {
                carrier.riders[size_t(k)] = 0;
                continue;
            }
            const BoneOffset off = dataRecord<BoneOffset>(stateAddress(kRiderOffsets[k]), BoneOffset::kBytes);
            const int16_t v[3] = {off.x, off.y, off.z};
            int16_t o[3];
            bonePointOffset(actor, uint32_t(int32_t(off.bone)), v, o); // exe 0x5e1970
            rider.pos.x.whole = o[0];
            rider.pos.y.whole = o[1];
            rider.pos.z.whole = o[2];
            rider.rotation.y = actor.rotation.y;
        }
    }
    if ((f & kCarrierBox) && (carriedBox(carrier).flags & kActorLive))
        holdBetweenHands(actor, carriedBox(carrier), dataRecord<BoneOffset>(stateAddress(TableId::FieldEnemiesHoldOffsets), BoneOffset::kBytes));
    if (f & kCarrierHoldsPlayer)
        holdBetweenHands(actor, player(), dataRecord<BoneOffset>(uint32_t(stateAddress(TableId::FieldEnemiesHoldOffsets) + int32_t(carrier.gripPose.get()) * 8), BoneOffset::kBytes));
    carrier.hitBox.result = 0;
    carrier.hitBox.previousX = actor.prevPos.x.whole;
    carrier.hitBox.previousZ = actor.prevPos.z.whole;
    const int16_t pos[3] = {actor.pos.x.whole, actor.pos.y.whole, actor.pos.z.whole};
    hitListSubmit(pos, carrier.hitBox,
                  hitOwner(actor.slot) | hitGroup(kHitGroupBodies) | kHitContactMotion | hitRadius(0x40),
                  animatedStats(actor.type).attack | kHitAttackSolid); // exe 0x5e4120
    // exe 0x5f20f0(a, 0x40, 0x100, 0): the shadow (render output)
}

} // namespace

void critterUpdate(const PlayerEnv& env, uint32_t actor) { critter(env, animatedActorAt(actor)); }
void boxCarrierUpdate(const PlayerEnv& env, uint32_t actor) { boxCarrier(env, animatedActorAt(actor)); }

} // namespace od::port
