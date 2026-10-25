// The field reaverbot class 0x450310 (see field_reaverbot.hpp). States [0x88ea64]: 0 0x4504c0 bind, 1 0x450640 rise
// (sub-states [0x88eaa4]: 0x450680 out of the ground, 0x450780 face the anchor), 2 0x4507f0 main (14 sub-states
// [0x88eaac]), 3 0x451530 carried (sub-states [0x88eaf4]: 0x451550 at the hands, 0x451660 thrown / dropped), 4 0x451790
// dying, 5 0x4518f0 sinking away. Its stat row is the type's row of its spawn byte (game/actor_stats); its behaviour
// (the main state's next sub-state row [0x88eb1c]) comes from its variant & 0xf.
// Deviation: notPorted where the exe would read through a null pointer (a bone point missing, the lobbed shot's pool
// full while the flash is still spawned at its position).
#include "game/actor_state.hpp"
#include "game/field_alias.hpp"
#include "game/field_reaverbot.hpp"
#include "game/field_reaverbot_state.hpp"

#include "engine/data_source.hpp"
#include "game/actor_animation.hpp"
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
#include "game/territory.hpp"

namespace od::port {

using namespace field_reaverbot_state;

namespace {

constexpr const char* kFieldReaverbotPort = "field reaverbot port"; // the area of its port-gap errors

// +0x161
constexpr uint8_t kFieldWallX = 0x01, kFieldWallZ = 0x02, kFieldLanded = 0x04, kFieldGroundLost = 0x08;
constexpr uint8_t kFieldWalls = kFieldWallX | kFieldWallZ;
// +0x162
constexpr uint8_t kFieldNoHitEntry = 0x01, kFieldNoDamage = 0x02, kFieldNoWallTurn = 0x04, kFieldNoFall = 0x08;
constexpr uint8_t kFieldThrown = 0x10, kFieldStrongAttack = 0x20, kFieldBounces = 0x40, kFieldNotTarget = 0x80;
// +0x160
constexpr uint8_t kFieldCircles = 0, kFieldHopper = 3;
// +1 bit 5: no attack starts, no sinking away (the encounter's off-screen bit)
constexpr uint8_t kRunFlagNoAttack = kActorOffScreen;

enum class FieldState : uint8_t {
    Bind = 0,    // exe 0x4504c0
    Rise = 1,    // exe 0x450640
    Main = 2,    // exe 0x4507f0
    Carried = 3, // exe 0x451530
    Dying = 4,   // exe 0x451790
    Sinking = 5, // exe 0x4518f0
};
enum class FieldRiseSub : uint8_t { OutOfGround = 0, FaceAnchor = 1 }; // exe 0x450680 / 0x450780
enum class FieldSub : uint8_t {
    Reset = 0x0,      // exe 0x450900
    Circle = 0x1,     // exe 0x450980
    GiveWay = 0x2,    // exe 0x450b70
    WallTurn = 0x3,   // exe 0x450c90
    Lunge = 0x4,      // exe 0x450da0
    Hop = 0x5,        // exe 0x450f20
    Fall = 0x6,       // exe 0x450d40
    Patrol = 0x7,     // exe 0x4529a0
    WalkOn = 0x8,     // exe 0x451040
    ReturnHome = 0x9, // exe 0x451090
    Flinch = 0xa,     // exe 0x451180
    KnockBack = 0xb,  // exe 0x451210
    KnockAside = 0xc, // exe 0x451310
    Fire = 0xd,       // exe 0x4513b0
};
enum class FieldCarriedSub : uint8_t { InHands = 0, Flying = 1 }; // exe 0x451550 / 0x451660

// The parameters of the glows it attaches (a stage effect of its own stage's table)
struct FieldGlowParams {
    explicit FieldGlowParams(const EffectActor& actor) : FieldGlowParams(const_cast<EffectActor&>(actor)) {}
    explicit FieldGlowParams(EffectActor& actor) : owner(actor.params[0]), part(actor.params[1]), size(actor.params, 2) {}
    uint8_t& owner; // the reaverbot's slot
    uint8_t& part;
    BytesField<uint16_t, 4> size;
};

const DataTable<CollisionBoxRecord> fieldBox{TableId::FieldReaverbotBox};
const DataTable<InteractionBoxRecord> fieldLiftBox{TableId::FieldReaverbotLiftBox};
const DataTable<ScalarTable<uint8_t, 8>> fieldModels{TableId::FieldReaverbotModels}; // by the spawn byte
const DataTable<ScalarTable<uint32_t, 8>> fieldAttackBits{TableId::FieldReaverbotAttackBits}; // by the spawn byte: the attack word's group immunities
const DataTable<ScalarTable<uint8_t, 0x10>> fieldBehaviours{TableId::FieldReaverbotBehaviours}; // by variant & 0xf
const DataTable<ScalarTable<uint8_t, 0x10>> fieldNextSubs{TableId::FieldReaverbotNextSubs}; // by the behaviour
const DataTable<ScalarTable<int8_t, 0x10>> wallTurnRates{TableId::FieldReaverbotWallTurnRates};
const DataTable<ScalarTable<int8_t, 0x10>> homeTurnRates{TableId::FieldReaverbotHomeTurnRates};
const DataTable<ScalarTable<uint8_t, 0x10>> attackCooldowns{TableId::FieldReaverbotAttackCooldowns};
constexpr uint8_t kAimedVariant = 0x80;         // variant bit: it fires (the lobbed shot) instead of lunging
constexpr uint8_t kGlowSpawnByte = 3;           // the spawn byte whose reaverbot carries the glows
constexpr uint8_t kAttachmentStageType = 0x10;  // the stage effect type of its attached glows (ST0E: 0x4370b0)
constexpr int16_t kWalkSpeed = -0x180, kWalkDecel = -0x10, kPatrolSpeed = -0x240, kLungeSpeed = -0x300;
constexpr int16_t kKnockBackSpeed = -0x2a0;
constexpr int16_t kGravity = 0x40;
constexpr int16_t kRiseStep = 0xc;      // a tick's rise out of the ground
constexpr int16_t kBuriedDepth = 0xb4;  // how far below the ground it starts / sinks before it is freed
constexpr uint8_t kFarTicks = 0x1e;     // the ticks far from the player before it sinks away
constexpr int kFarDistance = 0x601;
constexpr uint16_t kSoundLand = 0xc4, kSoundLunge = 0xa6, kSoundHurt = 0x8d, kSoundStep = 0xa5, kSoundSink = 0xa4;
constexpr uint16_t kSoundRise = 0xa3;
constexpr uint8_t kSparkLoops = 2;
constexpr uint16_t kTintDyingBlink = 0x4214; // every other tick of the dying flash

AnimatedActorStats statsOf(const AnimatedActor& a) { return animatedStats(a.type, a.spawnByte); }
// the type's hit points of its spawn byte's row (0 without a row)
uint16_t rowHitPoints(const AnimatedActor& a) { return hasAnimatedStats(a.type) ? statsOf(a).hitPoints : 0; }
void toSub(AnimatedActor& a, FieldSub s) {
    a.subState = uint8_t(s);
    a.enterStep(0);
}
// exe 0x4519f0: the behaviour's sub-state
void toNextSub(AnimatedActor& a, FieldReaverbotFields& f) {
    const uint8_t s = fieldNextSubs[f.behaviour];
    a.enterStep(0);
    a.subState = s;
}

// exe 0x451a10: the damage of the hit box -- a flinch on the player's attacks (one hit less), knocked back / aside on
// the special weapons' pushes; dead or no hits left -> the knock-back (-1)
int hurt(AnimatedActor& a, FieldReaverbotFields& f) {
    const uint32_t v = f.hitBox.result;
    const uint32_t dmg = v & kHitDamageMask;
    if (dmg == 0) return 0;
    uint32_t take = 0;
    if (v & (kHitAttackByPlayer | kHitAttackThrown | kHitAttackStrong)) {
        f.heading = f.hitBox.contact.x;
        --f.hitsLeft;
        a.tint = kTintFlash;
        a.subState = uint8_t(FieldSub::Flinch);
        a.enterStep(0);
        take = dmg;
    }
    if (v & (kHitLevelBelow | kHitLevelWithin | kHitLevelAbove)) {
        f.heading = f.hitBox.contact.x;
        if (v & kHitLevelBelow) {
            a.subState = uint8_t(FieldSub::KnockBack);
            a.enterStep(0);
            a.tint = kTintFlash;
        } else if (v & kHitLevelWithin) {
            a.subState = uint8_t(FieldSub::KnockAside);
            a.enterStep(0);
            a.tint = kTintFlash;
        }
        take = dmg;
    }
    if (hitPointsAdd(a, -int(take)) == 0 && f.hitsLeft >= 1) return 0;
    a.subState = uint8_t(FieldSub::KnockBack);
    a.enterStep(0);
    return -1;
}
// exe 0x451ad0: the walls / the territory / the ground -> the contact bits; 1 = the ground was lost (> 0x10 below)
int collide(const PlayerEnv& env, AnimatedActor& a, FieldReaverbotFields& f) {
    f.contact = 0;
    uint32_t r = 0;
    if (int32_t(actorTestMove(env, a)) < 0) { // exe 0x40f570
        r = actorPushOut(env, a);              // exe 0x410960
        if (int32_t(r) < 0) a.pos = a.prevPos;
        if (r & 0x300) f.contact |= kFieldWallX;
        if (r & 0xc00) f.contact |= kFieldWallZ;
        if ((f.bits & kFieldBounces) && reflectRotationOffWalls(a.rotation, r) != 0) // exe 0x410740
            f.contact |= kFieldLanded;
    } else {
        const int z = territoryViolated(a.pos); // exe 0x5e6ab0
        if (z != 0) {
            r = territoryPushBack(a, z); // exe 0x5e6b40
            if (r & 0x300) f.contact |= kFieldWallX;
            if (r & 0xc00) f.contact |= kFieldWallZ;
        }
    }
    const int g = actorGround(env, a); // exe 0x40f190
    if (a.airborne != 0) {
        if (!(f.bits & kFieldBounces) && (r & 0x3000)) {
            a.airborne = 0;
            a.pos.y.whole = int16_t(g);
            f.contact |= kFieldLanded;
        }
        return 0;
    }
    if (g - a.pos.y.whole > 0x10) {
        f.contact |= kFieldGroundLost;
        return 1;
    }
    a.pos.y.whole = int16_t(g);
    return 0;
}
// exe 0x451720(actor, yaw): the airborne step -- landed: halved / bounced, the sound, 1; else the move along the yaw and
// the gravity
int airStep(AnimatedActor& a, FieldReaverbotFields& f, int yaw) {
    if (f.contact & kFieldLanded) {
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
// exe 0x450920: the walk animation, its speed and deceleration, a random side, the scale 1.0, the far ticks
void walkReset(AnimatedActor& a, FieldReaverbotFields& f) {
    a.anim = 1;
    a.velocity.setXY(0, 0);
    a.velocity.z = kWalkSpeed;
    a.acceleration.z = kWalkDecel;
    const uint32_t r = rngStep();
    f.neighbour = uint16_t(r & 1);
    resetModelScale(a); // exe 0x4e5cd0
    a.runFlags &= uint8_t(~kActorRunMarked);
    f.farTicks = int8_t(kFarTicks);
}
// the ground debris of the rise / the sinking (exe 0x450680 / 0x4518f0): dust at a random bone 1..4 every third tick,
// two ground effects on the first
void groundDebris(AnimatedActor& a, FieldReaverbotFields& f, int g) {
    if (int(f.timer) % 3 != 0) return;
    const int32_t kind = groundKindUnder(a.pos); // exe 0x5e6240
    const uint32_t r = rngStep();
    const ShortVec4& bp = bonePoint(a, (r & 3) + 1, kFieldReaverbotPort); // exe 0x5e1810
    spawnDustSpark(bp.x, wrap16(g - 10), bp.z);                         // exe 0x5e62c0
    if (f.timer == 0) {
        spawnStepDust(a.pos.x.whole, wrap16(g), a.pos.z.whole, kind); // exe 0x5e6300
        spawnStepDust(a.pos.x.whole, wrap16(g), a.pos.z.whole, kind);
    }
}

// ---- state 0 --------------------------------------------------------------------------------------------------------
// exe 0x4505f0(actor, part, size): a stage effect (type 0x10, variant 1) attached to the reaverbot
void attachGlow(AnimatedActor& a, uint8_t part, uint16_t size) {
    const uint32_t address = allocActorA0(kActorStageClass); // exe 0x5e13a0
    if (!address) return;
    EffectActor& e = effectActorAt(address);
    e.flags = kActorLive;
    e.type = kAttachmentStageType;
    e.loopFlags |= kActorSweepGroup2;
    e.variant = 1;
    e.pos.x = 0;
    FieldGlowParams g(e);
    g.owner = a.slot;
    g.part = part;
    g.size = size;
    e.pos.y = 0;
    e.pos.z = 0;
}
void bind(const PlayerEnv& env, AnimatedActor& a, FieldReaverbotFields& f) { // exe 0x4504c0
    if (!bindActorModel(a, fieldModels[a.spawnByte])) { // exe 0x5e0bc0
        freeActor(a);                                   // exe 0x5e1490
        return;
    }
    a.flags |= kActorDrawn;
    a.anim = 4;
    a.prevAnim = 0xff;
    a.runFlags |= uint8_t(kActorWarpKept | kActorRunMarked);
    const uint16_t hp = rowHitPoints(a);
    a.maxHitPoints = int16_t(hp);
    a.hitPoints = int16_t(hp);
    a.collisionBox.pointAt(fieldBox.stateAddress());
    a.interactionBox.pointAt(fieldLiftBox.stateAddress());
    a.interaction = kInteractLift;
    a.airborne = 0;
    a.onPlatform = 0;
    a.clearClassBlock(); // exe 0x40dc40
    f.hitBox.previousX = a.pos.x.whole;
    f.hitBox.previousZ = a.pos.z.whole;
    f.hitBox.result = 0;
    f.hitsLeft = 3;
    f.farTicks = int8_t(kFarTicks);
    a.pos.y.whole = wrap16(actorGround(env, a) + kBuriedDepth);
    f.behaviour = fieldBehaviours[a.variant & 0xf];
    f.group = int8_t(enemyGroupFindFor(a, 0));
    if (a.spawnByte == kGlowSpawnByte) {
        attachGlow(a, 0, 0x8c);
        attachGlow(a, 1, 0x40);
        attachGlow(a, 3, 0x40);
    }
    const uint8_t slot = a.slot;
    a.state = uint8_t(FieldState::Rise);
    a.subState = 0;
    a.step = 0;
    a.stepCounter = 0;
    registerEnemy(slot, 0);
}

// ---- state 1 --------------------------------------------------------------------------------------------------------
// exe 0x450680: up out of the ground (kRiseStep a tick, not above the ground) until the animation ends
void outOfGround(const PlayerEnv& env, AnimatedActor& a, FieldReaverbotFields& f) {
    if (a.step == 0) {
        f.bits = 0;
        a.enterStep(1);
        f.timer = 0;
        soundPlayAtPos(kSoundRise, a.pos);
    }
    a.pos.y.whole -= uint32_t(kRiseStep);
    const int g = actorGround(env, a);
    if (a.pos.y.whole < g) a.pos.y.whole = int16_t(g);
    if (a.animEnded()) {
        a.subState = uint8_t(FieldRiseSub::FaceAnchor);
        a.enterStep(0);
    }
    groundDebris(a, f, g);
    ++f.timer;
}
// exe 0x450780: one tick turning toward the encounter's anchor bearing, then the main state
void faceAnchor(const PlayerEnv& env, AnimatedActor& a, FieldReaverbotFields& f) {
    if (a.step == 0) {
        f.bits = 0;
        a.step = 1;
        f.timer = 1;
        a.anim = 0;
    }
    turnYawTo(a.rotation.y, uint32_t(encounterFieldAnchorAngle(a)), 0x20); // exe 0x5e6450, 0x5e3df0
    --f.timer;
    if (f.timer == 0) a.enterState(uint8_t(FieldState::Main));
    collide(env, a, f);
}
void rise(const PlayerEnv& env, AnimatedActor& a, FieldReaverbotFields& f) { // exe 0x450640
    a.tint = kTintNeutral;
    switch (FieldRiseSub(a.subState)) {
    case FieldRiseSub::OutOfGround: outOfGround(env, a, f); break;
    case FieldRiseSub::FaceAnchor: faceAnchor(env, a, f); break;
    default: notPortedAt(kFieldReaverbotPort, "0x450310 state 1 sub-state (table 0x88eaa4 has 2)", a.subState);
    }
    if (a.animKey.events & kAnimEventFootfall) soundPlayAtPos(kSoundStep, a.pos);
}

// ---- state 2 sub-states ---------------------------------------------------------------------------------------------
void resetWalk(AnimatedActor& a, FieldReaverbotFields& f) { // exe 0x450900
    walkReset(a, f);
    f.bits = 0;
    toSub(a, FieldSub::Circle);
}
void circle(AnimatedActor& a, FieldReaverbotFields& f) { // exe 0x450980: around the player; a lunge / a shot / a hop
    if (a.step == 0) {
        f.bits = 0;
        a.anim = 1;
        a.enterStep(1);
    }
    const int head = headingFromPlayer(a, neighbourSide(f.neighbour), 500, 700); // exe 0x5e5cf0
    if (stepTowardLimit(a.velocity.z, 0xfff0, 0x180) != 0) a.velocity.z = kWalkSpeed;
    turnYawTo(a.rotation.y, uint32_t(head), 0x20);
    integrateVelocity(a, 0, 0, a.velocity.z);
    int found = int(neighbourFind(a, f.neighbour, 0)); // exe 0x5e5e00
    if (found != 0) toSub(a, FieldSub::GiveWay);
    const Player& p = player();
    const int dist = planarDistance(p.pos, a.pos); // exe 0x5e3b00
    if (f.cooldown != 0) --f.cooldown;
    if (f.cooldown == 0 && ((1u << (rngStep() & 0xf)) & 0x2c0u)) {
        if (a.variant & kAimedVariant) {
            found = angleDiff(uint32_t(int32_t(a.rotation.y)), uint32_t(angleOf(p.pos, a.pos)));
            if (uint32_t(found + 0x180) < 0x300u && dist < 0x578 && !(a.runFlags & kRunFlagNoAttack) &&
                !(enemyGroupMarks(f.group, EnemyGroupMarks::Either) & 1)) {
                enemyGroupMark(f.group, 1);
                toSub(a, FieldSub::Fire);
            }
        } else if (dist < 700 && !(a.runFlags & kRunFlagNoAttack) &&
                   !(enemyGroupMarks(f.group, EnemyGroupMarks::Either) & 1)) {
            enemyGroupMark(f.group, 1);
            toSub(a, FieldSub::Lunge);
        }
    }
    if (f.behaviour == kFieldHopper && found == 0) {
        const uint32_t r = rngStep();
        if (((1u << (r & 0xf)) & 0x80) && dist > 0x600) toSub(a, FieldSub::Hop);
    }
}
void giveWay(AnimatedActor& a, FieldReaverbotFields& f) { // exe 0x450b70: by how the neighbour moves (exe 0x5e5f70)
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
        if (n & kNeighbourOpposite) target = headingFromOther(a, other, neighbourSide(f.neighbour), 300); // exe 0x5e5d90
        if (n & kNeighbourCrossingSlow) accelerateToward(a, 0x200);                                     // exe 0x5e5450
        if (n & kNeighbourCatching) decelerateToward(a, 0x100);                                         // exe 0x5e54a0
        if (other.flags == 0 || ((uint16_t(other.pos.x.whole) ^ uint16_t(a.pos.x.whole)) & 0xff00) ||
            ((uint16_t(other.pos.z.whole) ^ uint16_t(a.pos.z.whole)) & 0xff00))
            a.enterStep(2);
    }
    turnYawTo(a.rotation.y, uint32_t(target), 0x40);
    integrateVelocity(a, 0, 0, a.velocity.z);
}
void wallTurn(AnimatedActor& a, FieldReaverbotFields& f) { // exe 0x450c90: a wall -- turn at a random rate until free
    if (a.step == 0) {
        a.enterStep(1);
        f.bits = kFieldNoWallTurn;
        const int was = a.prevRotation.y, now = a.rotation.y;
        const uint32_t r = rngStep();
        const int8_t rate = wallTurnRates[r & 0xf];
        f.heading = int16_t(rate);
        if (now != was) {
            if (now - was <= -1) f.heading = int16_t(-int32_t(rate));
        } else if (neighbourSide(f.neighbour) & kNeighbourSideRight) {
            f.heading = int16_t(-int32_t(rate));
        }
        a.velocity.z = kWalkSpeed;
        a.anim = 1;
    }
    integrateVelocity(a, 0, 0, a.velocity.z);
    a.rotation.y = int16_t(uint16_t(a.rotation.y) + uint16_t(f.heading));
    if (f.contact & kFieldWalls) return;
    toNextSub(a, f);
}
void lunge(AnimatedActor& a, FieldReaverbotFields& f) { // exe 0x450da0: shake 0x5a ticks facing the player, dash 0xf
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
        const int odd = uint8_t(f.timer) & 1;
        a.scale.x = int16_t((odd + 0x10) * 0x20);
        a.scale.y = int16_t(odd * -0x20 + kModelScaleOne);
        a.scale.z = int16_t((odd + 0x10) * 0x20);
        --f.timer;
        if (f.timer == 0) {
            a.enterStep(2);
            resetModelScale(a);
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
        --f.timer;
        if (f.timer == 0) toSub(a, FieldSub::Reset);
        f.bits |= kFieldStrongAttack;
        break;
    default: break;
    }
    enemyGroupMark(f.group, 1);
}
void hop(AnimatedActor& a, FieldReaverbotFields& f) { // exe 0x450f20: wait 0x1e ticks facing the player, then a hop at it
    switch (a.step) {
    case 0:
        f.bits = kFieldBounces | kFieldNoFall;
        a.enterStep(1);
        a.anim = 0;
        f.timer = 0x1e;
        [[fallthrough]];
    case 1:
        a.rotation.y = int16_t(angleOf(player().pos, a.pos));
        --f.timer;
        if (f.timer == 0) {
            a.enterStep(2);
            resetModelScale(a);
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
        a.velocity.y = int16_t(launchSpeed(a.pos, p.pos, 0x300, kGravity, 0x400)); // exe 0x5e3ee0
        a.velocity.z = kLungeSpeed;
        a.airborne = 1;
    }
        [[fallthrough]];
    case 3:
        f.bits |= kFieldStrongAttack;
        if (airStep(a, f, a.rotation.y)) {
            toNextSub(a, f);
            a.airborne = 0;
            a.velocity.y = 0;
        }
        break;
    default: break;
    }
    enemyGroupMark(f.group, 1);
}
void fall(AnimatedActor& a, FieldReaverbotFields& f) { // exe 0x450d40: the ground lost -- until it lands
    const uint8_t st = a.step;
    if (st == 0) {
        f.bits = kFieldBounces | kFieldNoFall | kFieldNoWallTurn | kFieldNoHitEntry;
        a.step = 1;
        a.anim = 1;
        a.acceleration.y = kGravity;
        resetModelScale(a);
    } else if (st != 1) {
        return;
    }
    if (!airStep(a, f, a.rotation.y)) return;
    a.airborne = 0;
    a.velocity.y = 0;
    toNextSub(a, f);
}
void patrol(AnimatedActor& a, FieldReaverbotFields& f) { // exe 0x4529a0: back and forth along the home yaw
    if (a.step == 0) {
        f.bits = kFieldNoWallTurn;
        f.heading = int16_t(f.patrolCells);
        a.enterStep(1);
        a.anim = 1;
    }
    if (uint16_t(a.rotation.y) != uint16_t(f.homeYaw)) {
        if (stepTowardLimit(a.velocity.z, 0xfff0, 0) != 0) {
            const int yaw = f.homeYaw;
            a.velocity.z = 0;
            if (uint32_t(turnYawTo(a.rotation.y, uint32_t(yaw), 0x40) + 0x40) <= 0x80u) a.rotation.y = f.homeYaw;
        }
    } else if (stepTowardLimit(a.velocity.z, 0xfff0, 0x240) != 0) {
        a.velocity.z = kPatrolSpeed;
        a.stepCounter = 0;
    }
    integrateVelocity(a, 0, 0, a.velocity.z);
    if (((uint16_t(a.prevPos.x.whole) ^ uint16_t(a.pos.x.whole)) & 0xfe00) ||
        ((uint16_t(a.prevPos.z.whole) ^ uint16_t(a.pos.z.whole)) & 0xfe00)) {
        --f.heading;
        if (f.heading < 0) {
            a.stepCounter = 1;
            f.homeYaw = int16_t((uint32_t(uint16_t(f.homeYaw)) - uint32_t(kHalfTurn)) & kAngleMask);
            f.heading = int16_t(int32_t(f.patrolCells) << 1);
            return;
        }
    }
    if (a.stepCounter != 0) return;
    const uint16_t leg = uint16_t(f.homeYaw);
    if (uint32_t(f.contact & kFieldWalls) != uint32_t(2 - ((leg & 0x400) != 0 ? 1 : 0))) return;
    a.stepCounter = 1;
    a.velocity.z = 0;
    f.homeYaw = int16_t((uint32_t(leg) - uint32_t(kHalfTurn)) & kAngleMask);
    f.heading = int16_t(int32_t(f.patrolCells) << 1);
}
void walkOn(AnimatedActor& a, FieldReaverbotFields& f) { // exe 0x451040: on until a wall -> circle
    if (a.step == 0) {
        f.bits = kFieldNoWallTurn;
        a.step = 1;
        a.anim = 1;
    }
    integrateVelocity(a, 0, 0, a.velocity.z);
    if (!(f.contact & kFieldWalls)) return;
    toSub(a, FieldSub::Circle);
}
void returnHome(AnimatedActor& a, FieldReaverbotFields& f) { // exe 0x451090: back toward the home point; the player near
    if (a.step == 0) {
        f.bits = kFieldNoWallTurn;
        a.anim = 1;
        a.enterStep(1);
    }
    if (stepTowardLimit(a.velocity.z, 0xfff0, 0x180) != 0) a.velocity.z = kWalkSpeed;
    const int ang = angleOf(wholeToFixed16(f.homeX), wholeToFixed16(f.homeZ), a.pos.x, a.pos.z);
    const uint32_t r = rngStep();
    turnYawTo(a.rotation.y, uint32_t(ang), homeTurnRates[r & 0xf]);
    integrateVelocity(a, 0, 0, a.velocity.z);
    if (neighbourFind(a, f.neighbour, 0) != 0) toSub(a, FieldSub::GiveWay);
    if (planarDistance(player().pos, a.pos) >= 700) return;
    f.behaviour = kFieldCircles;
    toSub(a, FieldSub::Circle);
}
void flinch(AnimatedActor& a, FieldReaverbotFields& f) { // exe 0x451180: until the animation ends (airborne: it falls)
    if (a.step != 0) {
        if (a.animEnded()) {
            if (a.airborne == 0) toNextSub(a, f);
        }
    } else {
        a.enterStep(1);
        f.bits = kFieldNoWallTurn | kFieldNoDamage;
        a.anim = 0;
        a.velocity.z = 0;
        if (a.airborne != 0) a.acceleration.y = kGravity;
        else a.velocity.y = 0;
        soundPlayAtPos(kSoundHurt, a.pos);
        resetModelScale(a);
    }
    if (a.airborne == 0) return;
    if (!airStep(a, f, a.rotation.y)) return;
    a.airborne = 0;
    a.velocity.y = 0;
}
void knockBack(AnimatedActor& a, FieldReaverbotFields& f) { // exe 0x451210: along the hit's direction; dead -> dying
    if (a.step == 0) {
        a.enterStep(1);
        f.bits = kFieldNoWallTurn | kFieldNoDamage;
        a.anim = 2;
        a.rotation.y = f.heading;
        a.velocity.x = 0;
        a.velocity.z = kKnockBackSpeed;
        if (a.airborne != 0) a.acceleration.y = kGravity;
        else a.velocity.y = 0;
        soundPlayAtPos(kSoundHurt, a.pos);
        resetModelScale(a);
        f.timer = 6;
        f.hitsLeft = 3;
        if (a.hitPoints >= 0) f.bits &= uint8_t(~kFieldNoFall);
        else f.bits |= kFieldNoFall;
    }
    integrateVelocity(a, 0, a.velocity.y, a.velocity.z);
    if (a.hitPoints >= 0) {
        if (f.timer != 0) --f.timer;
        if (f.timer == 0 && a.airborne == 0) toNextSub(a, f);
    } else {
        --f.timer;
        if (f.timer == 0 || a.airborne != 0) a.enterState(uint8_t(FieldState::Dying));
    }
}
void knockAside(AnimatedActor& a, FieldReaverbotFields& f) { // exe 0x451310: along the hit's direction, decelerating
    if (a.step == 0) {
        f.bits = kFieldNoDamage;
        a.enterStep(1);
        a.anim = 0;
        a.velocity.x = 0;
        a.velocity.z = kWalkSpeed;
        a.acceleration.z = kGravity;
        if (a.airborne != 0) a.acceleration.y = kGravity;
        else a.velocity.y = 0;
        soundPlayAtPos(kSoundHurt, a.pos);
        resetModelScale(a);
    }
    moveAlongYaw(a.pos, f.heading, 0, a.velocity.y, a.velocity.z); // exe 0x5e36f0
    a.velocity.z = int16_t(uint16_t(a.velocity.z) + uint16_t(a.acceleration.z));
    if (a.velocity.z < 0) return;
    toNextSub(a, f);
}
// exe 0x4513b0: face the player; at the key 0xb the lobbed shot (its stage projectile type, variant 1, the shot damage,
// no launch sound) and a hit spark (variant 3) at it
void fire(AnimatedActor& a, FieldReaverbotFields& f) {
    switch (a.step) {
    case 0: {
        f.bits = 0;
        a.anim = 3;
        const uint32_t r = rngStep();
        f.cooldown = attackCooldowns[r & 0xf];
        ++a.step;
        return;
    }
    case 1: {
        turnYawTo(a.rotation.y, uint32_t(angleOf(player().pos, a.pos)), 0x20);
        enemyGroupMark(f.group, 1);
        if (a.keyIndex != 0xb) return;
        const uint32_t shot = allocActor40(kActorStageClass); // exe 0x5e1280
        if (shot) {
            ProjectileActor& s = projectileActorAt(shot);
            LobbedShotParams lob(s);
            s.flags = kActorLive;
            s.type = kLobbedShotStageType;
            s.variant = 1;
            s.pos.x = a.pos.x;
            s.pos.y = int32_t(uint32_t(a.pos.y) - 0x400000u);
            s.pos.z = a.pos.z;
            s.rotation.x = 0;
            lob.size = 0x20;
            s.acceleration.y = 0x20;
            s.rotation.y = a.rotation.y;
            s.rotation.z = 0;
            s.velocity.x = 0;
            s.velocity.y = -0x100;
            s.velocity.z = -0x200;
            lob.damage = int16_t(statsOf(a).shotAttack);
            lob.launchSound = -1;
        }
        const uint32_t effect = allocActorA0(0); // exe 0x5e13b0: a flash at the shot
        if (effect) {
            if (!shot) notPorted(kFieldReaverbotPort, "0x4513b0: the flash reads the shot's position with the pool-0x40 shot null");
            const ProjectileActor& s = projectileActorAt(shot);
            EffectActor& e = effectActorAt(effect);
            e.flags = kActorLiveDrawn;
            e.loopFlags |= kActorSweepGroup2;
            e.type = typeByte(GlobalEffectType::HitSpark);
            e.variant = 3;
            e.pos.x = s.pos.x.whole;
            e.pos.y = s.pos.y.whole;
            e.pos.z = s.pos.z.whole;
            classState<HitSparkFields>(e).size = 0xc0;
        }
        ++a.step;
        return;
    }
    case 2:
        if (a.animEnded()) toSub(a, FieldSub::Reset);
        return;
    default: return;
    }
}

void mainState(const PlayerEnv& env, AnimatedActor& a, FieldReaverbotFields& f) { // exe 0x4507f0
    a.tint = kTintNeutral;
    if (!(f.bits & kFieldNoDamage)) hurt(a, f);
    switch (FieldSub(a.subState)) {
    case FieldSub::Reset: resetWalk(a, f); break;
    case FieldSub::Circle: circle(a, f); break;
    case FieldSub::GiveWay: giveWay(a, f); break;
    case FieldSub::WallTurn: wallTurn(a, f); break;
    case FieldSub::Lunge: lunge(a, f); break;
    case FieldSub::Hop: hop(a, f); break;
    case FieldSub::Fall: fall(a, f); break;
    case FieldSub::Patrol: patrol(a, f); break;
    case FieldSub::WalkOn: walkOn(a, f); break;
    case FieldSub::ReturnHome: returnHome(a, f); break;
    case FieldSub::Flinch: flinch(a, f); break;
    case FieldSub::KnockBack: knockBack(a, f); break;
    case FieldSub::KnockAside: knockAside(a, f); break;
    case FieldSub::Fire: fire(a, f); break;
    default: notPortedAt(kFieldReaverbotPort, "0x450310 state 2 sub-state (table 0x88eaac has 14)", a.subState);
    }
    if (a.animKey.events & kAnimEventFootfall) soundPlayAtPos(kSoundStep, a.pos);
    bool restart = false;
    if (collide(env, a, f)) {
        a.airborne = 1;
        if (!(f.bits & kFieldNoFall)) {
            a.subState = uint8_t(FieldSub::Fall);
            restart = true;
        }
    } else if ((f.contact & kFieldWalls) && !(f.bits & kFieldNoWallTurn)) {
        a.subState = uint8_t(FieldSub::WallTurn);
        restart = true;
    }
    if (restart) a.restartStep();
    if (a.hitPoints >= 0 && playerLiftTargetCheck(a, 0)) { // exe 0x573a60
        a.enterState(uint8_t(FieldState::Carried));
        f.bits |= kFieldNoHitEntry;
        a.runFlags |= kActorRunMarked;
    }
    if (!(a.runFlags & kRunFlagNoAttack) && a.airborne == 0) {
        if (planarDistance(player().pos, a.pos) >= kFarDistance) {
            --f.farTicks;
            if (f.farTicks <= 0) {
                a.enterState(uint8_t(FieldState::Sinking));
                return;
            }
        } else {
            f.farTicks = int8_t(kFarTicks);
        }
    }
}

// ---- state 3 --------------------------------------------------------------------------------------------------------
void thrown(const PlayerEnv& env, AnimatedActor& a, FieldReaverbotFields& f);
void inHands(const PlayerEnv& env, AnimatedActor& a, FieldReaverbotFields& f) { // exe 0x451550: until released
    if (a.step == 0) {
        f.bits = kFieldBounces | kFieldNoHitEntry;
        a.step = 1;
        a.anim = 5;
        resetModelScale(a);
        encounterUntrack(a); // exe 0x5e6540
    }
    carryAtHands(a, 0, 0, 0);               // exe 0x573ab0
    const CarryRelease r = carryReleased(); // exe 0x573950
    if (r == CarryRelease::Held) return;
    f.bits &= uint8_t(~kFieldNoHitEntry);
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
        f.bits |= kFieldThrown;
    }
    a.acceleration.y = kGravity;
    a.rotation.y = f.heading;
    a.airborne = 1;
    carryDropFromPlayer(a); // exe 0x573990
    a.subState = uint8_t(FieldCarriedSub::Flying);
    a.enterStep(0);
    thrown(env, a, f);
}
// exe 0x451660: flying; a solid hit while alive bounces it back (dead); landed twice -> the main state / dying
void thrown(const PlayerEnv& env, AnimatedActor& a, FieldReaverbotFields& f) {
    if ((f.hitBox.result & kHitAttackSolid) && a.hitPoints >= 0) {
        a.velocity.z = int16_t(uint32_t(-int32_t(a.velocity.z)) >> 1);
        a.hitPoints = -1;
        spawnHitSpark(kSparkLoops, a.pos.x.whole, wrap16(a.pos.y.whole - 0x40), a.pos.z.whole);
    }
    if (airStep(a, f, a.rotation.y)) {
        if (a.stepCounter == 0) {
            a.stepCounter = 1;
            a.anim = 2;
            integrateVelocity(a, 0, a.velocity.y, a.velocity.z);
            collide(env, a, f);
            return;
        }
        a.enterState(uint8_t(a.hitPoints >= 0 ? FieldState::Main : FieldState::Dying));
        a.velocity.y = 0;
        a.airborne = 0;
    }
    collide(env, a, f);
}
void carried(const PlayerEnv& env, AnimatedActor& a, FieldReaverbotFields& f) { // exe 0x451530
    switch (FieldCarriedSub(a.subState)) {
    case FieldCarriedSub::InHands: inHands(env, a, f); break;
    case FieldCarriedSub::Flying: thrown(env, a, f); break;
    default: notPortedAt(kFieldReaverbotPort, "0x450310 state 3 sub-state (table 0x88eaf4 has 2)", a.subState);
    }
}

// ---- states 4 / 5 ---------------------------------------------------------------------------------------------------
// exe 0x451790: the flash, an explosion, the drops after 0x14 / 8 ticks, freed
void dying(const PlayerEnv& env, AnimatedActor& a, FieldReaverbotFields& f) {
    a.tint = kTintFlash;
    if (a.subState == 0) {
        a.subState = 1;
        f.timer = 0x14;
        if (a.airborne != 0) {
            f.timer = 8;
        } else {
            a.velocity.x = 0;
            a.velocity.y = 0;
            a.velocity.z = 0;
        }
        a.acceleration.y = kGravity;
        a.anim = 2;
        const uint32_t address = allocActor60(0); // exe 0x5e1300: the explosion (game/explosions)
        if (address) {
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
        countDefeat(); // exe 0x5e53e0
    }
    if (uint8_t(f.timer) & 1) a.tint = kTintDyingBlink;
    if (f.timer <= 4) f.bits |= kFieldNotTarget;
    --f.timer;
    if (f.timer == 0) {
        spawnDrops(a.pos, statsOf(a).dropTable); // exe 0x5f1270
        freeActor(a);                            // exe 0x5e1490
        return;
    }
    integrateVelocity(a, a.velocity.x, a.velocity.y, a.velocity.z);
    if (a.airborne != 0) a.velocity.y = int16_t(uint16_t(a.velocity.y) + uint16_t(a.acceleration.y));
    collide(env, a, f);
    if (a.wasAirborne != 0 && a.airborne == 0) {
        a.velocity.z = 0;
        a.velocity.x = 0;
    }
}
// exe 0x4518f0: far from the player -- down into the ground 0x10 a tick with the debris, freed kBuriedDepth below it
void sinking(const PlayerEnv& env, AnimatedActor& a, FieldReaverbotFields& f) {
    if (a.subState == 0) {
        a.anim = 4;
        f.timer = 0;
        soundPlayAtPos(kSoundSink, a.pos);
        a.subState = 1;
        return;
    }
    a.pos.y += 0x100000u;
    const int g = actorGround(env, a);
    groundDebris(a, f, g);
    ++f.timer;
    if (g < a.pos.y.whole - kBuriedDepth) {
        encounterUntrack(a); // exe 0x5e6540
        freeActor(a);        // exe 0x5e1490
    }
}

void fieldReaverbot(const PlayerEnv& env, AnimatedActor& a) { // exe 0x450310
    FieldReaverbotFields& f = classState<FieldReaverbotFields>(a);
    a.prevAnim = a.anim;
    a.savePreviousPose();
    a.savePreviousContact();
    switch (FieldState(a.state)) {
    case FieldState::Bind: bind(env, a, f); break;
    case FieldState::Rise: rise(env, a, f); break;
    case FieldState::Main: mainState(env, a, f); break;
    case FieldState::Carried: carried(env, a, f); break;
    case FieldState::Dying: dying(env, a, f); break;
    case FieldState::Sinking: sinking(env, a, f); break;
    default: notPortedAt(kFieldReaverbotPort, "0x450310 state (table 0x88ea64 has 6)", a.state);
    }
    if (!(a.flags & kActorLive)) return;
    if (a.anim != a.prevAnim) animSetPose(a, a.anim, 0); // exe 0x5e1c30
    else animStepPose(a);                                // exe 0x5e1e80
    const uint8_t b = f.bits;
    const AnimatedActorStats atk = statsOf(a);
    const ShortVec4* point = bonePointOrNull(a, 0); // exe 0x5e1810 (read only by the submit below)
    f.hitBox.result = 0;
    f.hitBox.previousX = a.prevPos.x.whole;
    f.hitBox.previousZ = a.prevPos.z.whole;
    if (!(b & kFieldNoHitEntry)) {
        if (!point) notPorted(kFieldReaverbotPort, "0x450310: exe 0x5e1810 returned null for the hit entry (bone 0)");
        uint32_t param = uint32_t(b & kFieldNotTarget) << 12, param2; // kHitNoLockOn
        const uint32_t slot = a.slot;
        const uint32_t immunity = fieldAttackBits[a.spawnByte];
        if (a.state == uint8_t(FieldState::Dying)) {
            param2 = kHitAttackBump;
            param = slot << 24 | param | 0x408012;
        } else if (b & kFieldThrown) {
            param2 = immunity | atk.bodyAttack | kHitAttackByPlayer;
            param = slot << 24 | 0x208012;
        } else {
            const uint32_t kind = (b & kFieldStrongAttack) ? (kHitAttackStrong | kHitAttackThrown) : kHitAttackSolid;
            const uint32_t dmg = (b & kFieldStrongAttack) ? atk.strongAttack : atk.attack;
            param2 = immunity | kind | dmg;
            param = ((slot << 15 | atk.hitHeight) << 9) | param | 0x508012;
        }
        const ShortVec4& bp = *point;
        const int16_t pos[3] = {bp.x, bp.y, bp.z};
        hitListSubmit(pos, f.hitBox, param, param2); // exe 0x5e4120
    }
    // exe 0x5f20f0(actor, 0x30, 0x160, 0): the shadow (render output), skipped while state / sub (u16 +8) == 3
}

} // namespace

void fieldReaverbotUpdate(const PlayerEnv& env, uint32_t actor) { fieldReaverbot(env, animatedActorAt(actor)); }

} // namespace od::port
