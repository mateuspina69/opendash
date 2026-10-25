// ST09's push block, static class 0x5b5010 (type 0x4b sub 0, stage table 0x8d817c; see st09_push_block.hpp). Its shadow
// (exe 0x5f20f0) is render output and left out.
#include "content/yosyonke/st09_push_block.hpp"
#include "content/yosyonke/st09_push_block_state.hpp"

#include "engine/data_source.hpp"
#include "game/actor_collision.hpp"
#include "game/actor_model.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actor_stats.hpp"
#include "game/audio_logic.hpp"
#include "game/carry.hpp"
#include "game/collision.hpp"
#include "game/event_state.hpp"
#include "game/hit_list.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/story.hpp"

namespace od::port {

using namespace st09_push_block_state;

namespace {

enum class BlockState : uint8_t {
    Bind = 0,  // exe 0x5b5170
    Run = 1,   // exe 0x5b52c0
    Still = 2, // exe 0x52ef00 (the copy)
};
// +9 of Run (the exe's table 0x8d0988)
enum class BlockSub : uint8_t {
    Settle = 0,  // exe 0x5b5380: the motion cleared
    Rest = 1,    // exe 0x5b53c0: at rest, solid
    Slide = 2,   // exe 0x5b53f0: pushed along the travel yaw (sound 0x29b), slowing to rest
    Fall = 3,    // exe 0x5b5510: dropped off an edge, falls until it lands, then Slide
    HopBack = 4, // exe 0x5b5580: hit from behind / bumped by an actor: a hop back (at least 0x2a0)
    HighHop = 5, // exe 0x5b5690: hit from the front: a high hop back (sound 0x29b)
};
// +0xa of Slide / Fall / the hops
enum class SlideStep : uint8_t { Push = 0, Pushed = 1, Coast = 2 };
enum class HopStep : uint8_t { Launch = 0, Air = 1, Slide = 2 };
constexpr uint8_t kStepStarted = 1; // Rest / Fall: the step's set-up done

constexpr uint8_t kHitSidesLow = 0x1;  // the result's 0x300 sides (the push-out's x walls, the actor collision's z sides)
constexpr uint8_t kHitSidesHigh = 0x2; // its 0xc00 sides
constexpr uint8_t kHitBounced = 0x4; // bounced / landed
constexpr uint8_t kHitDropped = 0x8; // the floor dropped away
constexpr uint8_t kModeHitsOn = 0x01;      // no hit-list entries
constexpr uint8_t kModeIgnoresHits = 0x02; // the hits are not taken
constexpr uint8_t kModeAirborne = 0x08;    // the ground's drop does not make it fall
constexpr uint8_t kModePlayerSide = 0x10;  // its hit-list entry as the player's attacks
constexpr uint8_t kModeReflects = 0x40;    // the walls / actors reflect the travel yaw
constexpr uint8_t kModeSlide = 0x60, kModeFall = 0x4b, kModeHopBack = 0x48, kModeHighHop = 0x4a;
constexpr uint8_t kCopyBit = 0x1; // a variant with bit 0: the block's copy
constexpr uint8_t kBlockType = 0x4b, kCopyVariant = 1;
constexpr uint8_t kCoolDownTicks = 8;
constexpr uint16_t kPushSound = 0x29b;
constexpr uint32_t kKickHit = 0x80000;  // the result's flag of the player's kick (with damage: a hop)
constexpr uint32_t kBumpHit = 0x10000;  // a touch from behind
constexpr uint32_t kRidingMask = 0x1fffff;
const DataTable<CollisionBoxRecord> blockCollisionBox{TableId::St09PushBlockCollisionBox};
const DataTable<InteractionBoxRecord> blockLiftBox{TableId::St09PushBlockLiftBox};

// exe 0x59beb0: the player stands on the block
bool ridden(const StaticActor& actor) {
    return player().onPlatform != 0 && ((slotIdentityWord(player().platform) ^ slotIdentityWord(actor)) & kRidingMask) == 0;
}

// exe 0x5b57f0: the hit the box took: the kick with damage -> the push yaw = the player's yaw, 1; a bump -> the same,
// -1; else 0
int hitDirection(StaticActor& actor) {
    BlockFields& f = classState<BlockFields>(actor);
    const uint32_t v = f.box.result;
    if ((v & kKickHit) && (v & kHitDamageMask)) {
        f.pushYaw = uint16_t(uint16_t(player().rotation.y) & kAngleMask);
        return 1;
    }
    if (v & kBumpHit) {
        f.pushYaw = uint16_t(uint16_t(player().rotation.y) & kAngleMask);
        return -1;
    }
    return 0;
}
// exe 0x5b5790(a, yaw): bounced -> the fall's speed reversed and halved, the forward speed halved, the pitch 0,
// airborne, 1; else moved along the yaw by (0, velocity y, velocity z), the fall accelerated, 0
int airStep(StaticActor& actor, int yaw) {
    BlockFields& f = classState<BlockFields>(actor);
    if (f.hits & kHitBounced) {
        const int32_t vy = -int32_t(actor.velocity.y) >> 1;
        actor.velocity.z = int16_t(int32_t(actor.velocity.z) >> 1);
        actor.velocity.y = int16_t(vy);
        actor.rotation.x = 0;
        actor.airborne = kAirborneFalling;
        return 1;
    }
    moveAlongYaw(actor.pos, yaw, 0, actor.velocity.y, actor.velocity.z); // exe 0x5e36f0
    actor.velocity.y += actor.acceleration.y;
    return 0;
}
// exe 0x5b5850: the walls (the push-out; reflecting: the travel yaw turned), the mover queued, the ground: on the
// ground and the floor more than 0x20 below -> dropped, 1; else snapped. Airborne: a floor / ceiling (unless
// reflecting) lands it
int collide(const PlayerEnv& env, StaticActor& actor) {
    BlockFields& f = classState<BlockFields>(actor);
    f.hits = 0;
    uint32_t r = 0;
    if (int32_t(actorTestMove(env, actor)) < 0) { // exe 0x40f570
        r = actorPushOut(env, actor);              // exe 0x410960
        if (int32_t(r) < 0) actor.pos = actor.prevPos;
        if (r & (kWallOnXPlus | kWallOnXMinus)) f.hits |= kHitSidesLow;
        if (r & (kWallOnZPlus | kWallOnZMinus)) f.hits |= kHitSidesHigh;
        if (f.mode & kModeReflects) {
            actor.rotation.y = f.travelYaw;
            if (reflectRotationOffWalls(actor.rotation, r)) f.hits |= kHitBounced; // exe 0x410740
            f.travelYaw = actor.rotation.y;
            actor.rotation.y = 0;
            actor.prevRotation.y = 0;
        }
    }
    actorCollisionSubmitMover(actor);         // exe 0x4140b0
    const int g = actorGround(env, actor);    // exe 0x40f190
    if (actor.airborne == 0) {
        if (g - int32_t(actor.pos.y.whole) > 0x20) {
            f.hits |= kHitDropped;
            return 1;
        }
        actor.pos.y.whole = int16_t(g);
        return 0;
    }
    if (!(f.mode & kModeReflects) && (r & kWallOnY)) {
        actor.airborne = 0;
        actor.pos.y.whole = int16_t(g);
        f.hits |= kHitBounced;
    }
    return 0;
}
// exe 0x5b5960: an actor bumped it: the sides into the hits; reflecting -> the travel yaw turned (exe 0x4107d0), the
// push yaw = the travel yaw + a half turn; 1 when it hit a side or bounced
int bumped(StaticActor& actor) {
    BlockFields& f = classState<BlockFields>(actor);
    const uint32_t v = actor.actorHit;
    if (v == 0) return 0;
    if (v & (kSolidOnZPlus | kSolidOnZMinus)) f.hits |= kHitSidesLow;
    if (v & (kSolidOnXPlus | kSolidOnXMinus)) f.hits |= kHitSidesHigh;
    if (v & kSolidAboveOrBelow) f.hits |= kHitBounced;
    if (f.mode & kModeReflects) {
        actor.rotation.y = f.travelYaw;
        if (bounceRotationOffWalls(actor.rotation, v)) f.hits |= kHitBounced;
        f.travelYaw = actor.rotation.y;
        f.pushYaw = uint16_t(uint16_t(actor.rotation.y) + uint32_t(kHalfTurn));
        actor.rotation.y = 0;
        actor.prevRotation.y = 0;
    }
    return (f.hits & (kHitSidesLow | kHitSidesHigh | kHitBounced)) ? 1 : 0;
}

void enterSub(StaticActor& actor, BlockSub s) {
    actor.subState = uint8_t(s);
    actor.restartStep();
}

// exe 0x5b5380 (+ exe 0x5b53a0: the velocity and the forward acceleration cleared)
void settle(StaticActor& actor) {
    BlockFields& f = classState<BlockFields>(actor);
    actor.velocity.setXY(0, 0);
    actor.velocity.z = 0;
    actor.acceleration.z = 0;
    f.mode = 0;
    enterSub(actor, BlockSub::Rest);
}
// exe 0x5b53c0
void rest(StaticActor& actor) {
    BlockFields& f = classState<BlockFields>(actor);
    if (actor.step != 0) return;
    f.mode = 0;
    actor.velocity.z = 0;
    actor.velocity.y = 0;
    actor.acceleration.y = 0;
    f.solid = 1;
    actor.enterStep(kStepStarted);
}
// exe 0x5b53f0
void slide(StaticActor& actor) {
    BlockFields& f = classState<BlockFields>(actor);
    switch (SlideStep(actor.step)) {
    case SlideStep::Push:
        f.travelYaw = int16_t(f.pushYaw);
        f.mode = kModeSlide;
        actor.airborne = 0;
        if (actor.velocity.z != 0) actor.velocity.z -= 0x20;
        else actor.velocity.z = -0x100;
        if (actor.velocity.z < -0x380) actor.velocity.z = -0x380;
        f.pushYaw = 0xa; // the push's timer
        f.mode |= kModeIgnoresHits;
        soundPlayAtPos(kPushSound, actor.pos); // exe 0x5d5380
        ++actor.step;
        actor.velocity.y = 0;
        actor.acceleration.y = 0;
        [[fallthrough]];
    case SlideStep::Pushed:
        --f.pushYaw;
        if (f.pushYaw == 0) {
            f.mode &= uint8_t(~kModeIgnoresHits);
            ++actor.step;
        }
        [[fallthrough]];
    case SlideStep::Coast:
        if (ridden(actor)) {
            if (eventState().active != 0) return;
            ++actor.velocity.z;
        } else {
            actor.velocity.z += 8;
        }
        moveAlongYaw(actor.pos, f.travelYaw, 0, 0, actor.velocity.z); // exe 0x5e36f0
        if (actor.velocity.z > 0) {
            actor.velocity.z = 0;
            enterSub(actor, BlockSub::Rest);
        }
        return;
    default: return;
    }
}
// exe 0x5b5510
void fall(StaticActor& actor) {
    BlockFields& f = classState<BlockFields>(actor);
    switch (actor.step) {
    case 0:
        f.mode = kModeFall;
        actor.step = kStepStarted;
        f.solid = 0;
        actorDetachRiders(actor); // exe 0x414e70
        actor.acceleration.y = 0x40;
        [[fallthrough]];
    case kStepStarted:
        if (airStep(actor, f.travelYaw)) {
            actor.airborne = 0;
            actor.velocity.y = 0;
            f.pushYaw = uint16_t(f.travelYaw);
            enterSub(actor, BlockSub::Slide);
        }
        return;
    default: return;
    }
}
// the airborne tail of the hops: Air (falls until it bounces, then the speed doubled) and Slide (slides back along the
// travel yaw, the speed + its acceleration until >= 0 -> Rest)
void hopTail(StaticActor& actor, HopStep step) {
    BlockFields& f = classState<BlockFields>(actor);
    if (step == HopStep::Air) {
        if (!airStep(actor, f.travelYaw)) return;
        actor.velocity.z = int16_t(uint32_t(uint16_t(actor.velocity.z)) << 1);
        actor.airborne = 0;
        actor.velocity.y = 0;
        f.mode &= uint8_t(~kModeAirborne);
        ++actor.step;
    }
    moveAlongYaw(actor.pos, f.travelYaw, 0, 0, actor.velocity.z); // exe 0x5e36f0
    actor.velocity.z += actor.acceleration.z;
    if (actor.velocity.z >= 0) {
        actor.restartStep();
        actor.subState = uint8_t(BlockSub::Rest);
    }
}
// the launch of a hop: the mode, off the solid queue, the riders off, the travel yaw = the push yaw, level, airborne
void hopLaunch(StaticActor& actor, uint8_t mode) {
    BlockFields& f = classState<BlockFields>(actor);
    actor.enterStep(uint8_t(HopStep::Air));
    f.mode = mode;
    f.solid = 0;
    actorDetachRiders(actor); // exe 0x414e70
    f.travelYaw = int16_t(f.pushYaw);
}
// exe 0x5b5580
void hopBack(StaticActor& actor) {
    switch (HopStep(actor.step)) {
    case HopStep::Launch: {
        hopLaunch(actor, kModeHopBack);
        const int32_t v = actor.velocity.z;
        actor.rotation.x = 0;
        actor.rotation.z = 0;
        actor.airborne = kAirborneFalling;
        const int32_t mag = v < 0 ? -v : v;
        if (mag < 0x2a0) actor.velocity.z = -0x2a0;
        else actor.velocity.z = int16_t(-mag);
        actor.velocity.x = 0;
        actor.velocity.y = -0x100;
        actor.acceleration.x = 0;
        actor.acceleration.y = 0x40;
        actor.acceleration.z = 0x40;
        return;
    }
    case HopStep::Air:
    case HopStep::Slide: hopTail(actor, HopStep(actor.step)); return;
    default: return;
    }
}
// exe 0x5b5690
void highHop(StaticActor& actor) {
    switch (HopStep(actor.step)) {
    case HopStep::Launch:
        hopLaunch(actor, kModeHighHop);
        actor.rotation.x = 0;
        actor.rotation.z = 0;
        actor.airborne = kAirborneFalling;
        actor.velocity.x = 0;
        actor.velocity.y = -0x300;
        actor.velocity.z = -0x600;
        actor.acceleration.x = 0;
        actor.acceleration.y = 0x40;
        actor.acceleration.z = 0x40;
        soundPlayAtPos(kPushSound, actor.pos); // exe 0x5d5380
        return;
    case HopStep::Air:
    case HopStep::Slide: hopTail(actor, HopStep(actor.step)); return;
    default: return;
    }
}
// exe 0x5b52c0: the hits pick the sub-state, the sub-state runs, then the collisions (a drop: Fall)
void run(const PlayerEnv& env, StaticActor& actor) {
    BlockFields& f = classState<BlockFields>(actor);
    if (!(f.mode & kModeIgnoresHits)) {
        const int d = hitDirection(actor); // exe 0x5b57f0
        if (ridden(actor)) {
            if (d > 0) enterSub(actor, BlockSub::Slide);
        } else if (d > 0) {
            enterSub(actor, BlockSub::HighHop);
        } else if (d < 0 && f.coolDown == 0) {
            f.coolDown = kCoolDownTicks;
            enterSub(actor, BlockSub::HopBack);
        }
    }
    if (f.coolDown != 0) --f.coolDown;
    if (bumped(actor) && f.coolDown == 0) { // exe 0x5b5960
        f.coolDown = kCoolDownTicks;
        enterSub(actor, BlockSub::HopBack);
    }
    switch (BlockSub(actor.subState)) {
    case BlockSub::Settle: settle(actor); break;
    case BlockSub::Rest: rest(actor); break;
    case BlockSub::Slide: slide(actor); break;
    case BlockSub::Fall: fall(actor); break;
    case BlockSub::HopBack: hopBack(actor); break;
    case BlockSub::HighHop: highHop(actor); break;
    default: notPortedAt(kStoryPort, "0x5b52c0 sub-state (table 0x8d0988 runs into other classes)", actor.subState);
    }
    if (collide(env, actor)) { // exe 0x5b5850
        actor.airborne = kAirborneFalling;
        if (!(f.mode & kModeAirborne)) enterSub(actor, BlockSub::Fall);
    }
}
// exe 0x5b5170: the model, the hit points of the animated table's row (as the exe reads it), the boxes, the class bytes
// cleared; the copy (variant bit 0) stays still; the block takes the ground and spawns its copy
void bind(const PlayerEnv& env, StaticActor& actor) {
    BlockFields& f = classState<BlockFields>(actor);
    if (!bindActorModel(actor, 0)) { // exe 0x5e0ba0
        freeActor(actor);            // exe 0x5e1540
        return;
    }
    actor.rotation.x = 0;
    actor.rotation.z = 0;
    const uint16_t hp = hasAnimatedStats(actor.type) ? animatedStats(actor.type).hitPoints : 0;
    actor.maxHitPoints = int16_t(hp);
    actor.hitPoints = int16_t(hp);
    actor.collisionBox.pointAt(blockCollisionBox.stateAddress());
    actor.interactionBox.pointAt(blockLiftBox.stateAddress());
    actor.actorHit = 0;
    actor.lastMover = nullptr;
    actor.airborne = 0;
    actor.onPlatform = 0;
    actor.clearClassBlock(); // exe 0x40dc40
    f.box.previousX = actor.pos.x.whole;
    f.box.previousZ = actor.pos.z.whole;
    f.box.result = 0;
    actor.scale.x = 0x200;
    actor.scale.y = 0x200;
    actor.scale.z = 0x200;
    if (actor.variant & kCopyBit) { // the copy: Still (a ret), the update stops after the state
        actor.flags |= kActorDrawn;
        actor.interaction = 0;
        actor.tint = kTintNeutral;
        actor.enterState(uint8_t(BlockState::Still));
        return;
    }
    actor.setDrawn(false);
    actor.interaction = kInteractLift;
    actor.pos.y.whole = int16_t(actorGround(env, actor)); // exe 0x40f190
    const uint32_t at = allocActor60(kActorStageClass);   // exe 0x5e12f0
    if (!at) {
        freeActor(actor); // exe 0x5e1540
        return;
    }
    StaticActor& copy = staticActorAt(at);
    copy.flags = kActorLive;
    copy.loopFlags |= kActorSweepGroup2;
    copy.type = kBlockType;
    copy.subtype = 0;
    copy.variant = kCopyVariant;
    copy.spawnByte = 0;
    copy.pos = actor.pos;
    copy.rotation = actor.rotation;
    copy.rotation.y = int16_t(3 * kQuarterTurn);
    classState<BlockFields>(copy).partner = &actor;
    f.partner = &copy;
    actor.enterState(uint8_t(BlockState::Run));
}

// exe 0x5b5010
void pushBlock(const PlayerEnv& env, StaticActor& actor) {
    actor.savePreviousPose();
    actor.savePreviousContact();
    switch (BlockState(actor.state)) {
    case BlockState::Bind: bind(env, actor); break;
    case BlockState::Run: run(env, actor); break;
    case BlockState::Still: break;
    default:
        notPortedAt(kStoryPort, "0x5b5010 state (table 0x8d097c runs into the sub-state table 0x8d0988)", actor.state);
    }
    if (actor.variant & kCopyBit) return;
    BlockFields& f = classState<BlockFields>(actor);
    if (actor.flags & kActorLive) {
        const int16_t pos[3] = {actor.pos.x.whole, actor.pos.y.whole, actor.pos.z.whole};
        f.box.result = 0;
        f.box.previousX = actor.prevPos.x.whole;
        f.box.previousZ = actor.prevPos.z.whole;
        const uint8_t mode = f.mode;
        const uint32_t id = hitOwner(actor.slot);
        if (!(mode & kModeHitsOn)) { // exe 0x5e4120
            if (mode & kModePlayerSide) {
                hitListSubmit(pos, f.box, id | hitGroup(kHitGroupPlayerAttacks) | kHitNoLockOn | hitRadius(0x20),
                              kHitAttackHarmless | kHitAttackBump);
            } else if (player().body.anim != 0) { // the player is not idle: the lift box and the bump box
                hitListSubmit(pos, f.box, id | hitGroup(kHitGroupLiftable) | kHitNoLockOn | hitRadius(0x48),
                              (0x80000000u >> kHitGroupPlayerAttacks) | kHitAttackHarmless | kHitAttackBump);
                hitListSubmit(pos, f.box, id | hitGroup(kHitGroupLiftable) | kHitNoLockOn | hitRadius(0x30),
                              kHitAttackHarmless | kHitAttackBump);
            }
        }
        if (f.solid == 1) actorCollisionSubmitSolid(actor, SolidType::StandOn, kNoBone, 0); // exe 0x414050
        // exe 0x5f20f0(a, 0x30, 0x100, 0): the shadow (render output)
    }
    if (!f.partner) notPortedAt(kStoryPort, "0x5b5010: the exe writes through the copy pointer its failed spawn left",
                                actor.state);
    StaticActor& copy = *f.partner;
    copy.pos = actor.pos;
    copy.rotation.y = f.travelYaw;
}

} // namespace

void pushBlockUpdate(const PlayerEnv& env, uint32_t actor) { pushBlock(env, staticActorAt(actor)); }

} // namespace od::port
