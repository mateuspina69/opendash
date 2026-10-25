// The walking reaverbot class 0x451c00 (see reaverbot_walker.hpp). States [0x88eb6c]: 0 0x451d70 spawn, 1 0x451f70
// main (18 sub-states [0x88ebb8]), 2 0x4530c0 carried (sub-states [0x88ec10]: 0x453130 at the hands, 0x453230
// thrown / dropped), 3 0x453380 dying, 4 0x453520 sinking. Its animations are model-bank indices keyed by the
// behaviour.
#include "game/actor_state.hpp"
#include "game/reaverbot_walker_state.hpp"
#include "game/field_alias.hpp"
#include "game/reaverbot_walker.hpp"

#include "engine/data_source.hpp"
#include "game/actor_animation.hpp"
#include "game/actor_collision.hpp"
#include "game/actor_model.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_stats.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/carry.hpp"
#include "game/distances.hpp"
#include "game/encounter.hpp"
#include "game/enemy_motion.hpp"
#include "game/enemy_neighbours.hpp"
#include "game/explosions.hpp"
#include "game/fixed_point.hpp"
#include "game/global_class_types.hpp"
#include "game/hit_effects.hpp"
#include "game/hit_list.hpp"
#include "game/hit_points.hpp"
#include "game/lobbed_shot.hpp"
#include "game/pickups.hpp"
#include "game/player/player_motion.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/room_grid.hpp"
#include "game/step_dust.hpp"
#include "game/story_flags.hpp"
#include "game/territory.hpp"

namespace od::port {

using namespace reaverbot_walker_state;

namespace {

constexpr const char* kWalkerPort = "reaverbot walker port"; // the area of its port-gap errors

// +0x161
constexpr uint8_t kWalkerWallX = 0x01, kWalkerWallZ = 0x02, kWalkerLanded = 0x04, kWalkerGroundLost = 0x08;
constexpr uint8_t kWalkerHitActor = 0x10;
constexpr uint8_t kWalkerWalls = kWalkerWallX | kWalkerWallZ;
// +0x162
constexpr uint8_t kWalkerNoHitEntry = 0x01, kWalkerNoDamage = 0x02, kWalkerNoWallTurn = 0x04, kWalkerNoFall = 0x08;
constexpr uint8_t kWalkerThrown = 0x10, kWalkerStrongAttack = 0x20, kWalkerBounces = 0x40, kWalkerNotTarget = 0x80;
// +0x160
constexpr uint8_t kWalkerCircles = 0, kWalkerHopper = 3, kWalkerKeepsBehaviour = 4;
// +1 bit 5: no attack starts (its setter is not traced)
constexpr uint8_t kRunFlagNoAttack = 0x20;

enum class WalkerState : uint8_t {
    Spawn = 0,   // exe 0x451d70
    Main = 1,    // exe 0x451f70
    Carried = 2, // exe 0x4530c0
    Dying = 3,   // exe 0x453380
    Sinking = 4, // exe 0x453520
};
enum class WalkerSub : uint8_t {
    Dormant = 0x0,     // exe 0x452080
    DropIn = 0x1,      // exe 0x4520d0
    Hidden = 0x2,      // exe 0x452190
    Wake = 0x3,        // exe 0x452230
    Circle = 0x4,      // exe 0x4522b0
    GiveWay = 0x5,     // exe 0x4524a0
    WallTurn = 0x6,    // exe 0x4525c0
    Lunge = 0x7,       // exe 0x4526d0
    Hop = 0x8,         // exe 0x452850
    Fall = 0x9,        // exe 0x452670
    Patrol = 0xa,      // exe 0x4529a0
    WalkOn = 0xb,      // exe 0x452b00
    ReturnHome = 0xc,  // exe 0x452b50
    Idle = 0xd,        // exe 0x452c50
    Flinch = 0xe,      // exe 0x452ce0
    KnockBack = 0xf,   // exe 0x452d70
    KnockAside = 0x10, // exe 0x452e70
    Fire = 0x11,       // exe 0x452f40
};
enum class WalkerCarriedSub : uint8_t { InHands = 0, Flying = 1 }; // exe 0x453130 / 0x453230

// The parameters of the glows it attaches (a stage effect of its own stage's table)
struct WalkerGlowParams {
    explicit WalkerGlowParams(const EffectActor& actor) : WalkerGlowParams(const_cast<EffectActor&>(actor)) {}
    explicit WalkerGlowParams(EffectActor& actor) : owner(actor.params[0]), part(actor.params[1]), size(actor.params, 2) {}
    uint8_t& owner; // the walker's slot
    uint8_t& part;
    BytesField<uint16_t, 4> size;
};

const DataTable<CollisionBoxRecord> walkerBox{TableId::ReaverbotWalkerBox};
const DataTable<InteractionBoxRecord> walkerLiftBox{TableId::ReaverbotWalkerLiftBox};
const DataTable<ScalarTable<uint8_t, 8>> walkerModels{TableId::ReaverbotWalkerModels}; // by the spawn byte
const DataTable<ScalarTable<uint8_t, 0x10>> walkerBehaviours{TableId::ReaverbotWalkerBehaviours}; // by variant & 0xf
const DataTable<ScalarTable<uint8_t, 0x10>> walkerFirstSubs{TableId::ReaverbotWalkerFirstSubs}; // by variant & 0xf
const DataTable<ScalarTable<uint8_t, 0x10>> walkerNextSubs{TableId::ReaverbotWalkerNextSubs}; // by the behaviour
const DataTable<ScalarTable<uint32_t, 8>> walkerAttackBits{TableId::ReaverbotWalkerAttackBits}; // by the spawn byte: the attack word's group immunities
const DataTable<ScalarTable<int8_t, 0x10>> wallTurnRates{TableId::ReaverbotWalkerWallTurnRates};
const DataTable<ScalarTable<int8_t, 0x10>> homeTurnRates{TableId::ReaverbotWalkerHomeTurnRates};
const DataTable<ScalarTable<uint8_t, 0x10>> attackCooldowns{TableId::ReaverbotWalkerAttackCooldowns};
constexpr uint8_t kAimedVariant = 0x80;  // variant bit: it fires (the lobbed shot) instead of lunging
constexpr uint8_t kAttachmentStageType = 0x10; // the stage effect type of its attached glows (ST0F: no class)
constexpr int16_t kWalkSpeed = -0x180, kWalkDecel = -0x10, kPatrolSpeed = -0x240, kLungeSpeed = -0x300;
constexpr int16_t kGravity = 0x40;
constexpr uint16_t kSoundLand = 0xc4, kSoundLunge = 0xa6, kSoundHurt = 0x8d, kSoundShot = 0x161, kSoundStep = 0xa5;
constexpr uint16_t kSoundSink = 0xa4;
constexpr uint8_t kSparkLoops = 2;

AnimatedActorStats statsOf(const AnimatedActor& a) { return animatedStats(a.type, a.spawnByte); }
// the type's hit points of its spawn byte's row (0 without a row)
uint16_t rowHitPoints(const AnimatedActor& a) { return hasAnimatedStats(a.type) ? statsOf(a).hitPoints : 0; }
void toSub(AnimatedActor& a, WalkerSub s) {
    a.subState = uint8_t(s);
    a.enterStep(0);
}
// exe 0x453650: the behaviour's sub-state
void toNextSub(AnimatedActor& a, WalkerFields& f) {
    const uint8_t s = walkerNextSubs[f.behaviour];
    a.enterStep(0);
    a.subState = s;
}

// exe 0x453870: the actor-collision result -> the contact bits
void contactHits(AnimatedActor& a, WalkerFields& f) {
    const uint32_t r = a.actorHit;
    if (r == 0) return;
    if (r & 0x300) f.contact |= kWalkerWallX;
    if (r & 0xc00) f.contact |= kWalkerWallZ;
    if (r & 0xf00) f.contact |= kWalkerHitActor;
    if (r & 0x3000) {
        a.airborne = 0;
        f.contact |= kWalkerLanded;
    }
}
// exe 0x453670: the damage of the hit box -- a flinch on the player's attacks (one hit less), knocked back / aside on
// the special weapons' pushes; dead or no hits left -> the knock-back (-1)
int hurt(AnimatedActor& a, WalkerFields& f) {
    const uint32_t v = f.hitBox.result;
    const uint32_t dmg = v & kHitDamageMask;
    if (dmg == 0) return 0;
    uint32_t take = 0;
    if (v & (kHitAttackByPlayer | kHitAttackThrown | kHitAttackStrong)) {
        f.heading = f.hitBox.contact.x;
        f.hitsLeft = int8_t(uint8_t(f.hitsLeft) - 1u);
        a.tint = kTintFlash;
        a.subState = uint8_t(WalkerSub::Flinch);
        a.enterStep(0);
        take = dmg;
    }
    if (v & (kHitLevelBelow | kHitLevelWithin | kHitLevelAbove)) {
        f.heading = f.hitBox.contact.x;
        if (v & kHitLevelBelow) {
            a.subState = uint8_t(WalkerSub::KnockBack);
            a.enterStep(0);
            a.tint = kTintFlash;
        } else if (v & kHitLevelWithin) {
            a.subState = uint8_t(WalkerSub::KnockAside);
            a.enterStep(0);
            a.tint = kTintFlash;
        }
        take = dmg;
    }
    if (hitPointsAdd(a, -int(take)) == 0 && f.hitsLeft > 0) return 0;
    a.subState = uint8_t(WalkerSub::KnockBack);
    a.enterStep(0);
    return -1;
}
// exe 0x453730: the walls / the territory / the ground; 1 = the ground was lost (> 0x10 below)
int collide(const PlayerEnv& env, AnimatedActor& a, WalkerFields& f) {
    f.contact = 0;
    uint32_t r;
    if (int32_t(actorTestMove(env, a)) < 0) {
        r = actorPushOut(env, a); // exe 0x410960
        if (int32_t(r) < 0) a.pos = a.prevPos;
        if (r & 0x300) f.contact |= kWalkerWallX;
        if (r & 0xc00) f.contact |= kWalkerWallZ;
        if ((f.bits & kWalkerBounces) && reflectRotationOffWalls(a.rotation, r) != 0) f.contact |= kWalkerLanded;
    } else {
        const int z = territoryViolated(a.pos);
        r = 0;
        if (z != 0) {
            r = territoryPushBack(a, z);
            if (r & 0x300) f.contact |= kWalkerWallX;
            if (r & 0xc00) f.contact |= kWalkerWallZ;
        }
    }
    if (a.hitPoints >= 0) actorCollisionSubmitMover(a); // exe 0x4140b0
    const int g = actorGround(env, a);                  // exe 0x40f190
    if (a.airborne == 0) {
        if (a.onPlatform == 0) {
            if (g - a.pos.y.whole > 0x10) {
                f.contact |= kWalkerGroundLost;
                return 1;
            }
            a.pos.y.whole = int16_t(g);
            return 0;
        }
    } else if (!(f.bits & kWalkerBounces) && (r & 0x3000)) {
        a.airborne = 0;
        a.pos.y.whole = int16_t(g);
        f.contact |= kWalkerLanded;
    }
    return 0;
}
// exe 0x453300(actor, yaw): the airborne step -- an actor hit stops the forward speed; landed: halved / bounced, the
// sound, 1; else the move along the yaw and the gravity
int airStep(AnimatedActor& a, WalkerFields& f, int yaw) {
    if (f.contact & kWalkerHitActor) a.velocity.z = 0;
    if (f.contact & kWalkerLanded) {
        a.velocity.z = int16_t(a.velocity.z >> 1);
        a.velocity.y = int16_t(uint32_t(-int(a.velocity.y)) >> 1);
        a.rotation.x = 0;
        a.airborne = 1;
        soundPlayAtPos(kSoundLand, a.pos);
        return 1;
    }
    moveAlongYaw(a.pos, yaw, 0, a.velocity.y, a.velocity.z); // exe 0x5e36f0
    a.velocity.y = int16_t(a.velocity.y + a.acceleration.y);
    return 0;
}
// exe 0x452250: the walk animation, its speed and deceleration, a random side, the scale 1.0
void walkReset(AnimatedActor& a, WalkerFields& f) {
    a.anim = 1;
    a.velocity.setXY(0, 0);
    a.velocity.z = kWalkSpeed;
    a.acceleration.z = kWalkDecel;
    const uint32_t r = rngStep();
    f.neighbour = uint16_t(r & 1);
    resetModelScale(a);
    a.runFlags &= uint8_t(~kActorRunMarked);
}

// ---- sub-states -----------------------------------------------------------------------------------------------------
void dormant(AnimatedActor& a, WalkerFields& f) { // exe 0x452080: until the story flag -> walk on
    if (a.step == 0) {
        walkReset(a, f);
        a.anim = 0;
        f.bits = int8_t(kWalkerNotTarget | kWalkerNoWallTurn);
        a.enterStep(1);
    }
    if (storyFlag(uint32_t(int32_t(f.wakeFlag)))) toSub(a, WalkerSub::WalkOn);
}
void dropIn(AnimatedActor& a, WalkerFields& f) { // exe 0x4520d0: falls, one bounce with a random yaw, the behaviour
    if (a.step == 0) {
        a.anim = 2;
        a.velocity.y = kGravity;
        a.acceleration.y = kGravity;
        f.bits = int8_t(kWalkerNotTarget | kWalkerBounces | kWalkerNoWallTurn);
        a.velocity.z = 0;
        a.airborne = 1;
        a.enterStep(1);
    }
    if (airStep(a, f, a.rotation.y) == 0) return;
    if (a.stepCounter == 0) {
        a.velocity.z = -0x80;
        const uint32_t r = rngStep();
        a.rotation.y = int16_t((r & 0xff) << 4);
        ++a.stepCounter;
        integrateVelocity(a, 0, a.velocity.y, a.velocity.z);
        return;
    }
    toNextSub(a, f);
    a.velocity.y = 0;
    a.airborne = 0;
}
void hidden(AnimatedActor& a, WalkerFields& f) { // exe 0x452190: hidden until the story flag, the hit points refilled
    if (a.step == 0) {
        walkReset(a, f);
        a.anim = 0;
        f.bits = int8_t(kWalkerNotTarget | kWalkerNoFall | kWalkerNoWallTurn | kWalkerNoDamage | kWalkerNoHitEntry);
        a.enterStep(1);
    }
    a.anim = 0;
    a.prevAnim = 0xff;
    if (!storyFlag(uint32_t(int32_t(f.wakeFlag)))) return;
    const uint16_t hp = rowHitPoints(a);
    a.subState = uint8_t(WalkerSub::Wake);
    a.enterStep(0);
    a.maxHitPoints = int16_t(hp);
    a.hitPoints = int16_t(hp);
}
void wake(AnimatedActor& a, WalkerFields& f) { // exe 0x452230: the walk reset -> circle
    walkReset(a, f);
    f.bits = 0;
    toSub(a, WalkerSub::Circle);
}
void circle(AnimatedActor& a, WalkerFields& f) { // exe 0x4522b0: around the player; a lunge / a shot / a hop
    if (a.step == 0) {
        f.bits = 0;
        a.anim = 1;
        a.enterStep(1);
    }
    const int ang = headingFromPlayer(a, neighbourSide(f.neighbour), 500, 700);
    if (stepTowardLimit(a.velocity.z, 0xfff0, 0x180) != 0) a.velocity.z = kWalkSpeed;
    turnYawTo(a.rotation.y, uint32_t(ang), 0x20);
    integrateVelocity(a, 0, 0, a.velocity.z);
    int found = int(neighbourFind(a, f.neighbour, 0));
    if (found != 0) toSub(a, WalkerSub::GiveWay);
    const Player& p = player();
    const int dist = planarDistance(p.pos, a.pos);
    if (f.cooldown != 0) --f.cooldown;
    if (f.cooldown == 0) {
        const uint32_t r = rngStep();
        if ((1u << (r & 0xf)) & 0x2c0) {
            if (!(a.variant & kAimedVariant)) {
                if (dist < 0x2bc && !(a.runFlags & kRunFlagNoAttack) &&
                    !(enemyGroupMarks(f.group, EnemyGroupMarks::Either) & 1)) {
                    enemyGroupMark(f.group, 1);
                    toSub(a, WalkerSub::Lunge);
                }
            } else {
                found = angleDiff(uint32_t(int32_t(a.rotation.y)), uint32_t(angleOf(p.pos, a.pos)));
                if (uint32_t(found + 0x180) < 0x300u && dist < 0x578 && !(a.runFlags & kRunFlagNoAttack) &&
                    !(enemyGroupMarks(f.group, EnemyGroupMarks::Either) & 1)) {
                    enemyGroupMark(f.group, 1);
                    toSub(a, WalkerSub::Fire);
                }
            }
        }
    }
    if (f.behaviour == kWalkerHopper && found == 0) {
        const uint32_t r = rngStep();
        if (((1u << (r & 0xf)) & 0x80) && dist > 0x600) toSub(a, WalkerSub::Hop);
    }
}
void giveWay(AnimatedActor& a, WalkerFields& f) { // exe 0x4524a0: by how the neighbour moves (exe 0x5e5f70)
    Actor& other = neighbourActor(uint16_t(f.neighbour)); // the id before this tick's classification
    int target = 0;
    const uint8_t st = a.step;
    if (st == 2) {
        if (stepTowardLimit(a.velocity.z, 0xfff0, 0x180) != 0) toNextSub(a, f);
    } else if (st <= 1) {
        if (st == 0) {
            f.bits = 0;
            a.enterStep(1);
            neighbourClassify(a, f.neighbour);
            a.acceleration.z = kWalkDecel;
        }
        const uint16_t n = f.neighbour;
        if (n & kNeighbourOpposite) target = headingFromOther(a, other, neighbourSide(f.neighbour), 300);
        if (n & kNeighbourCrossingSlow) accelerateToward(a, 0x200);
        if (n & kNeighbourCatching) decelerateToward(a, 0x100);
        if (other.flags == 0 || ((uint16_t(other.pos.x.whole) ^ uint16_t(a.pos.x.whole)) & 0xff00) ||
            ((uint16_t(other.pos.z.whole) ^ uint16_t(a.pos.z.whole)) & 0xff00))
            a.enterStep(2);
    }
    turnYawTo(a.rotation.y, uint32_t(target), 0x40);
    integrateVelocity(a, 0, 0, a.velocity.z);
}
void wallTurn(AnimatedActor& a, WalkerFields& f) { // exe 0x4525c0: a wall -- turn at a random rate until free
    if (a.step == 0) {
        a.enterStep(1);
        f.bits = int8_t(kWalkerNoWallTurn);
        const uint32_t r = rngStep();
        const int c = wallTurnRates[r & 0xf];
        f.heading = int16_t(c);
        if (a.rotation.y == a.prevRotation.y) {
            if (f.neighbour & kNeighbourSideRight) f.heading = int16_t(-c);
        } else if (int(a.rotation.y) - int(a.prevRotation.y) < 0) {
            f.heading = int16_t(-c);
        }
        a.velocity.z = kWalkSpeed;
        a.anim = 1;
    }
    integrateVelocity(a, 0, 0, a.velocity.z);
    a.rotation.y = int16_t(uint16_t(a.rotation.y) + uint16_t(f.heading));
    if ((f.contact & kWalkerWalls) == 0) toNextSub(a, f);
}
void lunge(AnimatedActor& a, WalkerFields& f) { // exe 0x4526d0: shake 0x5a ticks facing the player, dash 0xf ticks
    EncounterState& enc = encounterState();
    switch (a.step) {
    case 0: {
        f.bits = 0;
        a.enterStep(1);
        a.anim = 0;
        f.timer = 0x5a;
        const uint32_t r = rngStep();
        f.cooldown = attackCooldowns[r & 0xf];
    }
        [[fallthrough]];
    case 1: {
        a.rotation.y = int16_t(angleOf(player().pos, a.pos));
        const uint32_t b = uint8_t(f.timer) & 1;
        a.scale.x = int16_t((b + 0x10) * 0x20);
        a.scale.y = int16_t(int(b) * -0x20 + kModelScaleOne);
        a.scale.z = int16_t((b + 0x10) * 0x20);
        f.timer = int16_t(f.timer - 1);
        if (f.timer == 0) {
            a.enterStep(2);
            resetModelScale(a);
            enc.markAttackLock(kAttackLockLunge);
            return;
        }
        break;
    }
    case 2:
        a.enterStep(3);
        a.anim = 2;
        a.rotation.y = int16_t(angleOf(player().pos, a.pos));
        a.velocity.setXY(0, 0);
        a.velocity.z = kLungeSpeed;
        f.timer = 0xf;
        soundPlayAtPos(kSoundLunge, a.pos);
        [[fallthrough]];
    case 3:
        integrateVelocity(a, 0, 0, a.velocity.z);
        f.timer = int16_t(f.timer - 1);
        if (f.timer == 0) toSub(a, WalkerSub::Wake);
        f.bits |= int8_t(kWalkerStrongAttack);
        break;
    default: break;
    }
    enc.markAttackLock(kAttackLockLunge);
}
void hop(AnimatedActor& a, WalkerFields& f) { // exe 0x452850: wait 0x1e ticks facing the player, then a hop at it
    EncounterState& enc = encounterState();
    switch (a.step) {
    case 0:
        f.bits = int8_t(kWalkerBounces | kWalkerNoFall);
        a.enterStep(1);
        a.anim = 0;
        a.acceleration.y = kGravity;
        f.timer = 0x1e;
        [[fallthrough]];
    case 1:
        a.rotation.y = int16_t(angleOf(player().pos, a.pos));
        f.timer = int16_t(f.timer - 1);
        if (f.timer == 0) {
            a.enterStep(2);
            resetModelScale(a);
            enc.markAttackLock(kAttackLockLunge);
            return;
        }
        break;
    case 2: {
        a.enterStep(3);
        a.anim = 2;
        const Player& p = player();
        const int yaw = angleOf(p.pos, a.pos);
        a.velocity.setXY(0, 0);
        a.rotation.y = int16_t(yaw);
        a.velocity.x = 0;
        const int32_t from[3] = {a.pos.x, a.pos.y, a.pos.z}, to[3] = {p.pos.x, a.pos.y, p.pos.z};
        a.velocity.y = int16_t(launchSpeed(from, to, 0x300, kGravity, 0x400));
        a.velocity.z = kLungeSpeed;
        a.airborne = 1;
    }
        [[fallthrough]];
    case 3:
        f.bits |= int8_t(kWalkerStrongAttack);
        if (airStep(a, f, a.rotation.y) != 0) {
            toNextSub(a, f);
            a.airborne = 0;
            a.velocity.y = 0;
        }
        break;
    default: break;
    }
    enc.markAttackLock(kAttackLockLunge);
}
void fall(AnimatedActor& a, WalkerFields& f) { // exe 0x452670: the ground lost -- until it lands
    if (a.step == 0) {
        f.bits = int8_t(kWalkerBounces | kWalkerNoFall | kWalkerNoWallTurn);
        a.step = 1;
        a.anim = 1;
        a.acceleration.y = kGravity;
        resetModelScale(a);
    } else if (a.step != 1) {
        return;
    }
    if (airStep(a, f, a.rotation.y) != 0) {
        a.airborne = 0;
        a.velocity.y = 0;
        toNextSub(a, f);
    }
}
void patrol(AnimatedActor& a, WalkerFields& f) { // exe 0x4529a0: back and forth along the home yaw
    if (a.step == 0) {
        f.bits = int8_t(kWalkerNoWallTurn);
        f.heading = int16_t(f.patrolCells);
        a.enterStep(1);
        a.anim = 1;
    }
    if (a.rotation.y == f.homeYaw) {
        if (stepTowardLimit(a.velocity.z, 0xfff0, 0x240) != 0) {
            a.velocity.z = kPatrolSpeed;
            a.stepCounter = 0;
        }
    } else if (stepTowardLimit(a.velocity.z, 0xfff0, 0) != 0) {
        a.velocity.z = 0;
        const int d = turnYawTo(a.rotation.y, uint32_t(int32_t(f.homeYaw)), 0x40);
        if (uint32_t(d + 0x40) < 0x81u) a.rotation.y = f.homeYaw;
    }
    integrateVelocity(a, 0, 0, a.velocity.z);
    if (((uint16_t(a.prevPos.x.whole) ^ uint16_t(a.pos.x.whole)) & 0xfe00) ||
        ((uint16_t(a.prevPos.z.whole) ^ uint16_t(a.pos.z.whole)) & 0xfe00)) {
        f.heading = int16_t(f.heading - 1);
        if (f.heading < 0) {
            a.stepCounter = 1;
            f.homeYaw = int16_t((uint32_t(uint16_t(f.homeYaw)) - uint32_t(kHalfTurn)) & kAngleMask);
            f.heading = int16_t(int32_t(f.patrolCells) << 1);
            return;
        }
    }
    if (a.stepCounter == 0 && int(f.contact & kWalkerWalls) == 2 - ((uint16_t(f.homeYaw) & 0x400) != 0 ? 1 : 0)) {
        a.stepCounter = 1;
        a.velocity.z = 0;
        f.homeYaw = int16_t((uint32_t(uint16_t(f.homeYaw)) - uint32_t(kHalfTurn)) & kAngleMask);
        f.heading = int16_t(int32_t(f.patrolCells) << 1);
    }
}
void walkOn(AnimatedActor& a, WalkerFields& f) { // exe 0x452b00: on until a wall -> circle
    if (a.step == 0) {
        f.bits = int8_t(kWalkerNoWallTurn);
        a.step = 1;
        a.anim = 1;
    }
    integrateVelocity(a, 0, 0, a.velocity.z);
    if (f.contact & kWalkerWalls) toSub(a, WalkerSub::Circle);
}
void returnHome(AnimatedActor& a, WalkerFields& f) { // exe 0x452b50: back toward the home point; the player near
    if (a.step == 0) {
        f.bits = int8_t(kWalkerNoWallTurn);
        a.anim = 1;
        a.enterStep(1);
    }
    if (stepTowardLimit(a.velocity.z, 0xfff0, 0x180) != 0) a.velocity.z = kWalkSpeed;
    const int ang = angleOf(wholeToFixed16(a.spin.z), wholeToFixed16(f.homeZ), a.pos.x, a.pos.z);
    const uint32_t r = rngStep();
    turnYawTo(a.rotation.y, uint32_t(ang), homeTurnRates[r & 0xf]);
    integrateVelocity(a, 0, 0, a.velocity.z);
    if (neighbourFind(a, f.neighbour, 0) != 0) toSub(a, WalkerSub::GiveWay);
    if (planarDistance(player().pos, a.pos) < 0x2bc) {
        f.behaviour = kWalkerCircles;
        toSub(a, WalkerSub::Circle);
    }
}
void idle(AnimatedActor& a, WalkerFields& f) { // exe 0x452c50: until the story flag (-1 = never) -> circle
    if (a.step == 0) {
        f.bits = 0;
        a.anim = 0;
        a.velocity.setXY(0, 0);
        a.velocity.z = 0;
        a.acceleration.z = 0;
        const uint32_t r = rngStep();
        f.neighbour = uint16_t(r & 1);
        resetModelScale(a);
        a.step = 1;
        a.runFlags &= uint8_t(~kActorRunMarked);
    }
    if (f.wakeFlag != -1 && storyFlag(uint32_t(int32_t(f.wakeFlag)))) {
        f.behaviour = kWalkerCircles;
        toSub(a, WalkerSub::Circle);
    }
}
void flinch(AnimatedActor& a, WalkerFields& f) { // exe 0x452ce0: until the animation ends (airborne: until landed)
    bool air = false;
    if (a.step == 0) {
        a.enterStep(1);
        f.bits = int8_t(kWalkerNoWallTurn | kWalkerNoDamage);
        a.anim = 0;
        a.velocity.z = 0;
        if (a.airborne == 0) a.velocity.y = 0;
        else a.acceleration.y = kGravity;
        soundPlayAtPos(kSoundHurt, a.pos);
        resetModelScale(a);
    } else if (a.animEnded()) {
        if (a.airborne != 0) air = true;
        else toNextSub(a, f);
    }
    if (!air && a.airborne == 0) return;
    if (airStep(a, f, a.rotation.y) != 0) {
        a.airborne = 0;
        a.velocity.y = 0;
    }
}
void knockBack(AnimatedActor& a, WalkerFields& f) { // exe 0x452d70: along the hit's direction (6 ticks); dead -> dying
    if (a.step == 0) {
        a.enterStep(1);
        f.bits = int8_t(kWalkerNoWallTurn | kWalkerNoDamage);
        a.anim = 2;
        a.rotation.y = f.heading;
        a.velocity.x = 0;
        a.velocity.z = -0x2a0;
        if (a.airborne == 0) a.velocity.y = 0;
        else a.acceleration.y = kGravity;
        soundPlayAtPos(kSoundHurt, a.pos);
        resetModelScale(a);
        f.timer = 6;
        f.hitsLeft = 3;
        f.bits = int8_t(a.hitPoints < 0 ? (uint8_t(f.bits) | kWalkerNoFall) : (uint8_t(f.bits) & 0xf7u));
    }
    integrateVelocity(a, 0, a.velocity.y, a.velocity.z);
    if (a.hitPoints < 0) {
        f.timer = int16_t(f.timer - 1);
        if (f.timer == 0 || a.airborne != 0) a.enterState(uint8_t(WalkerState::Dying));
    } else {
        if (f.timer != 0) f.timer = int16_t(f.timer - 1);
        if (f.timer == 0 && a.airborne == 0) toNextSub(a, f);
    }
}
void knockAside(AnimatedActor& a, WalkerFields& f) { // exe 0x452e70: along the hit's direction, decelerating
    if (a.step == 0) {
        f.bits = int8_t(kWalkerNoDamage);
        a.enterStep(1);
        a.anim = 0;
        a.velocity.x = 0;
        a.velocity.z = kWalkSpeed;
        a.acceleration.z = kGravity;
        if (a.airborne == 0) a.velocity.y = 0;
        else a.acceleration.y = kGravity;
        soundPlayAtPos(kSoundHurt, a.pos);
        resetModelScale(a);
    }
    if (a.airborne == 0) {
        moveAlongYaw(a.pos, f.heading, 0, a.velocity.y, a.velocity.z); // exe 0x5e36f0
    } else if (airStep(a, f, f.heading) != 0) {
        a.airborne = 0;
        a.velocity.y = 0;
    }
    a.velocity.z = int16_t(a.velocity.z + a.acceleration.z);
    if (a.velocity.z >= 0) {
        a.velocity.z = 0;
        if (a.airborne == 0) toNextSub(a, f);
    }
}
// exe 0x452f40: face the player; at the key 0xb the lobbed shot (its stage projectile type, the shot damage, its sound)
// and a hit spark (variant 3)
void fire(AnimatedActor& a, WalkerFields& f) {
    const uint8_t st = a.step;
    if (st == 0) {
        f.bits = 0;
        a.anim = 3;
        const uint32_t r = rngStep();
        f.cooldown = attackCooldowns[r & 0xf];
        ++a.step;
    } else if (st == 1) {
        turnYawTo(a.rotation.y, uint32_t(angleOf(player().pos, a.pos)), 0x20);
        enemyGroupMark(f.group, 1);
        if (a.keyIndex != 0xb) return;
        const uint32_t shot = allocActor40(kActorStageClass); // exe 0x5e1280
        if (shot != 0) {
            ProjectileActor& s = projectileActorAt(shot);
            LobbedShotParams lob(s);
            s.flags = kActorLive;
            s.type = kLobbedShotStageType;
            s.variant = 1;
            s.pos.x = a.pos.x;
            s.pos.y = int32_t(uint32_t(a.pos.y) - 0x400000u);
            s.pos.z = a.pos.z;
            s.rotation.x = 0;
            const int16_t yaw = a.rotation.y;
            lob.size = 0x20;
            s.acceleration.y = 0x20;
            s.rotation.y = yaw;
            s.rotation.z = 0;
            s.velocity.x = 0;
            s.velocity.y = -0x100;
            s.velocity.z = -0x200;
            lob.damage = int16_t(statsOf(a).shotAttack);
            lob.launchSound = int16_t(kSoundShot);
        }
        const uint32_t effect = allocActorA0(0); // exe 0x5e13b0
        if (effect != 0) {
            if (shot == 0) notPorted(kWalkerPort, "0x452f40: a spark at a null shot (the exe reads address 0)");
            const ProjectileActor& s = projectileActorAt(shot);
            EffectActor& e = effectActorAt(effect);
            e.flags = kActorLiveDrawn;
            e.type = typeByte(GlobalEffectType::HitSpark);
            e.variant = 3;
            e.loopFlags |= kActorSweepGroup2;
            e.pos.x = s.pos.x.whole;
            e.pos.y = s.pos.y.whole;
            e.pos.z = s.pos.z.whole;
            classState<HitSparkFields>(e).size = 0xc0;
        }
        ++a.step;
    } else if (st == 2 && a.animEnded()) {
        toSub(a, WalkerSub::Wake);
    }
}

// ---- states ---------------------------------------------------------------------------------------------------------
// exe 0x4505f0(actor, part, size): a stage effect (type 0x10, variant 1) attached to the walker (ST0F: no class)
void attachGlow(AnimatedActor& a, uint8_t part, uint16_t size) {
    const uint32_t address = allocActorA0(kActorStageClass); // exe 0x5e13a0
    if (!address) return;
    EffectActor& e = effectActorAt(address);
    e.flags = kActorLive;
    e.type = kAttachmentStageType;
    e.loopFlags |= kActorSweepGroup2;
    e.variant = 1;
    const uint8_t slot = a.slot;
    e.pos.x = 0;
    WalkerGlowParams g(e);
    g.owner = slot;
    g.part = part;
    g.size = size;
    e.pos.y = 0;
    e.pos.z = 0;
}
void spawn(const PlayerEnv& env, AnimatedActor& a, WalkerFields& f) { // exe 0x451d70 (state 0)
    if (!bindActorModel(a, walkerModels[a.spawnByte])) {
        freeActor(a);
        return;
    }
    a.prevPos.x = a.pos.x;
    a.anim = 4;
    a.prevAnim = 0xff;
    a.prevPos.y = a.pos.y;
    a.flags |= kActorDrawn;
    a.prevPos.z = a.pos.z;
    a.prevRotation = a.rotation;
    a.wasOnPlatform = a.onPlatform;
    a.wasAirborne = a.airborne;
    a.runFlags |= kActorRunMarked;
    const uint16_t hp = rowHitPoints(a);
    a.maxHitPoints = int16_t(hp);
    a.hitPoints = int16_t(hp);
    a.collisionBox.pointAt(walkerBox.stateAddress());
    a.interactionBox.pointAt(walkerLiftBox.stateAddress());
    a.interaction = kInteractLift;
    a.platform = nullptr;
    a.actorHit = 0;
    a.airborne = 0;
    a.onPlatform = 0;
    a.clearClassBlock();
    f.hitBox.previousX = a.pos.x.whole;
    f.hitBox.previousZ = a.pos.z.whole;
    f.hitBox.result = 0;
    f.hitsLeft = 3;
    f.behaviour = walkerBehaviours[a.variant & 0xf];
    f.group = int8_t(enemyGroupFindFor(a, 0));
    switch (a.variant & 0xf) {
    case 2:
        a.spin.z = a.pos.x.whole; // the home point's x
        f.homeZ = a.pos.z.whole;
        walkReset(a, f);
        [[fallthrough]];
    case 0:
    case 1:
        if (f.patrolCells != 0) f.behaviour = kWalkerHopper;
        a.pos.y.whole = int16_t(actorGround(env, a));
        break;
    case 3:
        f.homeYaw = a.rotation.y;
        walkReset(a, f);
        a.pos.y.whole = int16_t(actorGround(env, a));
        break;
    case 5:
        a.hitPoints = -1;
        [[fallthrough]];
    case 4:
        if (f.patrolCells != 0) f.behaviour = kWalkerHopper;
        break;
    case 6: a.anim = 0; break;
    default: break;
    }
    if (a.spawnByte == 3 || a.spawnByte == 5) {
        attachGlow(a, 0, 0x8c);
        attachGlow(a, 1, 0x40);
        attachGlow(a, 3, 0x40);
    }
    const uint8_t variant = a.variant, slot = a.slot;
    a.state = uint8_t(WalkerState::Main);
    a.subState = 0;
    a.step = 0;
    a.stepCounter = 0;
    a.subState = walkerFirstSubs[variant & 0xf];
    registerEnemy(slot, 0);
}
void mainState(const PlayerEnv& env, AnimatedActor& a, WalkerFields& f) { // exe 0x451f70 (state 1)
    f.setTintBytes(0x10, 0x42);
    contactHits(a, f);
    a.prevAnim = a.anim;
    a.savePreviousPose();
    a.savePreviousContact();
    if (!(f.bits & kWalkerNoDamage)) hurt(a, f);
    switch (WalkerSub(a.subState)) {
    case WalkerSub::Dormant: dormant(a, f); break;
    case WalkerSub::DropIn: dropIn(a, f); break;
    case WalkerSub::Hidden: hidden(a, f); break;
    case WalkerSub::Wake: wake(a, f); break;
    case WalkerSub::Circle: circle(a, f); break;
    case WalkerSub::GiveWay: giveWay(a, f); break;
    case WalkerSub::WallTurn: wallTurn(a, f); break;
    case WalkerSub::Lunge: lunge(a, f); break;
    case WalkerSub::Hop: hop(a, f); break;
    case WalkerSub::Fall: fall(a, f); break;
    case WalkerSub::Patrol: patrol(a, f); break;
    case WalkerSub::WalkOn: walkOn(a, f); break;
    case WalkerSub::ReturnHome: returnHome(a, f); break;
    case WalkerSub::Idle: idle(a, f); break;
    case WalkerSub::Flinch: flinch(a, f); break;
    case WalkerSub::KnockBack: knockBack(a, f); break;
    case WalkerSub::KnockAside: knockAside(a, f); break;
    case WalkerSub::Fire: fire(a, f); break;
    default: notPortedAt(kWalkerPort, "0x451c00 sub-state", a.subState);
    }
    if (a.animKey.events & kAnimEventFootfall) soundPlayAtPos(kSoundStep, a.pos); // a footstep frame
    bool reset = false;
    if (collide(env, a, f) == 0) {
        if ((f.contact & kWalkerWalls) != 0 && !(f.bits & kWalkerNoWallTurn)) {
            a.subState = uint8_t(WalkerSub::WallTurn);
            reset = true;
        }
    } else {
        a.airborne = 1;
        if (!(f.bits & kWalkerNoFall)) {
            a.subState = uint8_t(WalkerSub::Fall);
            reset = true;
        }
    }
    if (reset) {
        a.step = 0;
        a.stepCounter = 0;
    }
    if (a.hitPoints >= 0 && playerLiftTargetCheck(a, 0)) {
        a.state = uint8_t(WalkerState::Carried);
        a.subState = 0;
        a.step = 0;
        a.stepCounter = 0;
        f.bits |= int8_t(kWalkerNoHitEntry);
        a.runFlags |= kActorRunMarked;
    }
}
// exe 0x453380 (state 3): the flash, an explosion, the drops after 0x14 / 8 ticks, freed
void dying(const PlayerEnv& env, AnimatedActor& a, WalkerFields& f) {
    f.setTintBytes(0xff, 0x7f);
    contactHits(a, f);
    a.prevAnim = a.anim;
    a.savePreviousPose();
    a.savePreviousContact();
    if (a.subState == 0) {
        a.subState = 1;
        f.timer = 0x14;
        if (a.airborne == 0) {
            a.velocity.x = 0;
            a.velocity.y = 0;
            a.velocity.z = 0;
        } else {
            f.timer = 8;
        }
        a.acceleration.y = kGravity;
        a.anim = 2;
        const uint32_t address = allocActor60(0); // exe 0x5e1300: the explosion (game/explosions)
        if (address != 0) {
            StaticActor& x = staticActorAt(address);
            ExplosionParams e(x);
            x.flags = kActorLive;
            x.type = typeByte(GlobalStaticType::Explosion);
            x.variant = 0;
            x.loopFlags |= kActorSweepGroup2;
            x.pos.x.whole = a.pos.x.whole;
            x.pos.y.whole = wrap16(a.pos.y.whole - 0x60);
            x.pos.z.whole = a.pos.z.whole;
            e.spread = 0x30;
            e.flashes = uint16_t(f.timer);
        }
        countDefeat();
    }
    if (uint8_t(f.timer) & 1) f.setTintBytes(0x14, 0x42);
    if (f.timer < 5) f.bits |= int8_t(kWalkerNotTarget);
    f.timer = int16_t(f.timer - 1);
    if (f.timer == 0) {
        spawnDrops(a.pos, statsOf(a).dropTable); // exe 0x5f1270
        freeActor(a);
        return;
    }
    integrateVelocity(a, a.velocity.x, a.velocity.y, a.velocity.z);
    if (a.airborne != 0) a.velocity.y = int16_t(a.velocity.y + a.acceleration.y);
    collide(env, a, f);
    if (a.wasAirborne != 0 && a.airborne == 0) {
        a.velocity.z = 0;
        a.velocity.x = 0;
    }
}
// exe 0x453520 (state 4): sinks 0x10 a tick, dust every 3rd tick, freed 0xb4 below the ground (hypothesis: into a
// pit / the water)
void sinking(const PlayerEnv& env, AnimatedActor& a, WalkerFields& f) {
    a.prevAnim = a.anim;
    a.savePreviousPose();
    a.savePreviousContact();
    if (a.subState == 0) {
        a.anim = 4;
        f.timer = 0;
        soundPlayAtPos(kSoundSink, a.pos);
        a.subState = 1;
        return;
    }
    a.pos.y += 0x100000u;
    const int g = actorGround(env, a);
    if (int(f.timer) % 3 == 0) {
        const int32_t kind = groundKindUnder(a.pos);
        const uint32_t r = rngStep();
        const ShortVec4& bp = bonePoint(a, (r & 3) + 1, kWalkerPort);
        spawnDustSpark(bp.x, wrap16(g - 10), bp.z);
        if (f.timer == 0) {
            spawnStepDust(a.pos.x.whole, int16_t(uint16_t(g)), a.pos.z.whole, kind);
            spawnStepDust(a.pos.x.whole, int16_t(uint16_t(g)), a.pos.z.whole, kind);
        }
    }
    f.timer = int16_t(f.timer + 1);
    if (g < a.pos.y.whole - 0xb4) freeActor(a);
}
void thrown(const PlayerEnv& env, AnimatedActor& a, WalkerFields& f);
void inHands(const PlayerEnv& env, AnimatedActor& a, WalkerFields& f) { // exe 0x453130: until released
    if (a.step == 0) {
        f.bits = int8_t(kWalkerBounces | kWalkerNoHitEntry);
        a.step = 1;
        a.anim = 5;
        resetModelScale(a); // exe 0x4e5cd0 (no exe 0x5e6540 here, unlike 0x451550)
    }
    carryAtHands(a, 0, 0, 0); // exe 0x573ab0
    const CarryRelease r = carryReleased(); // exe 0x573950
    if (r == CarryRelease::Held) return;
    f.bits &= int8_t(~kWalkerNoHitEntry);
    a.runFlags &= uint8_t(~kActorRunMarked);
    f.heading = player().rotation.y;
    if (r != CarryRelease::Thrown) {
        a.velocity.y = -0x300;
        a.velocity.z = -0x40;
    } else {
        int32_t v[3];
        playerAimVector(0, -0x100, -0x380, v); // exe 0x5e3e50
        a.velocity.x = 0;
        a.velocity.y = int16_t(v[1]);
        a.velocity.z = int16_t(v[2]);
        f.bits |= int8_t(kWalkerThrown);
    }
    a.acceleration.y = kGravity;
    a.rotation.y = f.heading;
    a.airborne = 1;
    carryDropFromPlayer(a); // exe 0x573990
    a.subState = uint8_t(WalkerCarriedSub::Flying);
    a.enterStep(0);
    thrown(env, a, f);
}
// exe 0x453230: flying; a hit while alive bounces it back (dead); landed twice -> the main state (its behaviour kept
// only when kWalkerKeepsBehaviour) / dead -> dying
void thrown(const PlayerEnv& env, AnimatedActor& a, WalkerFields& f) {
    if ((f.hitBox.result & kHitAttackSolid) && a.hitPoints >= 0) {
        a.velocity.z = int16_t(uint32_t(-int32_t(a.velocity.z)) >> 1);
        a.hitPoints = -1;
        spawnHitSpark(kSparkLoops, a.pos.x.whole, wrap16(a.pos.y.whole - 0x40), a.pos.z.whole);
    }
    if (airStep(a, f, a.rotation.y)) { // exe 0x453300
        if (a.stepCounter == 0) {
            a.stepCounter = 1;
            a.anim = 2;
            integrateVelocity(a, 0, a.velocity.y, a.velocity.z); // exe 0x5e3690
            collide(env, a, f);
            return;
        }
        if (a.hitPoints >= 0) {
            if (f.behaviour != kWalkerKeepsBehaviour) f.behaviour = kWalkerCircles;
            a.enterState(uint8_t(WalkerState::Main));
            toNextSub(a, f); // exe 0x453650
        } else {
            a.enterState(uint8_t(WalkerState::Dying));
        }
        a.velocity.y = 0;
        a.airborne = 0;
    }
    collide(env, a, f); // exe 0x453730
}
void carried(const PlayerEnv& env, AnimatedActor& a, WalkerFields& f) { // exe 0x4530c0 (state 2)
    if (a.subState != 0) contactHits(a, f);
    a.prevAnim = a.anim;
    a.savePreviousPose();
    a.savePreviousContact();
    switch (WalkerCarriedSub(a.subState)) {
    case WalkerCarriedSub::InHands: inHands(env, a, f); break;
    case WalkerCarriedSub::Flying: thrown(env, a, f); break;
    default: notPortedAt(kWalkerPort, "0x451c00 state 2 sub-state (table 0x88ec10 has 2)", a.subState);
    }
}

void walker(const PlayerEnv& env, AnimatedActor& a) { // exe 0x451c00
    WalkerFields& f = classState<WalkerFields>(a);
    switch (WalkerState(a.state)) {
    case WalkerState::Spawn: spawn(env, a, f); break;
    case WalkerState::Main: mainState(env, a, f); break;
    case WalkerState::Carried: carried(env, a, f); break;
    case WalkerState::Dying: dying(env, a, f); break;
    case WalkerState::Sinking: sinking(env, a, f); break;
    default: notPortedAt(kWalkerPort, "0x451c00 state", a.state);
    }
    if (!(a.flags & kActorLive)) return;
    if (a.anim == a.prevAnim) animStepPose(a); // exe 0x5e1e80 / 0x5e1c30
    else animSetPose(a, a.anim, 0);
    const int8_t b = f.bits;
    const AnimatedActorStats atk = statsOf(a);
    const ShortVec4& bp = bonePoint(a, 0, kWalkerPort);
    f.hitBox.result = 0;
    f.hitBox.previousX = a.prevPos.x.whole;
    f.hitBox.previousZ = a.prevPos.z.whole;
    if (!(b & kWalkerNoHitEntry)) {
        const uint32_t notTarget = (uint32_t(int32_t(b)) & kWalkerNotTarget) << 12; // kHitNoLockOn
        uint32_t param, param2;
        const uint32_t slot = a.slot;
        if (a.state == uint8_t(WalkerState::Dying)) {
            param2 = kHitAttackBump;
            param = slot << 24 | notTarget | 0x408012;
        } else if (!(b & kWalkerThrown)) {
            const uint32_t kind = (b & kWalkerStrongAttack) ? (kHitAttackStrong | kHitAttackThrown) : kHitAttackSolid;
            const uint32_t dmg = (b & kWalkerStrongAttack) ? atk.strongAttack : atk.attack;
            param2 = walkerAttackBits[a.spawnByte] | kind | dmg;
            param = ((slot << 15 | atk.hitHeight) << 9) | notTarget | 0x508012;
        } else {
            param2 = walkerAttackBits[a.spawnByte] | atk.bodyAttack | kHitAttackByPlayer;
            param = slot << 24 | 0x208012;
        }
        const int16_t pos[3] = {bp.x, bp.y, bp.z};
        hitListSubmit(pos, f.hitBox, param, param2);
    }
    // exe 0x5f20f0(actor, 0x30, 0x160, 0): the shadow (render output), skipped while state / sub (u16 +8) == 2
}

} // namespace

void reaverbotWalkerUpdate(const PlayerEnv& env, uint32_t actor) { walker(env, animatedActorAt(actor)); }

} // namespace od::port
