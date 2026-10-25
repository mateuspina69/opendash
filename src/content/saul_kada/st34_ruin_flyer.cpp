// The ST34 ruin's flyer (sub 1, story 12+; pool 0x20 type 0x67 sub 1, exe 0x4edff0; hypothesis names). The same exe
// family as the Manda exam's ambush flyer (exe 0x4ed1a0, content/manda/exam_enemies_b): they share the walls' test
// (exe 0x4eee30), the shots (exe 0x4eeec0) and the stagger (exe 0x4eecc0).
// States [0x8966ec]: 0 bind 0x4ee0b0, 1 run 0x4ee200, 2 freed 0x468fa0.
// The run: its stagger gauge decays on odd frames; a hit of the player's (not dying) alerts it, flashes it, takes its
// hit points (dead: dying; else, in a group and not yet staggered, the gauge past its row's limit: the stagger); else
// the player's lift check marks it. Then its sub-state [0x8966fc]:
//   0 patrol 0x4ee420 (steps 0 / 1: forward until 0x100 from home, turned back within 0x80 of it; step 2: closing in on
//     the player at his height - 0x140 to within 0x340 -> the hover; the patrol closes in once the player is within
//     0x400 of home or it is alerted),
//   1 hover 0x4ee6a0 (facing the player, two shots at its animation's key 4 while facing him within 0x40; the player
//     slowed -> the dive; the player 0x400 away -> home (him far from home, not alerted) or closing in),
//   2 dive 0x4ee7c0 (at his height - 0xc0 until he is out of its 0x80 cone, levelled out 0x14 ticks, braking, 0x2d ticks
//     -> closing in or home), 3 home 0x4eeaf0 (turned off the walls it hits, braking within 0x400 -> the patrol),
//   4 stagger 0x4eecc0 (-> the patrol's step 3), 5 dying 0x4eecf0 (an explosion, a defeat counted, its drops,
//   blinking -> freed), 6 the first rise 0x4eedd0 (0x28 ticks up 0x80, then home = here).
// No shadow call (the OT draw is render output).
#include "content/saul_kada/st34_ruin_flyer.hpp"
#include "content/saul_kada/st34_ruin_flyer_state.hpp"

#include "engine/data_source.hpp"
#include "game/actor_animation.hpp"
#include "game/actor_model.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actor_stats.hpp"
#include "game/angles.hpp"
#include "game/enemy_motion.hpp"
#include "game/audio_logic.hpp"
#include "game/carry.hpp"
#include "game/distances.hpp"
#include "game/encounter.hpp"
#include "game/explosions.hpp"
#include "game/vectors.hpp"
#include "game/game_state.hpp"
#include "game/global_class_types.hpp"
#include "game/hit_list.hpp"
#include "game/hit_points.hpp"
#include "game/pickups.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/story.hpp"

namespace od::port {

using namespace st34_ruin_flyer_state;

namespace {

enum class FlyerState : uint8_t { Bind = 0, Run = 1, Free = 2 };
enum class FlyerSub : uint8_t { Patrol = 0, Hover = 1, Dive = 2, Home = 3, Stagger = 4, Dying = 5, Rise = 6 };
enum class PatrolStep : uint8_t { Out = 0, Back = 1, CloseIn = 2, Recover = 3 };
enum class DiveStep : uint8_t { Start = 0, Wind = 1, Dive = 2, Level = 3, Brake = 4, Rest = 5 };
enum class FlyerAnim : uint8_t { Fly = 0, Hover = 1, DiveStart = 2, Dive = 3, Brake = 4, Stagger = 5, Turn = 6 };
constexpr uint8_t kFlyerLifted = 0x01, kFlyerCollideAsShot = 0x02, kFlyerDecodeOnly = 0x04, kFlyerStaggered = 0x08;
constexpr uint8_t kFlyerDying = 0x10, kFlyerInGroup = 0x20, kFlyerWallTurn = 0x40;
constexpr uint8_t kFlyerContactWall = 0x01;
constexpr uint8_t kNoAnim = 0xff;
constexpr uint8_t kFlyerShotType = 0x4c;
constexpr uint8_t kShotSlot = 0x10; // the slot id it collides as (a shot's layers)
constexpr uint16_t kHurtSound = 0x8d, kFlapSound = 0x200, kDiveFlapSound = 0x206;
constexpr uint8_t kEventFlap = 0x01;
constexpr uint16_t kExplosionTint = 0x421f;
// the variant's row index (by variant: {0, 1, 1, 0}) for the bind's model and hit points / for the drops
const DataTable<ScalarTable<uint8_t, 4>> bindRows{TableId::St34RuinFlyerBindRows}, dropRows{TableId::St34RuinFlyerDropRows};

// its row: (spawn byte + 2 * the row index) * 16 into its type's rows
AnimatedActorStats flyerStats(const AnimatedActor& actor, uint32_t rowIndex) {
    return animatedStatsAt(statTableRow(actorStatTables().animated, actor.type) +
                              ((uint32_t(actor.spawnByte) + rowIndex * 2u) << 4));
}
void sound(uint16_t id, const AnimatedActor& actor) { soundPlayAtPos(id, actor.pos); } // exe 0x5d5380
int angleToPlayer(const AnimatedActor& actor) { return angleOf(player().pos, actor.pos); } // exe 0x5e3bc0(player, it)
void turnTo(AnimatedActor& actor, int angle) { turnYawTo(actor.rotation.y, uint32_t(angle), 0x20); } // exe 0x5e3df0
bool facingPlayerWithin(const AnimatedActor& actor, int tolerance) {
    return facingWithin(actor, player().pos.x, player().pos.z, tolerance);
}
// its home as a 16.16 point (the exe's stack copy)
void homePoint(const FlyerFields& flyer, int32_t out[3]) {
    out[0] = int32_t(uint32_t(int32_t(flyer.home.x)) << 16);
    out[1] = int32_t(uint32_t(int32_t(flyer.home.y)) << 16);
    out[2] = int32_t(uint32_t(int32_t(flyer.home.z)) << 16);
}
int playerFromHome(const FlyerFields& flyer) {
    int32_t home[3];
    homePoint(flyer, home);
    return planarDistance(player().pos, home);
}
// the player's position with its integer y + dy (the exe's stack copy)
void playerAbove(int dy, int32_t out[3]) {
    const Player& p = player();
    out[0] = p.pos.x;
    out[1] = int32_t((uint32_t(p.pos.y) & 0xffffu) | uint32_t(uint16_t(uint16_t(p.pos.y.whole) + uint16_t(dy))) << 16);
    out[2] = p.pos.z;
}
void positionValues(const AnimatedActor& actor, int32_t out[3]) {
    out[0] = actor.pos.x;
    out[1] = actor.pos.y;
    out[2] = actor.pos.z;
}
// the move along its yaw and its pitch (the pitch put in the rotation for exe 0x5e3810, then cleared)
void flyForward(AnimatedActor& actor, const FlyerFields& flyer) {
    actor.rotation.x = flyer.pitch;
    moveAlongRotation(actor.pos, actor.rotation, 0, 0, actor.velocity.z); // exe 0x5e3810
    actor.rotation.x = 0;
}
// the pitch stepped (0x10) toward the point, by the 16-bit sign of the difference
void pitchToward(const AnimatedActor& actor, FlyerFields& flyer, const int32_t point[3]) {
    int32_t at[3];
    positionValues(actor, at);
    const uint16_t c = uint16_t(0x800 - pitchOf(point, at) - int(uint16_t(flyer.pitch)));
    flyer.pitch = int16_t(uint16_t(uint16_t(flyer.pitch) + (int16_t(c) > 0 ? 0x10u : 0xfff0u)));
}
// its forward speed: above `limit` less its braking, else `floor`
void speedUp(AnimatedActor& actor, int16_t limit, int16_t floor) {
    if (actor.velocity.z > limit) actor.velocity.z = int16_t(uint16_t(actor.velocity.z) - uint16_t(actor.acceleration.z));
    else actor.velocity.z = floor;
}
// its forward speed back toward 0 by its braking
void brake(AnimatedActor& actor) {
    actor.velocity.z = int16_t(uint16_t(uint16_t(actor.velocity.z) + uint16_t(actor.acceleration.z)));
}
// exe 0x4eee30: the walls (as the shot slot while kFlyerCollideAsShot); never nearer than 0x20 above the ground
uint32_t collide(const PlayerEnv& env, AnimatedActor& actor, FlyerFields& flyer) {
    uint8_t slot = 0;
    if (flyer.flags & kFlyerCollideAsShot) {
        slot = actor.slot;
        actor.slot = kShotSlot;
    }
    uint32_t result = 0;
    if (int32_t(actorTestMove(env, actor)) < 0) { // exe 0x40f570
        result = actorPushOut(env, actor);         // exe 0x410960
        if (int32_t(result) < 0) actor.pos = actor.prevPos;
        if (result & 0xf00) flyer.contact |= kFlyerContactWall;
    }
    const int ground = int(actorGround(env, actor)); // exe 0x40f190
    if (ground - actor.pos.y.whole < 0x20) actor.pos.y.whole = int16_t(ground - 0x20);
    if (flyer.flags & kFlyerCollideAsShot) actor.slot = slot;
    return result;
}
// exe 0x4eeec0: two shots (the stage's static type 0x4c), one pushed 0x280 to each side
void shots(const AnimatedActor& actor) {
    for (int k = 0; k < 2; ++k) {
        const uint32_t slot = allocActor60(kActorStageClass); // exe 0x5e12f0
        if (!slot) continue;
        StaticActor& shot = staticActorAt(slot);
        shot.flags = kActorLive;
        shot.loopFlags |= kActorSweepGroup2;
        shot.type = kFlyerShotType;
        shot.variant = 0;
        shot.pos = actor.pos;
        shot.rotation.y = actor.rotation.y;
        integrateVelocity(shot, k == 0 ? 0x280 : -0x280, 0, 0); // exe 0x5e3690
    }
}

// ---- the sub-states ----------------------------------------------------------------------------------------------
// the end of the patrol's steps out / back: the player within 0x400 of home, or alerted -> closing in
void patrolWake(AnimatedActor& actor, const FlyerFields& flyer) {
    if (playerFromHome(flyer) < 0x400 || flyer.alerted != 0) actor.step = uint8_t(PatrolStep::CloseIn);
}
// exe 0x4ee420: the patrol around home / closing in on the player
void patrol(const PlayerEnv& env, AnimatedActor& actor, FlyerFields& flyer) {
    const int16_t home[3] = {flyer.home.x, flyer.home.y, flyer.home.z};
    switch (PatrolStep(actor.step)) {
    case PatrolStep::Out: { // exe 0x4ee5fe
        integrateVelocity(actor, 0, 0, actor.velocity.z); // exe 0x5e3690
        const int16_t at[3] = {actor.pos.x.whole, actor.pos.y.whole, actor.pos.z.whole};
        if (planarDistance16(home, at) > 0x100) ++actor.step; // exe 0x5e3b40
        collide(env, actor, flyer);
        patrolWake(actor, flyer);
        return;
    }
    case PatrolStep::Back: { // exe 0x4ee53e
        const int16_t at[3] = {actor.pos.x.whole, actor.pos.y.whole, actor.pos.z.whole};
        turnTo(actor, int16_t(angleOf(home, at))); // exe 0x5e3c00
        int32_t homeAt[3];
        homePoint(flyer, homeAt);
        if (facingWithin(actor, homeAt[0], homeAt[2], 0x80)) actor.step = uint8_t(PatrolStep::Out);
        integrateVelocity(actor, 0, 0, actor.velocity.z);
        collide(env, actor, flyer);
        patrolWake(actor, flyer);
        return;
    }
    case PatrolStep::CloseIn: {
        turnTo(actor, int16_t(angleToPlayer(actor)));
        int32_t above[3];
        playerAbove(-0x140, above);
        pitchToward(actor, flyer, above);
        if (planarDistance(player().pos, actor.pos) > 0x340) {
            speedUp(actor, -0x180, -0x180);
        } else if (actor.velocity.z < 0) {
            brake(actor);
        } else {
            actor.velocity.z = 0;
            actor.subState = uint8_t(FlyerSub::Hover);
            actor.step = 0;
        }
        flyForward(actor, flyer);
        collide(env, actor, flyer);
        return;
    }
    default: return;
    }
}
// exe 0x4ee6a0: hovering by the player
void hover(AnimatedActor& actor, FlyerFields& flyer) {
    switch (actor.step) {
    case 0:
        actor.anim = uint8_t(FlyerAnim::Hover);
        actor.step = 1;
        return;
    case 1:
        if (planarDistance(player().pos, actor.pos) < 0x400) {
            if (facingPlayerWithin(actor, 0x40)) {
                if (actor.keyIndex == 4) shots(actor); // exe 0x4eeec0
            } else {
                turnTo(actor, angleToPlayer(actor));
            }
            if (uint8_t(player().status) & (kStatusSlowed | kStatusSlowedFloor)) {
                actor.subState = uint8_t(FlyerSub::Dive);
                actor.step = 0;
                flyer.alerted = 0;
            }
            return;
        }
        if (playerFromHome(flyer) >= 0x400 && flyer.alerted == 0) {
            actor.anim = uint8_t(FlyerAnim::Fly);
            actor.subState = uint8_t(FlyerSub::Home);
            actor.step = 0;
            return;
        }
        actor.anim = uint8_t(FlyerAnim::Fly);
        actor.subState = uint8_t(FlyerSub::Patrol);
        actor.step = uint8_t(PatrolStep::CloseIn);
        return;
    default: return;
    }
}
// exe 0x4ee7c0: the dive (its steps [0x4eead4])
void dive(const PlayerEnv& env, AnimatedActor& actor, FlyerFields& flyer) {
    switch (DiveStep(actor.step)) {
    case DiveStep::Start:
        actor.anim = uint8_t(FlyerAnim::DiveStart);
        ++actor.step;
        break;
    case DiveStep::Wind:
        if (actor.animEnded()) {
            actor.anim = uint8_t(FlyerAnim::Dive);
            ++actor.step;
        }
        break;
    case DiveStep::Dive: {
        turnTo(actor, int16_t(angleToPlayer(actor)));
        int32_t above[3];
        playerAbove(-0xc0, above);
        pitchToward(actor, flyer, above);
        speedUp(actor, 0x200, -0x200);
        flyForward(actor, flyer);
        if (!facingPlayerWithin(actor, 0x80)) {
            ++actor.step;
            flyer.timer = 0x14;
        }
        break;
    }
    case DiveStep::Level:
        speedUp(actor, 0x200, -0x200);
        flyer.pitch = int16_t(uint16_t(uint16_t(flyer.pitch) + (flyer.pitch < 0 ? 0x10u : 0xfff0u)));
        flyForward(actor, flyer);
        if (flyer.timer != 0) {
            flyer.timer -= 1u;
        } else {
            ++actor.step;
            actor.anim = uint8_t(FlyerAnim::Brake);
            actor.acceleration.x = 4;
        }
        break;
    case DiveStep::Brake:
        if (actor.velocity.z < 0) {
            brake(actor);
        } else {
            ++actor.step;
            actor.velocity.z = 0;
            flyer.timer = 0x2d;
        }
        flyForward(actor, flyer);
        break;
    case DiveStep::Rest:
        if (actor.animEnded() && actor.anim != uint8_t(FlyerAnim::Fly)) actor.anim = uint8_t(FlyerAnim::Fly);
        turnTo(actor, int16_t(angleToPlayer(actor)));
        if (flyer.timer != 0) {
            flyer.timer -= 1u;
            break;
        }
        {
            const int d = playerFromHome(flyer);
            actor.anim = uint8_t(FlyerAnim::Turn);
            if (d < 0x400) {
                actor.subState = uint8_t(FlyerSub::Patrol);
                actor.step = uint8_t(PatrolStep::CloseIn);
            } else {
                actor.subState = uint8_t(FlyerSub::Home);
                actor.step = 0;
            }
        }
        break;
    default: break;
    }
    collide(env, actor, flyer); // exe 0x4eee30
}
// exe 0x4eeaf0: back home, turned off the walls it hits
void goHome(const PlayerEnv& env, AnimatedActor& actor, FlyerFields& flyer) {
    int32_t home[3];
    homePoint(flyer, home);
    int32_t at[3];
    switch (actor.step) {
    case 0:
        positionValues(actor, at);
        turnTo(actor, angleOf(home[0], home[2], at[0], at[2]));
        if (facingWithin(actor, home[0], home[2], 0x80)) {
            actor.step = 1;
            flyer.pitch = 0;
            actor.velocity.z = int16_t(0xfe80);
        }
        return;
    case 1: {
        if (flyer.flags & kFlyerWallTurn) {
            if (turnYawTo(actor.rotation.y, uint32_t(int32_t(flyer.reflectedYaw)), 0x20) == 0) // exe 0x5e3df0
                flyer.flags &= uint8_t(~kFlyerWallTurn);
        } else {
            positionValues(actor, at);
            turnTo(actor, angleOf(home[0], home[2], at[0], at[2]));
            const int c = 0x800 - pitchOf(home, at) - flyer.pitch; // exe 0x5e3ca0
            flyer.pitch = int16_t(uint16_t(uint16_t(flyer.pitch) + (c > 0 ? 0x10u : 0xfff0u)));
        }
        if (planarDistance(actor.pos, home) < 0x400) {
            if (actor.velocity.z < -0x80) {
                brake(actor);
            } else {
                actor.velocity.z = int16_t(0xff80);
                actor.subState = uint8_t(FlyerSub::Patrol);
                actor.step = uint8_t(PatrolStep::Out);
            }
        }
        flyForward(actor, flyer);
        const uint32_t result = collide(env, actor, flyer);
        if (flyer.contact & kFlyerContactWall) {
            const int16_t yaw = actor.rotation.y;
            reflectRotationOffWalls(actor.rotation, result); // exe 0x410740
            flyer.reflectedYaw = actor.rotation.y;
            actor.rotation.y = yaw;
            flyer.flags |= kFlyerWallTurn;
        }
        return;
    }
    default: return;
    }
}
// exe 0x4eecc0: the stagger animation, then the patrol's recovery step
void stagger(AnimatedActor& actor, FlyerFields& flyer) {
    if (!actor.animEnded()) return;
    actor.anim = uint8_t(FlyerAnim::Turn);
    actor.subState = uint8_t(FlyerSub::Patrol);
    actor.step = uint8_t(PatrolStep::Recover);
    actor.rotation.x = 0;
    flyer.pitch = 0;
}
// exe 0x4eecf0: dying: an explosion at bone 0, a defeat counted, its drops; blinking, then freed
void dying(AnimatedActor& actor, FlyerFields& flyer) {
    switch (actor.step) {
    case 0: {
        const ShortVec4* bone = bonePointOrNull(actor, 0); // exe 0x5e1810(actor, 0)
        if (const uint32_t slot = allocActor60(0)) {        // exe 0x5e1300
            if (bone == 0) notPorted(kStoryPort, "0x4eecf0: exe 0x5e1810 returned null (dereferenced)");
            StaticActor& explosion = staticActorAt(slot);
            const ShortVec4& point = *bone;
            explosion.flags = kActorLive;
            explosion.loopFlags |= kActorSweepGroup2;
            explosion.type = typeByte(GlobalStaticType::Explosion);
            explosion.variant = 0;
            explosion.pos.x.whole = point.x;
            explosion.pos.y.whole = point.y;
            explosion.pos.z.whole = point.z;
            ExplosionParams params(actorHeaderAt(slot));
            params.spread = 0x60;
            params.flashes = 0x1e;
        }
        ++actor.step;
        countDefeat(); // exe 0x5e53e0
        spawnDrops(actor.pos, flyerStats(actor, dropRows[actor.variant]).dropTable); // exe 0x5f1270
        return;
    }
    case 1:
        if (flyer.timerLow & 1) actor.tint = kExplosionTint;
        if (flyer.timer == 0) actor.state = uint8_t(FlyerState::Free);
        else flyer.timer -= 1u;
        return;
    default: return;
    }
}
// exe 0x4eedd0: the first rise, then home = here
void rise(AnimatedActor& actor, FlyerFields& flyer) {
    if (flyer.timer != 0) {
        integrateVelocity(actor, 0, -0x80, 0); // exe 0x5e3690
        flyer.timer -= 1u;
        return;
    }
    actor.subState = uint8_t(FlyerSub::Patrol);
    flyer.risesFirst = 0;
    flyer.home.x = actor.pos.x.whole;
    flyer.home.y = actor.pos.y.whole;
    flyer.home.z = actor.pos.z.whole;
    flyer.homeYaw = actor.rotation.y;
}

// ---- the states ------------------------------------------------------------------------------------------------
// exe 0x4ee0b0: the model and the hit points of its row, its boxes, its speeds, home = here, its enemy group
void bind(AnimatedActor& actor, FlyerFields& flyer) {
    if (!bindActorModel(actor, bindRows[actor.variant])) { // exe 0x5e0bc0
        freeActor(actor);                                 // exe 0x5e1490
        return;
    }
    actor.flags |= kActorDrawn;
    actor.anim = uint8_t(FlyerAnim::Fly);
    actor.prevAnim = kNoAnim;
    const uint16_t hp = flyerStats(actor, bindRows[actor.variant]).hitPoints;
    actor.maxHitPoints = int16_t(hp);
    actor.hitPoints = int16_t(hp);
    actor.velocity.setXY(0, 0);
    actor.collisionBox.pointAt(TableId::St34RuinFlyerBox);
    actor.interactionBox.pointAt(TableId::St34RuinFlyerLiftBox);
    actor.interaction = kInteractLift;
    actor.velocity.z = int16_t(0xff80);
    actor.rotation.x = 0;
    actor.rotation.z = 0;
    actor.velocity.y = 0;
    actor.acceleration.y = 0x10;
    actor.acceleration.x = 0x10;
    actor.acceleration.z = 0x18;
    actor.airborne = 1;
    actor.onPlatform = 0;
    actor.platform = nullptr;
    actor.actorHit = 0;
    flyer.hitBox.result = 0;
    flyer.flags = 0;
    flyer.timer = 0;
    flyer.pitch = 0;
    flyer.home.x = actor.pos.x.whole;
    flyer.home.y = actor.pos.y.whole;
    flyer.home.z = actor.pos.z.whole;
    flyer.homeYaw = actor.rotation.y;
    flyer.gauge = 0;
    flyer.group = uint8_t(enemyGroupFindFor(actor, 1)); // exe 0x5e6cd0(actor, 1)
    actor.state = uint8_t(FlyerState::Run);
    if (flyer.risesFirst == 0) {
        actor.subState = uint8_t(FlyerSub::Patrol);
        actor.step = 0;
        return;
    }
    actor.subState = uint8_t(FlyerSub::Rise);
    actor.velocity.y = int16_t(0xff80);
    flyer.timer = 0x28;
    actor.step = 0;
}
// exe 0x4ee200
void run(const PlayerEnv& env, AnimatedActor& actor, FlyerFields& flyer) {
    const AnimatedActorStats stats = flyerStats(actor, actor.variant);
    flyer.contact = 0;
    if (flyer.gauge != 0 && (gameState().frame & 1)) flyer.gauge -= 1u;
    if ((flyer.hitBox.result & kHitAttackByPlayer) && !(flyer.flags & kFlyerDying)) {
        const uint16_t hp = uint16_t(actor.hitPoints);
        flyer.alerted = 1;
        actor.tint = kTintFlash;
        sound(kHurtSound, actor);
        if (hitPointsAdd(actor, -int(flyer.hitBox.result & kHitDamageMask)) != 0) { // exe 0x5e52f0
            actor.subState = uint8_t(FlyerSub::Dying);
            actor.step = 0;
            flyer.timer = 0x1e;
            flyer.flags |= kFlyerDying;
        } else {
            const uint8_t flags = flyer.flags;
            if (!(flags & kFlyerStaggered) && (flags & kFlyerInGroup)) {
                if (int16_t(flyer.gauge) > int(stats.hitSize)) { // the row's byte 9: its stagger limit
                    actor.subState = uint8_t(FlyerSub::Stagger);
                    actor.step = 0;
                    actor.anim = uint8_t(FlyerAnim::Stagger);
                    flyer.flags = uint8_t(flags | kFlyerStaggered);
                } else {
                    flyer.gauge = uint16_t(uint16_t(flyer.gauge) - uint16_t(actor.hitPoints) + hp);
                }
            }
        }
    } else if (playerLiftTargetCheck(actor, 0) && !(flyer.flags & kFlyerLifted)) { // exe 0x573a60
        flyer.flags |= kFlyerLifted;
    }
    switch (FlyerSub(actor.subState)) { // [0x8966fc]
    case FlyerSub::Patrol: patrol(env, actor, flyer); break;
    case FlyerSub::Hover: hover(actor, flyer); break;
    case FlyerSub::Dive: dive(env, actor, flyer); break;
    case FlyerSub::Home: goHome(env, actor, flyer); break;
    case FlyerSub::Stagger: stagger(actor, flyer); break;
    case FlyerSub::Dying: dying(actor, flyer); break;
    case FlyerSub::Rise: rise(actor, flyer); break;
    default: notPortedAt(kStoryPort, "0x4ee200 sub-state (table overrun)", actor.subState);
    }
    if (actor.animKey.events & kEventFlap)
        sound(actor.anim == uint8_t(FlyerAnim::Dive) ? kDiveFlapSound : kFlapSound, actor);
    flyer.hitBox.previousX = actor.prevPos.x.whole;
    flyer.hitBox.previousZ = actor.prevPos.z.whole;
    const uint8_t flags = flyer.flags;
    flyer.hitBox.result = 0;
    if ((flags & kFlyerDying) && flyer.timer == 0) return;
    const int16_t pos[3] = {actor.pos.x.whole, int16_t(uint16_t(actor.pos.y.whole) - 0x2eu), actor.pos.z.whole};
    const uint32_t shape = hitOwner(actor.slot) | hitHeightLevel(stats.hitHeight) | hitGroup(kHitGroupBodies) |
                           hitRadius(0x94);
    if (!(flags & kFlyerDying)) {
        hitListSubmit(pos, flyer.hitBox, shape, uint32_t(stats.attack) | kHitAttackSolid); // exe 0x5e4120
        return;
    }
    hitListSubmit(pos, flyer.hitBox, shape, uint32_t(stats.attack) | kHitAttackBump); // exe 0x5e4120
    flyer.timer -= 1u;
}
void ruinFlyer(const PlayerEnv& env, AnimatedActor& actor) { // exe 0x4edff0
    FlyerFields& flyer = classState<FlyerFields>(actor);
    actor.prevAnim = actor.anim;
    actor.tint = kTintNeutral;
    actor.prevPos = actor.pos;
    actor.prevRotation = actor.rotation;
    actor.savePreviousContact();
    switch (FlyerState(actor.state)) { // [0x8966ec]
    case FlyerState::Bind: bind(actor, flyer); break;
    case FlyerState::Run: run(env, actor, flyer); break;
    case FlyerState::Free: freeActor(actor); break; // 0x468fa0
    default: notPortedAt(kStoryPort, "0x4edff0 state (table overrun)", actor.state);
    }
    if (actor.anim == actor.prevAnim) {
        if (!(flyer.flags & kFlyerDecodeOnly)) animStep(actor); // exe 0x5e1ea0
        else animDecodeCurrent(actor);                          // exe 0x5e1f30
    } else {
        animSet(actor, actor.anim, 0); // exe 0x5e1c50
    }
    animPose(actor); // exe 0x5e2d90
}

} // namespace

void ruinFlyerUpdate(const PlayerEnv& env, uint32_t actor) { ruinFlyer(env, animatedActorAt(actor)); }

} // namespace od::port
