// The hopping Reaverbot of the Saul Kada area (ST26 / ST27 / ST34: pool 0x20 type 0x64, exe 0x4eadf0; hypothesis
// names), one routine per exe routine.
//   States [0x89658c]: 0 bind 0x4eaec0, 1 run 0x4eb000 (the sub-states [0x896598]), 2 freed 0x468fa0.
//   Sub-states: 0 hop in place 0x4eb390, 1 lifted / thrown by the player 0x4eb420 ([0x8965c8]: 0x4eb440 carried,
//   0x4eb500 flight), 2 death 0x4eb560 (a blast), 3 chase 0x4eb600, 4 fall 0x4eb6c0, 5 jump attack 0x4eb740, 6 give way
//   to a neighbour 0x4eb880, 7 knocked over / spinning 0x4eb9d0, 8 wander 0x4ebb20, 9 circle the target 0x4ebbc0,
//   10 hop + pick a wander heading 0x4ebd90, 11 grabbed by a partner 0x4ebef0.
//   Variant: 0 hops toward the player (chase < 0x400, jump attack < 0x100), 1 wanders around its spawn point (radius
//   wanderRange << 4), 2 starts in the air in sub-state 10 and targets the player or the partners Tron / Bon (pool
//   0x20 types 0x48 / 0x51; dies on landing from a fall / at y >= 0x30). Lifted and thrown (or launched by a partner)
//   it dies on its first wall / landing / hit; damage past its row's threshold knocks it over into a spin.
// The shadow (exe 0x5f20f0) is render output.
#include "content/saul_kada/hopping_reaverbot.hpp"
#include "content/saul_kada/hopping_reaverbot_state.hpp"
#include "content/saul_kada/st27_partners.hpp"

#include "engine/data_source.hpp"
#include "game/actor_animation.hpp"
#include "game/actor_model.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actor_stats.hpp"
#include "game/angles.hpp"
#include "game/enemy_motion.hpp"
#include "game/audio_logic.hpp"
#include "game/carry.hpp"
#include "game/collision.hpp"
#include "game/cutscene_actor.hpp"
#include "game/distances.hpp"
#include "game/enemy_neighbours.hpp"
#include "game/explosions.hpp"
#include "game/game_state.hpp"
#include "game/global_class_types.hpp"
#include "game/hit_list.hpp"
#include "game/hit_points.hpp"
#include "game/player/player_motion.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/story.hpp"

namespace od::port {

using namespace hopping_reaverbot_state;

namespace {

constexpr uint8_t kContactWall = 0x01, kContactLedge = 0x02, kContactLanded = 0x04;
constexpr uint8_t kHopAirborne = 0x02;
constexpr uint8_t kHopLifted = 0x04;
constexpr uint8_t kHopDead = 0x08;        // dying: a harmless box, no more hits
constexpr uint8_t kHopAttackBox = 0x10;   // the jump attack's box
constexpr uint8_t kHopReaim = 0x20;       // the circling picks a new heading
constexpr uint8_t kHopKeepChasing = 0x40; // bumped twice while circling: chases for good
constexpr uint8_t kHopThrown = 0x80;      // thrown: collides as the shots' slot class
constexpr uint8_t kThrownSlot = 0x10;

enum class HopState : uint8_t { Bind = 0, Run = 1, Free = 2 };
// [0x896598]
enum class HopSub : uint8_t {
    Hop = 0, Lifted = 1, Death = 2, Chase = 3, Fall = 4, JumpAttack = 5, GiveWay = 6, Spin = 7, Wander = 8,
    Circle = 9, HopPick = 10, Grabbed = 11,
};
// [0x8965c8]
enum class LiftedStep : uint8_t { Carried = 0, Flight = 1 };
enum class DeathStep : uint8_t { Blast = 0, Hide = 1, Wait = 2 };
enum class JumpStep : uint8_t { Air = 0, Land = 1, Rest = 2 };
enum class SpinStep : uint8_t { Start = 0, FirstBounce = 1, SecondBounce = 2, StartSpin = 3, Spinning = 4 };
enum class HopPickStep : uint8_t { Hop = 0, Wander = 1 };
enum class GrabbedStep : uint8_t { Held = 0, Carried = 1 };
enum class HopVariant : uint8_t { Chaser = 0, Wanderer = 1, Hunter = 2 };
enum HopAnim : uint8_t {
    kAnimHop = 0, kAnimRun = 1, kAnimJump = 2, kAnimLand = 3, kAnimRest = 4, kAnimKnocked = 5, kAnimLifted = 6,
    kAnimGrabbed = 7, kAnimTumble = 8, kAnimSpin = 9,
};

const DataTable<ScalarTable<int16_t, 8>> wanderHeadings{TableId::HoppingReaverbotWanderHeadings}; // the variant-1 headings around its wander heading
const DataTable<ScalarTable<int16_t, 8>> circleOffsets{TableId::HoppingReaverbotCircleOffsets}; // the circling's offsets from the bearing to the target
const DataTable<ScalarTable<uint8_t, 16>> pickedHeadings{TableId::HoppingReaverbotPickedHeadings}; // sub-state 10's wander headings (every other byte)
const DataTable<ScalarTable<uint8_t, 8>> pickedRanges{TableId::HoppingReaverbotPickedRanges}; // ... and ranges
const DataTable<ScalarTable<int16_t, 16>> landingRise{TableId::HoppingReaverbotLandingRise}; // the landing animation's rise by key
constexpr int16_t kHopSpeed = 0x266;
constexpr int16_t kHopLaunch = int16_t(0xfe80), kJumpLaunch = int16_t(0xfd80), kGravity = 0x48;
constexpr int16_t kMaxFall = 0xb00;
constexpr uint16_t kStepSound = 0x1ff, kLandSound = 0x1fe, kHurtSound = 0x8d;
constexpr uint16_t kBlastSize = 0xa4;
constexpr int kHitHeight = 0x58;
// the hit entries: the body (hit radius 0x64) as a body, as the player's attack (thrown) or a liftable one
constexpr uint32_t kBodyRadius = hitRadius(0x64);
constexpr uint32_t kBodyShape = hitGroup(kHitGroupBodies) | kBodyRadius;
constexpr uint32_t kThrownShape = hitGroup(kHitGroupPlayerAttacks) | kBodyRadius;
constexpr uint32_t kThrownAtPlayerShape = hitGroup(kHitGroupLiftable) | kBodyRadius;
constexpr uint32_t kThrownAttack = kHitAttackThrown | kHitAttackByPlayer | kHitAttackSolid;

void playSound(uint16_t id, const AnimatedActor& actor) { soundPlayAtPos(id, actor.pos); } // exe 0x5d5380

// exe 0x410740(rotation, result)
void bounceOffWall(AnimatedActor& actor, uint32_t result) { reflectRotationOffWalls(actor.rotation, result); }

// exe 0x4ec050: the walls (as the shots' slot class while thrown; inside -> back to the previous position, a side
// wall -> kContactWall), then the ground: on the ground more than 0x20 above it -> kContactLedge, else snapped to it;
// airborne, a landing (floor / ceiling) -> snapped, kContactLanded. The push-out's result
uint32_t collide(const PlayerEnv& env, AnimatedActor& actor, HopFields& hop) {
    uint32_t result = 0;
    uint8_t slot = 0;
    if (hop.flags & kHopThrown) {
        slot = actor.slot;
        actor.slot = kThrownSlot;
    }
    if (int32_t(actorTestMove(env, actor)) < 0) { // exe 0x40f570
        result = actorPushOut(env, actor);         // exe 0x410960
        if (int32_t(result) < 0) actor.pos = actor.prevPos;
        if (result & kWallOnSides) hop.contact |= kContactWall;
    }
    const int ground = actorGround(env, actor); // exe 0x40f190
    const uint8_t flags = hop.flags;
    if (!(flags & kHopAirborne)) {
        if (ground - actor.pos.y.whole > 0x20) {
            hop.contact |= kContactLedge;
            if (flags & kHopThrown) actor.slot = slot;
            return result;
        }
        actor.pos.y.whole = int16_t(ground);
    } else if (result & kWallOnY) {
        actor.airborne = 0;
        actor.pos.y.whole = int16_t(ground);
        hop.contact |= kContactLanded;
    }
    if (hop.flags & kHopThrown) actor.slot = slot;
    return result;
}
void enter(AnimatedActor& actor, HopSub sub) {
    actor.subState = uint8_t(sub);
    actor.step = 0;
}
// the chase set-up shared by exe 0x4ec120 / 0x4ec300
void startChase(AnimatedActor& actor, HopFields& hop) {
    enter(actor, HopSub::Chase);
    actor.anim = kAnimRun;
    actor.velocity.z = kHopSpeed;
    hop.bumpTicks = 0;
    hop.bumps = 0;
}
// exe 0x4ec120 (variants 0 / 1): the next sub-state by the distance to the target
void decideNear(AnimatedActor& actor, HopFields& hop) {
    const Actor& target = *hop.target;
    const int d = planarDistance(target.pos, actor.pos); // exe 0x5e3b00
    const HopVariant variant = HopVariant(actor.variant);
    if (variant == HopVariant::Wanderer) { // within 0x800 of the target: a heading around its wander heading
        if (d >= 0x800) return;
        enter(actor, HopSub::Wander);
        actor.anim = kAnimRun;
        actor.velocity.z = kHopSpeed;
        const uint32_t r = rngStep();
        const HoppingReaverbotParams params(actor);
        actor.rotation.y = int16_t(
            (uint16_t(wanderHeadings[r & 7]) + uint16_t(int32_t(params.wanderHeading.get()) << 4)) & 0xfff);
        return;
    }
    if (d < 0x100) { // the jump attack when facing it
        if (!facingWithin(actor, target.pos, 0x180)) return; // exe 0x5036e0
        enter(actor, HopSub::JumpAttack);
        actor.anim = kAnimJump;
        actor.velocity.y = kJumpLaunch;
        actor.acceleration.y = kGravity;
        hop.flags |= kHopAirborne;
        actor.airborne = 1;
        hop.bumpTicks = 0;
        hop.bumps = 0;
        hop.flags &= uint8_t(~kHopKeepChasing);
        return;
    }
    if (variant == HopVariant::Hunter) { // (not reached: the caller takes the hunters elsewhere)
        const bool keep = (hop.flags & kHopKeepChasing) != 0;
        if (d < 0x600) {
            if (keep) {
                startChase(actor, hop);
                return;
            }
            if (!facingWithin(actor, target.pos, 0x180)) return;
            enter(actor, HopSub::Circle);
            actor.anim = kAnimRun;
            actor.velocity.z = kHopSpeed;
            hop.flags |= kHopReaim;
            return;
        }
        if (keep) {
            startChase(actor, hop);
            return;
        }
        enter(actor, HopSub::Hop);
        actor.anim = kAnimHop;
        hop.flags &= uint8_t(~kHopKeepChasing);
        return;
    }
    if (d < 0x400) {
        if (facingWithin(actor, target.pos, 0x180)) startChase(actor, hop);
        return;
    }
    enter(actor, HopSub::Hop);
    actor.anim = kAnimHop;
    hop.bumpTicks = 0;
    hop.bumps = 0;
}
// exe 0x4ec300 (variant 2): the targets -- in sub-state 0 the player, the first pool-0x20 actors of type 0x48 and 0x51
// (by the type byte only), else the current one -- tried in order: near and level -> the jump attack, < 0x600 ->
// circle it (or chase it, kHopKeepChasing), farther -> sub-state 0
void decideHunter(AnimatedActor& actor, HopFields& hop) {
    AnimatedActor* tron = nullptr;
    AnimatedActor* bon = nullptr;
    for (size_t i = 0; i < kAnimatedFieldSlots; ++i) {
        AnimatedActor& slot = animatedPool()[i];
        const uint8_t type = slot.type;
        if (type == kPartnerTronType) tron = &slot;
        if (type == kPartnerBonType) bon = &slot;
        if (tron != nullptr && bon != nullptr) break;
    }
    const int8_t tries = HopSub(actor.subState) != HopSub::Hop ? 1 : 3;
    Actor* other = nullptr;
    for (int8_t c = 0; c < tries; ++c) {
        if (HopSub(actor.subState) == HopSub::Hop) {
            if (c == 0) other = &player();
            else if (c == 1) other = tron;
            else if (c == 2) other = bon;
        } else {
            other = hop.target.get();
        }
        if (other == nullptr)
            notPortedAt(kStoryPort, "0x4ec300: no actor of type 0x48 / 0x51 (the exe reads address 0x10)", uint32_t(c));
        const int d = planarDistance(other->pos, actor.pos); // exe 0x5e3b00
        if (d < 0x100 && int(actor.pos.y.whole) - int(other->pos.y.whole) < 0x80) {
            if (!facingWithin(actor, other->pos, 0x180)) continue; // exe 0x5036e0
            enter(actor, HopSub::JumpAttack);
            actor.anim = kAnimJump;
            actor.velocity.y = kJumpLaunch;
            actor.acceleration.y = kGravity;
            hop.flags |= kHopAirborne;
            actor.airborne = 1;
            hop.flags &= uint8_t(~kHopKeepChasing);
            hop.bumpTicks = 0;
            hop.bumps = 0;
            hop.target = other;
            return;
        }
        if (d < 0x600) {
            if (hop.flags & kHopKeepChasing) {
                startChase(actor, hop);
                hop.target = other;
                return;
            }
            if (!facingWithin(actor, other->pos, 0x180)) continue;
            enter(actor, HopSub::Circle);
            actor.anim = kAnimRun;
            actor.velocity.z = kHopSpeed;
            hop.target = other;
            hop.flags |= kHopReaim;
            return;
        }
        if (hop.flags & kHopKeepChasing) {
            startChase(actor, hop);
            hop.target = other;
            return;
        }
        enter(actor, HopSub::Hop);
        actor.anim = kAnimHop;
        hop.flags &= uint8_t(~kHopKeepChasing);
    }
}
void decide(AnimatedActor& actor, HopFields& hop) {
    if (actor.variant < uint8_t(HopVariant::Hunter)) decideNear(actor, hop);
    else decideHunter(actor, hop);
}
// to sub-state 4 (off a ledge)
void startFall(AnimatedActor& actor, HopFields& hop) {
    enter(actor, HopSub::Fall);
    actor.airborne = 1;
    hop.flags |= kHopAirborne;
}
// the neighbour test of sub-states 3 / 9: another enemy close -> give way to it
bool giveWay(AnimatedActor& actor, HopFields& hop) {
    if (neighbourFind(actor, hop.neighbour, 0) == 0) return false; // exe 0x5e5e00
    enter(actor, HopSub::GiveWay);
    actor.velocity.z = kHopSpeed;
    hop.timer = 0x14;
    return true;
}

// exe 0x4eb390: hop in place spinning; landed -> decide, sound 0x1fe
void hopInPlace(const PlayerEnv& env, AnimatedActor& actor, HopFields& hop) {
    hop.flags |= kHopAirborne;
    actor.airborne = 1;
    integrateVelocity(actor, 0, actor.velocity.y, 0); // exe 0x5e3690
    actor.rotation.y += 0x40;
    actor.velocity.y += uint16_t(actor.acceleration.y);
    collide(env, actor, hop);
    if (!(hop.contact & kContactLanded)) return;
    hop.flags &= uint8_t(~kHopAirborne);
    decide(actor, hop);
    actor.velocity.y = kHopLaunch;
    actor.acceleration.y = kGravity;
    playSound(kLandSound, actor);
}
// exe 0x4eb500: thrown; any hit, wall or landing -> death
void flight(const PlayerEnv& env, AnimatedActor& actor, HopFields& hop) {
    actor.velocity.y += uint16_t(actor.acceleration.y);
    integrateVelocity(actor, 0, actor.velocity.y, actor.velocity.z);
    if (hop.hitBox.result & kHitAttackSolid) {
        enter(actor, HopSub::Death);
        return;
    }
    collide(env, actor, hop);
    if (hop.contact & (kContactWall | kContactLanded)) {
        enter(actor, HopSub::Death);
        hop.flags |= kHopDead;
    }
}
// exe 0x4eb440: at the player's hands until thrown / dropped
void carried(const PlayerEnv& env, AnimatedActor& actor, HopFields& hop) {
    carryAtHands(actor, 0, 0, 0);                  // exe 0x573ab0
    const CarryRelease release = carryReleased(); // exe 0x573950
    if (release == CarryRelease::Held) return;
    if (release == CarryRelease::Thrown) {
        int32_t v[3];
        playerAimVector(0, -0x280, -0x400, v); // exe 0x5e3e50
        actor.velocity.x = 0;
        actor.velocity.y = int16_t(v[1]);
        actor.velocity.z = int16_t(v[2]);
    } else {
        actor.velocity.x = 0;
        actor.velocity.y = 0;
        actor.velocity.z = 0;
    }
    actor.acceleration.y = 0x40;
    actor.rotation.y = player().rotation.y;
    actor.rotation.z = 0;
    actor.rotation.x = 0;
    actor.airborne = 0;
    carryDropFromPlayer(actor); // exe 0x573990
    actor.step = uint8_t(LiftedStep::Flight);
    hop.flags = uint8_t((hop.flags & ~kHopLifted) | kHopThrown | kHopDead | kHopAirborne);
    flight(env, actor, hop);
}
// exe 0x4eb420 [0x8965c8]
void lifted(const PlayerEnv& env, AnimatedActor& actor, HopFields& hop) {
    switch (LiftedStep(actor.step)) {
    case LiftedStep::Carried: carried(env, actor, hop); return;
    case LiftedStep::Flight: flight(env, actor, hop); return;
    default: notPortedAt(kStoryPort, "0x4eb420: +0xa past [0x8965c8]", actor.step);
    }
}
// exe 0x4eb560: a blast, hidden, freed after 10 ticks
void death(AnimatedActor& actor, HopFields& hop) {
    switch (DeathStep(actor.step)) {
    case DeathStep::Blast: {
        if (const uint32_t slot = allocActor60(0)) { // exe 0x5e1300
            StaticActor& blast = staticActorAt(slot);
            blast.flags = kActorLive;
            blast.type = typeByte(GlobalStaticType::Blast);
            blast.variant = 0;
            blast.loopFlags |= kActorSweepGroup2;
            blast.pos.x.whole = actor.pos.x.whole;
            blast.pos.y.whole = int16_t(uint16_t(actor.pos.y.whole) - uint16_t(kHitHeight));
            blast.pos.z.whole = actor.pos.z.whole;
            BlastParams params(actorHeaderAt(slot));
            params.size = kBlastSize;
            params.attack = 0;
        }
        ++actor.step;
        return;
    }
    case DeathStep::Hide:
        actor.setDrawn(false);
        hop.timer = 0xa;
        ++actor.step;
        return;
    case DeathStep::Wait:
        if (hop.timer != 0) {
            --hop.timer;
            return;
        }
        actor.state = uint8_t(HopState::Free);
        return;
    default: return;
    }
}
// exe 0x4eb600: run at the target; a ledge -> fall, a wall -> bounce off, else decide
void chase(const PlayerEnv& env, AnimatedActor& actor, HopFields& hop) {
    const int bearing = angleOf(hop.target->pos, actor.pos); // exe 0x5e3bc0
    turnYawTo(actor.rotation.y, uint32_t(bearing), 0x20);    // exe 0x5e3df0
    integrateVelocity(actor, 0, 0, -kHopSpeed);
    const uint32_t result = collide(env, actor, hop);
    const uint8_t contact = hop.contact;
    if (contact & kContactLedge) {
        startFall(actor, hop);
        return;
    }
    if (contact & kContactWall) {
        bounceOffWall(actor, result);
        return;
    }
    if (giveWay(actor, hop)) return;
    decide(actor, hop);
}
// exe 0x4eb6c0: fall (at most 0xb00); landed -> sub-state 0, a hunter dies
void fall(const PlayerEnv& env, AnimatedActor& actor, HopFields& hop) {
    integrateVelocity(actor, 0, actor.velocity.y, 0);
    actor.velocity.y += uint16_t(actor.acceleration.y);
    if (actor.velocity.y > kMaxFall) actor.velocity.y = kMaxFall;
    collide(env, actor, hop);
    if (!(hop.contact & kContactLanded)) return;
    if (actor.variant > uint8_t(HopVariant::Wanderer)) {
        enter(actor, HopSub::Death);
        hop.flags |= kHopDead;
        return;
    }
    hop.flags &= uint8_t(~kHopAirborne);
    enter(actor, HopSub::Hop);
    actor.anim = kAnimHop;
}
// exe 0x4eb740: the jump attack (its attack box), the landing animation, a rest (stunned)
void jumpAttack(const PlayerEnv& env, AnimatedActor& actor, HopFields& hop) {
    HoppingReaverbotParams params(actor);
    switch (JumpStep(actor.step)) {
    case JumpStep::Air:
        hop.flags |= kHopAttackBox;
        integrateVelocity(actor, 0, actor.velocity.y, -kHopSpeed);
        actor.velocity.y += uint16_t(actor.acceleration.y);
        if (actor.velocity.y > kMaxFall) actor.velocity.y = kMaxFall;
        collide(env, actor, hop);
        if (hop.contact & kContactLanded) {
            hop.flags &= uint8_t(~(kHopAttackBox | kHopAirborne));
            actor.anim = kAnimLand;
            ++actor.step;
            actor.velocity.y = -0x20;
            actor.acceleration.y = 7;
        }
        return;
    case JumpStep::Land:
        if (actor.animEnded()) {
            ++actor.step;
            actor.anim = kAnimRest;
            hop.timer = actor.variant < uint8_t(HopVariant::Hunter) ? 0x3c : 0xb4;
            params.handling |= kHandlingStunned;
            actor.pos.y.whole = int16_t(actorGround(env, actor)); // exe 0x40f190
            return;
        }
        integrateVelocity(actor, 0, landingRise[size_t(int32_t(int8_t(actor.keyIndex)))], 0); // by key
        return;
    case JumpStep::Rest:
        if (hop.timer != 0) {
            --hop.timer;
            return;
        }
        actor.subState = uint8_t(HopSub::Hop);
        params.handling &= uint8_t(~kHandlingStunned);
        actor.step = 0;
        actor.anim = kAnimHop;
        hop.flags &= uint8_t(~kHopAttackBox);
        return;
    default: return;
    }
}
// exe 0x4eb880: steer / change speed by how the neighbour moves; gone or timed out -> chase
void yieldToNeighbour(const PlayerEnv& env, AnimatedActor& actor, HopFields& hop) {
    const Actor& other = neighbourActor(hop.neighbour);         // exe 0x5e5cb0
    if (actor.step == 0) neighbourClassify(actor, hop.neighbour); // exe 0x5e5f70
    const uint16_t movement = hop.neighbour;
    if (movement & kNeighbourOpposite) {
        const int16_t h = int16_t(headingFromOther(actor, other, neighbourSide(hop.neighbour), 0x200)); // exe 0x5e5d90
        hop.heading = h;
        turnYawTo(actor.rotation.y, uint32_t(int32_t(h)), 0x20);
    }
    if (movement & kNeighbourCrossingSlow) {
        actor.velocity.z += 0x20;
        actor.acceleration.z = 0x20;
        if (actor.velocity.z > 0x380) actor.velocity.z = 0x380;
    }
    if (movement & kNeighbourCatching) {
        actor.velocity.z -= 0x20;
        actor.acceleration.z = 0x20;
        if (actor.velocity.z < 0x100) actor.velocity.z = 0x100;
    }
    if (other.flags == 0 || hop.timer == 0) enter(actor, HopSub::Chase);
    else --hop.timer;
    integrateVelocity(actor, 0, 0, -int(actor.velocity.z));
    const uint32_t result = collide(env, actor, hop);
    const uint8_t contact = hop.contact;
    if (contact & kContactLedge) {
        startFall(actor, hop);
        return;
    }
    if (contact & kContactWall) {
        bounceOffWall(actor, result);
        return;
    }
    decide(actor, hop);
}
// exe 0x4eb9d0: knocked up twice (animations 5 / 8), then spins on the spot (animation 9)
void knockedOver(const PlayerEnv& env, AnimatedActor& actor, HopFields& hop) {
    HoppingReaverbotParams params(actor);
    switch (SpinStep(actor.step)) {
    case SpinStep::Start:
        ++actor.step;
        actor.anim = kAnimKnocked;
        actor.velocity.y = int16_t(0xfbf0);
        actor.acceleration.y = 0xa0;
        actor.velocity.z = int16_t(0xff9b);
        return;
    case SpinStep::FirstBounce:
    case SpinStep::SecondBounce:
        integrateVelocity(actor, 0, actor.velocity.y, actor.velocity.z);
        actor.velocity.y += uint16_t(actor.acceleration.y);
        hop.flags |= kHopAirborne;
        actor.airborne = 1;
        collide(env, actor, hop);
        if (hop.contact & kContactLanded) {
            actor.velocity.y = int16_t(0xfc30);
            ++actor.step;
            actor.acceleration.y = 0xf0;
            actor.velocity.z = int16_t(0xffbb);
            if (SpinStep(actor.step) == SpinStep::SecondBounce) actor.anim = kAnimTumble;
        }
        return;
    case SpinStep::StartSpin:
        ++actor.step;
        actor.anim = kAnimSpin;
        hop.timer = actor.variant < uint8_t(HopVariant::Hunter) ? 0x3c : 0xb4;
        params.handling |= kHandlingStunned;
        return;
    case SpinStep::Spinning:
        if (hop.timer != 0) {
            --hop.timer;
            actor.rotation.y += 0x40;
            hop.spin += 0x80;
            return;
        }
        actor.anim = kAnimHop;
        params.handling &= uint8_t(~kHandlingStunned);
        enter(actor, HopSub::Hop);
        hop.flags &= uint8_t(~kHopAttackBox);
        actor.rotation.z = 0;
        return;
    default: return;
    }
}
// the wander step of exe 0x4ebb20 / 0x4ebd90: along the wander heading; beyond the wander range of home -> sub-state 0
void wanderStep(const PlayerEnv& env, AnimatedActor& actor, HopFields& hop, bool resetVariant) {
    const HoppingReaverbotParams params(actor);
    turnYawTo(actor.rotation.y, uint32_t(int32_t(params.wanderHeading.get()) << 4), 0x20); // exe 0x5e3df0
    integrateVelocity(actor, 0, 0, -kHopSpeed);
    // the exe's stack point {home x, y, home z} (integer parts; its fractions are not initialised)
    const int32_t home[3] = {int32_t(uint32_t(hop.homeX) << 16), int32_t(uint32_t(uint16_t(actor.pos.y.whole)) << 16),
                             int32_t(uint32_t(hop.homeZ) << 16)};
    if (planarDistance(actor.pos, home) > int(uint32_t(params.wanderRange) << 4)) { // exe 0x5e3b00
        if (resetVariant) actor.variant = uint8_t(HopVariant::Chaser);
        enter(actor, HopSub::Hop);
        actor.anim = kAnimHop;
    }
    const uint32_t result = collide(env, actor, hop);
    if (hop.contact & kContactWall) bounceOffWall(actor, result);
}
// exe 0x4ebb20: out of range -> a chaser, sub-state 0
void wander(const PlayerEnv& env, AnimatedActor& actor, HopFields& hop) { wanderStep(env, actor, hop, true); }
// exe 0x4ebbc0: circle the target, a new heading every 0x3c ticks; two bumps -> chase it for good
void circleTarget(const PlayerEnv& env, AnimatedActor& actor, HopFields& hop) {
    const uint8_t flags = hop.flags;
    if (flags & kHopReaim) {
        const int bearing = angleOf(hop.target->pos, actor.pos); // exe 0x5e3bc0
        const uint32_t r = rngStep();
        hop.heading = int16_t(uint16_t(uint16_t(circleOffsets[r & 7]) + uint16_t(bearing)) + 0x800u);
        actor.rotation.y = hop.target->rotation.y;
        hop.timer = 0x3c;
        hop.flags = uint8_t(flags & ~kHopReaim);
    } else if (hop.timer != 0) {
        --hop.timer;
    } else {
        hop.flags = uint8_t(flags | kHopReaim);
    }
    turnYawTo(actor.rotation.y, uint32_t(int32_t(hop.heading)), 0x20);
    integrateVelocity(actor, 0, 0, -kHopSpeed);
    const uint32_t result = collide(env, actor, hop);
    const uint8_t contact = hop.contact;
    if (!(contact & kContactWall) && !(contact & kContactLedge)) {
        if (hop.bumpTicks != 0) --hop.bumpTicks;
        if (giveWay(actor, hop)) return;
        decide(actor, hop);
        return;
    }
    if (contact & kContactWall) {
        bounceOffWall(actor, result);
    } else { // a ledge: back to the previous x / z, turned round
        actor.pos.x = actor.prevPos.x;
        actor.pos.z = actor.prevPos.z;
        actor.rotation.y += 0x800u;
    }
    if (hop.bumpTicks == 0) {
        hop.bumpTicks = 0x18;
        return;
    }
    hop.bumpTicks = 0x18;
    ++hop.bumps;
    if (hop.bumps > 1) { // two bumps within 0x18 ticks: face the target and chase it for good
        const int bearing = angleOf(hop.target->pos, actor.pos);
        hop.heading = int16_t(bearing);
        actor.rotation.y = int16_t(bearing);
        hop.bumpTicks = 0;
        hop.bumps = 0;
        enter(actor, HopSub::Chase);
        hop.flags |= kHopKeepChasing;
    }
}
// exe 0x4ebd90: hop; landed -> a random wander heading / range, then wander
void hopAndPick(const PlayerEnv& env, AnimatedActor& actor, HopFields& hop) {
    switch (HopPickStep(actor.step)) {
    case HopPickStep::Hop:
        integrateVelocity(actor, 0, actor.velocity.y, 0);
        actor.velocity.y += uint16_t(actor.acceleration.y);
        if (actor.velocity.y > kMaxFall) actor.velocity.y = kMaxFall;
        collide(env, actor, hop);
        if (hop.contact & kContactLanded) {
            HoppingReaverbotParams params(actor);
            hop.flags &= uint8_t(~kHopAirborne);
            actor.anim = kAnimRun;
            params.wanderHeading = int8_t(pickedHeadings[(rngStep() & 7) * 2]);
            params.wanderRange = pickedRanges[rngStep() & 7];
            ++actor.step;
        }
        return;
    case HopPickStep::Wander: wanderStep(env, actor, hop, false); return;
    default: return;
    }
}
// exe 0x4ebff6: let go by its carrier -> death
void dropped(AnimatedActor& actor, HopFields& hop) {
    actor.velocity.x = 0;
    actor.velocity.y = 0;
    actor.velocity.z = 0;
    actor.acceleration.y = 0x40;
    actor.airborne = 0;
    enter(actor, HopSub::Death);
    hop.flags |= kHopDead;
}
// exe 0x4ebef0: held by a partner (kHandlingCarried / kHandlingGrabbed); thrown -> launched (the flight)
void grabbed(const PlayerEnv& env, AnimatedActor& actor, HopFields& hop) {
    const uint8_t handling = HoppingReaverbotParams(actor).handling;
    const bool held = (handling & kHandlingCarried) || (handling & kHandlingGrabbed);
    switch (GrabbedStep(actor.step)) {
    case GrabbedStep::Held:
        if (!held) {
            dropped(actor, hop);
            return;
        }
        if (!(handling & kHandlingCarried)) return;
        ++actor.step;
        actor.anim = kAnimGrabbed;
        return;
    case GrabbedStep::Carried:
        if (handling & kHandlingThrown) {
            if (handling & kHandlingAtPlayer) { // at the player (exe 0x5e3ee0 on a stack copy of its position)
                actor.velocity.x = 0;
                actor.velocity.y = int16_t(launchSpeed(actor.pos, player().pos, 0x600, 0x40, 0xffffffffu));
                actor.velocity.z = int16_t(0xfa00);
            } else {
                int32_t v[3];
                playerAimVector(0, -0x280, -0x400, v); // exe 0x5e3e50
                actor.velocity.x = 0;
                actor.velocity.y = int16_t(v[1]);
                actor.velocity.z = int16_t(v[2]);
            }
            actor.acceleration.y = 0x40;
            actor.airborne = 0;
            actor.subState = uint8_t(HopSub::Lifted);
            actor.step = uint8_t(LiftedStep::Flight);
            hop.flags = uint8_t((hop.flags & ~kHopLifted) | kHopThrown | kHopDead | kHopAirborne);
            flight(env, actor, hop);
            return;
        }
        if (!held) dropped(actor, hop);
        return;
    default: return;
    }
}

// exe 0x4eaec0: the model, the hit points of its row, its boxes, registered as an enemy
void bind(const PlayerEnv& env, AnimatedActor& actor, HopFields& hop) {
    if (!bindActorModel(actor, 0)) { // exe 0x5e0ba0
        freeActor(actor);            // exe 0x5e1490
        return;
    }
    actor.flags |= kActorDrawn;
    actor.anim = kAnimHop;
    hop.animKey = 0;
    actor.prevAnim = kNoAnimShown;
    const uint16_t hp = animatedStats(actor.type).hitPoints;
    actor.velocity.setXY(0, 0);
    actor.maxHitPoints = int16_t(hp);
    actor.hitPoints = int16_t(hp);
    actor.collisionBox.pointAt(TableId::HoppingReaverbotHopBox);
    actor.interactionBox.pointAt(TableId::HoppingReaverbotLiftBox);
    actor.interaction = kInteractLift;
    actor.velocity.setZSpare(0, 0);
    actor.rotation.z = 0;
    actor.rotation.x = 0;
    actor.velocity.y = kHopLaunch;
    actor.acceleration.y = kGravity;
    actor.airborne = 0;
    actor.onPlatform = 0;
    actor.platform = nullptr;
    actor.actorHit = 0;
    hop.hitBox.result = 0;
    hop.flags = 0;
    hop.spin = 0;
    hop.stunDamage = 0;
    const HopVariant variant = HopVariant(actor.variant);
    actor.state = uint8_t(HopState::Run);
    actor.subState = uint8_t(HopSub::Hop);
    actor.step = 0;
    if (variant == HopVariant::Hunter) { // starts hopping in the air (sub-state 10); home = the spawn x / z
        actor.subState = uint8_t(HopSub::HopPick);
        actor.airborne = 1;
        hop.flags |= kHopAirborne;
        hop.homeX = uint16_t(actor.pos.x.whole);
        hop.homeZ = uint16_t(actor.pos.z.whole);
        registerEnemy(actor.slot, 0); // exe 0x5e5c10
        return;
    }
    if (variant == HopVariant::Wanderer) { // home = the spawn x / z
        hop.homeX = uint16_t(actor.pos.x.whole);
        hop.homeZ = uint16_t(actor.pos.z.whole);
    }
    actor.pos.y.whole = int16_t(actorGround(env, actor)); // exe 0x40f190
    hop.target = &player();
    registerEnemy(actor.slot, 0);
}
// exe 0x4eb000: the hits, the lift, the sub-state, the hit-list entry
void run(const PlayerEnv& env, AnimatedActor& actor, HopFields& hop) {
    const AnimatedActorStats stats = animatedStats(actor.type, actor.subtype);
    HoppingReaverbotParams params(actor);
    hop.contact = 0;
    if (actor.anim == kAnimRun && (actor.keyIndex == 4 || actor.keyIndex == 9)) playSound(kStepSound, actor);
    if (!(gameState().frame & 3) && hop.stunDamage != 0) --hop.stunDamage;
    if ((hop.hitBox.result & kHitAttackByPlayer) && !(params.handling & kHandlingHeld) && !(hop.flags & kHopDead)) {
        actor.tint = kTintFlash;
        const int16_t hpBefore = actor.hitPoints;
        if (hitPointsAdd(actor, -int(hop.hitBox.result & kHitDamageMask)) != 0) { // exe 0x5e52f0: dead
            enter(actor, HopSub::Death);
            hop.flags |= kHopDead;
            countDefeat(); // exe 0x5e53e0
        } else { // the damage adds up; past the row's threshold (+9) -> knocked over
            hop.stunDamage += uint16_t(hpBefore - actor.hitPoints);
            if (HopSub(actor.subState) != HopSub::Spin && hop.stunDamage > int16_t(stats.hitSize)) {
                enter(actor, HopSub::Spin);
                hop.stunDamage = 0;
            }
        }
        playSound(kHurtSound, actor);
    } else {
        const uint8_t flags = hop.flags;
        if (!(flags & (kHopLifted | kHopDead))) {
            if (!(params.handling & kHandlingHeld)) {
                if (playerLiftTargetCheck(actor, 0)) { // exe 0x573a60: lifted
                    hop.flags |= kHopLifted;
                    actor.rotation.z = 0;
                    params.handling |= kHandlingTaken;
                    enter(actor, HopSub::Lifted);
                    actor.anim = kAnimLifted;
                }
            } else { // grabbed by a partner
                hop.flags = uint8_t(flags | kHopLifted);
                actor.rotation.z = 0;
                params.handling |= kHandlingTaken;
                enter(actor, HopSub::Grabbed);
            }
        }
    }
    switch (HopSub(actor.subState)) { // [0x896598]
    case HopSub::Hop: hopInPlace(env, actor, hop); break;
    case HopSub::Lifted: lifted(env, actor, hop); break;
    case HopSub::Death: death(actor, hop); break;
    case HopSub::Chase: chase(env, actor, hop); break;
    case HopSub::Fall: fall(env, actor, hop); break;
    case HopSub::JumpAttack: jumpAttack(env, actor, hop); break;
    case HopSub::GiveWay: yieldToNeighbour(env, actor, hop); break;
    case HopSub::Spin: knockedOver(env, actor, hop); break;
    case HopSub::Wander: wander(env, actor, hop); break;
    case HopSub::Circle: circleTarget(env, actor, hop); break;
    case HopSub::HopPick: hopAndPick(env, actor, hop); break;
    case HopSub::Grabbed: grabbed(env, actor, hop); break;
    default: notPortedAt(kStoryPort, "0x4eb000: sub-state past [0x896598]", actor.subState);
    }
    if (HopVariant(actor.variant) == HopVariant::Hunter && !(hop.flags & (kHopDead | kHopThrown)) &&
        !(params.handling & kHandlingHeld) && actor.pos.y.whole >= 0x30) {
        enter(actor, HopSub::Death); // a hunter low enough: dies
        hop.flags |= kHopDead;
    }
    hop.hitBox.previousX = actor.prevPos.x.whole;
    hop.hitBox.previousZ = actor.prevPos.z.whole;
    const uint8_t flags = hop.flags;
    hop.hitBox.result = 0;
    const uint32_t owner = hitOwner(actor.slot);
    if (flags & kHopDead) { // dying: a harmless box
        const int16_t pos[3] = {actor.pos.x.whole, int16_t(uint16_t(actor.pos.y.whole) - uint16_t(kHitHeight)),
                                actor.pos.z.whole};
        const uint32_t shape =
            ((params.handling & kHandlingAtPlayer) ? kThrownAtPlayerShape : kThrownShape) | kHitNoLockOn;
        hitListSubmit(pos, hop.hitBox, owner | shape, stats.bodyAttack | kHitAttackThrown | kHitAttackByPlayer); // 0x5e4120
        return;
    }
    if (flags & kHopLifted) return; // lifted: no entry
    const int16_t pos[3] = {actor.pos.x.whole,
                            int16_t((flags & kHopAttackBox) ? uint16_t(actor.pos.y.whole)
                                                            : uint16_t(uint16_t(actor.pos.y.whole) - uint16_t(kHitHeight))),
                            actor.pos.z.whole};
    const uint32_t base = owner | hitHeightLevel(stats.hitHeight);
    uint32_t shape, attack;
    if (actor.variant < uint8_t(HopVariant::Hunter)) {
        shape = base | kBodyShape;
        attack = kHitAttackSolid;
    } else if (flags & kHopAttackBox) {
        shape = base | kThrownAtPlayerShape;
        attack = kThrownAttack;
    } else if (flags & kHopThrown) {
        shape = base | ((params.handling & kHandlingAtPlayer) ? kThrownAtPlayerShape : kThrownShape);
        attack = kThrownAttack;
    } else {
        shape = base | kBodyShape;
        attack = kHitAttackSolid;
    }
    hitListSubmit(pos, hop.hitBox, shape, stats.attack | attack);
}

// exe 0x4eadf0
void hoppingReaverbot(const PlayerEnv& env, AnimatedActor& actor) {
    HopFields& hop = classState<HopFields>(actor);
    actor.prevAnim = actor.anim;
    actor.prevPos = actor.pos;
    actor.tint = kTintNeutral;
    actor.prevRotation = actor.rotation;
    actor.savePreviousContact();
    switch (HopState(actor.state)) { // [0x89658c]
    case HopState::Bind: bind(env, actor, hop); break;
    case HopState::Run: run(env, actor, hop); break;
    case HopState::Free: freeActor(actor); break; // 0x468fa0: exe 0x5e1490
    default: notPortedAt(kStoryPort, "0x4eadf0: state past [0x89658c]", actor.state);
    }
    if (actor.anim == actor.prevAnim) animStep(actor); // exe 0x5e1ea0
    else animSet(actor, actor.anim, 0);                // exe 0x5e1c50
    if (actor.anim == kAnimSpin) decodedFrame().entries[1].y += hop.spin; // the root part's yaw
    else hop.spin = 0;
    animPose(actor); // exe 0x5e2d90
}

} // namespace

void hoppingReaverbotUpdate(const PlayerEnv& env, uint32_t actor) { hoppingReaverbot(env, animatedActorAt(actor)); }

} // namespace od::port
