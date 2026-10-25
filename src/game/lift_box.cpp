// The box you lift, class 0x5a89b0 (see lift_box.hpp). States 0x8cef78: 0 init 0x5a8bc0, 1 0x5a8cc0 (sub-states
// 0x8cef8c), 2 carried / thrown 0x5a9080 (sub-states 0x8cef98), 3 held by something else 0x5a92f0 (sub-states
// 0x8cefa0), 4 destroyed 0x5a93d0. Its stats row (game/actor_stats, by variant & 0x7f): the hit points, the hit
// entry's damages, the push threshold and distance.
#include "game/actor_state.hpp"
#include "game/lift_box.hpp"
#include "game/lift_box_state.hpp"

#include "engine/data_source.hpp"
#include "game/camera.hpp"
#include "game/global_class_types.hpp"

#include "game/actor_collision.hpp"
#include "game/actor_model.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_stats.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/carry.hpp"
#include "game/collision.hpp"
#include "game/content_registry.hpp"
#include "game/distances.hpp"
#include "game/explosions.hpp"
#include "game/hit_list.hpp"
#include "game/hit_points.hpp"
#include "game/player/player_motion.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/stage_state.hpp"
#include "game/visibility.hpp"

namespace od::port {

using namespace lift_box_state;

namespace {

const PlayerEnv* E = nullptr;

constexpr const char* kLiftBoxPort = "lift box"; // the area of its port-gap errors

// +0x3e behaviour
constexpr uint16_t kBoxBounces = 0x01;    // walls reflect its travel yaw
constexpr uint16_t kBoxHitKindMask = 0x06; // the hit entry's kind: 2 / 4 (their damages: row +1 / +2), 0 / 6 harmless
constexpr uint16_t kBoxHitKindLight = 0x02, kBoxHitKindHeavy = 0x04, kBoxHitKindResting = 0x06;
constexpr uint16_t kBoxNoHitEntry = 0x08;
constexpr uint16_t kBoxSolid = 0x10;      // queued as a stand-on solid
constexpr uint16_t kBoxMover = 0x20;      // queued as a mover
constexpr uint16_t kBoxNoPush = 0x40;     // hits do not push it
// +0x46 collision bits
constexpr uint16_t kBoxHitWall = 0x01;
constexpr uint16_t kBoxBounced = 0x02;
constexpr uint16_t kBoxLandedOnRider = 0x04; // came off a platform onto a rider
constexpr uint16_t kBoxOffGrid = 0x08;
constexpr uint16_t kBoxRiding = 0x10;
constexpr uint16_t kBoxActorHit = 0x20;      // an actor-collision hit (with kBoxHitWall: kBoxHitByActor)
constexpr uint16_t kBoxHitByActor = kBoxActorHit | kBoxHitWall;
constexpr uint16_t kBoxNoFall = kBoxBounced | kBoxRiding; // the fall stops on these
// +0xc (an outside holder's): game/lift_box.hpp
constexpr uint8_t kHolderMask = kLiftBoxHolderMask, kHolderHolds = kLiftBoxHolds, kHolderLetGo = kLiftBoxLetGo,
                  kHolderMover = kLiftBoxHolderMover;

enum class BoxState : uint8_t {
    Init = 0,      // exe 0x5a8bc0
    Main = 1,      // exe 0x5a8cc0
    Carried = 2,   // exe 0x5a9080
    Held = 3,      // exe 0x5a92f0
    Destroyed = 4, // exe 0x5a93d0
};
enum class BoxSub : uint8_t { // in Main
    Resting = 0, // exe 0x5a8d80
    Falling = 1, // exe 0x5a8e20
    Pushed = 2,  // exe 0x5a8eb0
};
enum class CarriedSub : uint8_t { // in Carried / Held
    InHands = 0, // exe 0x5a90a0 (Held: exe 0x5a9310)
    Flying = 1,  // exe 0x5a9180
};

// the hit result flags it reacts to
constexpr uint32_t kHitPushes = kHitAttackByPlayer | kHitAttackStrong; // these hits push (or damage) it
constexpr uint32_t kHitBreaks = kHitAttackSolid | kHitAttackBump;      // a thrown box breaks on a body
constexpr int16_t kGravity = 0x40, kPushGravity = 0x30, kMaxFall = 0x800, kSpinSpeed = 0x10, kSpinLimit = 0x100;
constexpr int16_t kPushSpeed = -0x600, kPushJump = 0x600; // the hop's forward speed and launch speed
constexpr int16_t kDropUp = -0x200, kDropForward = -0x100;
constexpr int kFallOffEdge = 8;       // the ground this far below a resting box: it falls
constexpr uint8_t kLiftBoxType = 0x25; // its type (the rider follower checks it)
constexpr uint16_t kBlastSize = 0x60;
constexpr int16_t kHitHeight = 0x50;   // the hit entry's (and the blast's) centre above the feet
constexpr int kHandsRaise = -0x10;     // its y at the player's hands
constexpr int kThrowUp = -0x100, kThrowForward = -0x300; // the throw's velocity (rotated by the player's pitch / yaw)
constexpr uint32_t kNoReachLimit = 0xffffffffu; // the push hop's planar distance is not capped
// +6 variant: & kBoxVariantMask the stats row / the kind (& kBoxKindMask), kBoxSpawnedHeld: spawned held
constexpr uint8_t kBoxVariantMask = 0x7f, kBoxSpawnedHeld = 0x80, kBoxKindMask = 0x0f;
constexpr uint8_t kBoxKindAltSounds = 1; // the kind with its own landing / bump sounds
// the variants: which can be pushed (1, 2) and which take damage / break (0, 2)
bool pushable(uint32_t variant) { return variant == 1 || variant == 2; }
bool breakable(uint32_t variant) { return variant == 0 || variant == 2; }
// the landing (a fall, a push hop) / the throw's landing: cameraShakeAt(force, amount, near, far) of the camera
constexpr uint32_t kLandShakeForce = 0x200, kLandShakeAmount = 0x10, kLandShakeNear = 0x100, kLandShakeFar = 0x1c00;
constexpr uint32_t kThrowShakeForce = 0x300, kThrowShakeAmount = 0x80, kThrowShakeNear = 0x200, kThrowShakeFar = 0x800;
constexpr uint16_t kSoundLand = 0x117, kSoundLandAlt = 0x1c5, kSoundBump = 0xee, kSoundBumpAlt = 0x1fd;

const DataTable<CollisionBoxRecord> liftBoxCollisionBox{TableId::LiftBoxCollisionBox};
const DataTable<InteractionBoxRecord> liftBoxLiftBox{TableId::LiftBoxBox};
// the y range a box stays in, by variant (s16 min, max); outside it and seen -> freed
struct HeightRange : DataRecord {
    int16_t min = 0;
    int16_t max = 0;
    static HeightRange load(const TableReader& r) {
        HeightRange rec;
        rec.min = r.s16(0);
        rec.max = r.s16(2);
        return rec;
    }
};
const DataTable<RecordTable<HeightRange, 0x80, 4>> heightRanges{TableId::LiftBoxHeightRanges};

// the box's stats row (by its variant)
StaticActorStats statsOf(const StaticActor& actor) { return staticStats(actor.type, actor.variant & kBoxVariantMask); }
// the landing / bump sound of its kind
uint16_t landSound(const StaticActor& actor) {
    return (actor.variant & kBoxKindMask) == kBoxKindAltSounds ? kSoundLandAlt : kSoundLand;
}
uint16_t bumpSound(const StaticActor& actor) {
    return (actor.variant & kBoxKindMask) == kBoxKindAltSounds ? kSoundBumpAlt : kSoundBump;
}

// exe 0x5a9430: landed (or on a platform / pushed in) -> 1
int boxFall(StaticActor& actor, LiftBoxFields& box, int yaw) {
    if ((box.collisionBits & kBoxNoFall) == 0 && actor.airborne != 0) {
        moveAlongYaw(actor.pos, yaw, 0, actor.velocity.y, actor.velocity.z);
        actor.velocity.y = int16_t(uint16_t(actor.velocity.y + actor.acceleration.y));
        if (actor.velocity.y > kMaxFall) actor.velocity.y = kMaxFall;
        return 0;
    }
    actor.velocity.z = int16_t(actor.velocity.z >> 2);
    actor.velocity.y = int16_t(uint16_t(-int(actor.velocity.y) >> 2));
    return 1;
}
// its travel yaw reflected off the walls (exe 0x410740)
int boxBounce(LiftBoxFields& box, uint32_t r) {
    int16_t pitch = 0, yaw = box.travelYaw;
    const int hit = reflectRotationOffWalls(pitch, yaw, r);
    box.travelYaw = yaw;
    return hit;
}
// exe 0x5a94a0: the room's wall test and push-out (its travel yaw reflected while it bounces)
void boxCollide(StaticActor& actor, LiftBoxFields& box) {
    box.collisionBits = 0;
    const uint32_t r = actorTestMove(*E, actor);
    if (r & kMoveNoCell) box.collisionBits |= kBoxOffGrid;
    if (r & kMoveBlocked) {
        const uint32_t w = actorPushOut(*E, actor);
        actor.wallHit = w;
        if (w & kWallOnSides) box.collisionBits = kBoxHitWall;
        if ((box.behaviour & kBoxBounces) && boxBounce(box, w) != 0) box.collisionBits |= kBoxBounced;
    }
}
// exe 0x5a9530: the actor collision result, riding
int boxActorHits(StaticActor& actor, LiftBoxFields& box) {
    const uint32_t r = actor.actorHit;
    if (r != 0) {
        if (r & kSolidOnSides) box.collisionBits = kBoxHitByActor;
        if (box.behaviour & kBoxBounces) boxBounce(box, r);
    }
    if (actor.onPlatform != 0) box.collisionBits |= kBoxRiding;
    if (actor.airborne == 0 && actor.wasOnPlatform != 0 && actor.onPlatform == 0) {
        box.collisionBits |= kBoxLandedOnRider;
        return 1;
    }
    return 0;
}
// the pitch turns by the spin, within -0x100..0x100
void boxSpin(StaticActor& actor) {
    actor.rotation.x = int16_t(uint16_t(actor.rotation.x + actor.spin.x));
    if (actor.rotation.x > kSpinLimit) actor.rotation.x = kSpinLimit;
    if (actor.rotation.x < -kSpinLimit) actor.rotation.x = int16_t(-kSpinLimit);
}
// exe 0x5a93d0: a blast (static type 7) at the box, freed
void boxDestroy(StaticActor& actor) {
    if (const uint32_t address = allocActor60(0)) { // exe 0x5e1300
        StaticActor& blast = staticActorAt(address);
        BlastParams params(blast);
        blast.flags = kActorLive;
        blast.type = typeByte(GlobalStaticType::Blast);
        blast.variant = 0;
        blast.loopFlags |= kActorSweepGroup2;
        blast.pos.x = actor.pos.x;
        blast.pos.y = actor.pos.y;
        params.size = kBlastSize;
        params.attack = 0;
        blast.pos.z = actor.pos.z;
        blast.pos.y.whole = int16_t(uint16_t(blast.pos.y.whole - kHitHeight));
    }
    actorDetachRiders(actor);
    freeActor(actor);
}
// exe 0x5a95b0: 1 destroyed, 2 pushed
int boxDamage(StaticActor& actor, LiftBoxFields& box) {
    int res = 0;
    const uint32_t h = box.hitBox.result, v = actor.variant & kBoxVariantMask, d = h & kHitDamageMask;
    if (d != 0 && (h & kHitPushes)) {
        if (pushable(v) && (statsOf(actor).pushThreshold <= d || (h & kHitAttackStrongPush))) res = 2;
        if (breakable(v)) {
            actor.tint = kTintFlash;
            if (hitPointsAdd(actor, -int(d)) != 0) return 1;
        }
    }
    return res;
}
void boxThrown(StaticActor& actor, LiftBoxFields& box);
// exe 0x5a8d80: a solid (a mover too on a platform)
void boxResting(StaticActor& actor, LiftBoxFields& box) {
    if (actor.step == 0) {
        box.behaviour = actor.onPlatform != 0 ? uint16_t(kBoxSolid | kBoxMover) : kBoxSolid;
        actor.airborne = 0;
        actor.velocity.x = 0;
        actor.velocity.y = 0;
        actor.velocity.z = 0;
        actor.enterStep(1);
        actor.rotation.y = int16_t((uint16_t(actor.rotation.y) + 8u) & 0xff8u);
        box.holder = 0;
        box.carried = 0;
        box.settled = 0;
    }
    if (actor.wasOnPlatform == 0) {
        if (actor.onPlatform == 0 && actorGround(*E, actor) - actor.pos.y.whole > kFallOffEdge) {
            actorDetachRiders(actor);
            actor.acceleration.y = kGravity;
            actor.subState = uint8_t(BoxSub::Falling);
            actor.enterStep(0);
        }
    } else if (actor.onPlatform == 0) {
        actor.acceleration.y = kGravity;
        actor.subState = uint8_t(BoxSub::Falling);
        actor.enterStep(0);
    }
}
// exe 0x5a8e20: falls (a heavy hit entry, a mover) until it lands, then resting
void boxFalling(StaticActor& actor, LiftBoxFields& box) {
    if (actor.step == 0) {
        box.behaviour = kBoxBounces | kBoxHitKindHeavy | kBoxMover;
        actor.enterStep(1);
        actor.airborne = 1;
        box.carried = 1;
        box.settled = 0;
    }
    if (boxFall(actor, box, actor.rotation.y) != 0) {
        soundPlayAtPos(landSound(actor), actor.pos);
        cameraShakeAt(kLandShakeForce, kLandShakeAmount, kLandShakeNear, kLandShakeFar, actor.pos);
        actor.airborne = 0;
        actor.subState = uint8_t(BoxSub::Resting);
        actor.enterStep(0);
    }
    boxCollide(actor, box);
}
// exe 0x5a8eb0: the hit's direction, hops pushDistance away
void boxPushed(StaticActor& actor, LiftBoxFields& box) {
    const StaticActorStats stats = statsOf(actor);
    boxActorHits(actor, box);
    if (actor.step == 0) {
        box.behaviour = kBoxBounces | kBoxHitKindHeavy | kBoxMover | kBoxNoPush;
        actor.enterStep(1);
        const uint16_t dir = uint16_t(box.hitBox.contact.x) & kAngleMask;
        box.travelYaw = int16_t(dir);
        const int dd = int(stats.pushDistance) * 0x200;
        const int angle = int(uint32_t(int16_t(dir)) & kAngleMask);
        // the landing point: dd ahead of the push, at the box's height (the low halves of x / z are unused)
        const int16_t tz = int16_t(uint16_t((-(int(fixedCos(angle)) * dd) >> 12) + actor.pos.z.whole));
        const int16_t tx = int16_t(uint16_t((-(int(fixedSin(angle)) * dd) >> 12) + actor.pos.x.whole));
        const int32_t to[3] = {int32_t(uint32_t(uint16_t(tx)) << 16), actor.pos.y,
                               int32_t(uint32_t(uint16_t(tz)) << 16)};
        const int32_t from[3] = {actor.pos.x, actor.pos.y, actor.pos.z};
        actor.velocity.z = kPushSpeed;
        actor.velocity.y = int16_t(launchSpeed(from, to, kPushJump, kPushGravity, kNoReachLimit));
        if (uint16_t(box.hitBox.contact.y) == 0) actor.velocity.z = 0;
        actor.acceleration.y = kPushGravity;
        actor.airborne = 1;
        actor.spin.x = kSpinSpeed;
        actor.onPlatform = 0;
        actorDetachRiders(actor);
        box.carried = 1;
        box.settled = 0;
    }
    if (box.collisionBits & kBoxHitWall) {
        actor.spin.x = int16_t(-actor.spin.x);
        actor.velocity.z = 0;
        soundPlayAtPos(bumpSound(actor), actor.pos);
    }
    boxSpin(actor);
    if (boxFall(actor, box, box.travelYaw) != 0) {
        soundPlayAtPos(landSound(actor), actor.pos);
        cameraShakeAt(kLandShakeForce, kLandShakeAmount, kLandShakeNear, kLandShakeFar, actor.pos);
        actor.rotation.x = 0;
        actor.airborne = 0;
        actor.velocity.x = 0;
        actor.velocity.y = 0;
        actor.velocity.z = 0;
        actor.spin.x = 0;
        actor.subState = uint8_t(BoxSub::Resting);
        actor.enterStep(0);
    }
    boxCollide(actor, box);
}
// exe 0x5a90a0: until let go (thrown / dropped)
void boxInHands(StaticActor& actor, LiftBoxFields& box) {
    if (actor.step == 0) {
        box.behaviour = kBoxBounces | kBoxNoHitEntry | kBoxNoPush;
        box.carried = 1;
        actor.enterStep(1);
    }
    carryAtHands(actor, 0, kHandsRaise, 0);
    const CarryRelease r = carryReleased();
    if (r == CarryRelease::Held) return;
    if (r == CarryRelease::Thrown) {
        int32_t v[3];
        playerAimVector(0, kThrowUp, kThrowForward, v); // exe 0x5e3e50
        actor.velocity.x = 0;
        actor.velocity.y = int16_t(v[1]);
        actor.velocity.z = int16_t(v[2]);
        box.behaviour = kBoxBounces | kBoxHitKindLight | kBoxMover | kBoxNoPush;
    } else {
        actor.velocity.y = kDropUp;
        actor.velocity.z = kDropForward;
        box.behaviour = kBoxBounces | kBoxMover | kBoxNoPush;
    }
    actor.acceleration.y = kGravity;
    const int16_t yaw = player().rotation.y;
    actor.rotation.y = yaw;
    box.travelYaw = yaw;
    actor.airborne = 1;
    actor.spin.x = kSpinSpeed;
    actor.actorHit = 0;
    box.collisionBits = 0;
    box.settled = 0;
    carryDropFromPlayer(actor);
    actor.subState = uint8_t(CarriedSub::Flying);
    actor.enterStep(0);
    boxThrown(actor, box);
}
// exe 0x5a9180: bounces, lands (on a stack: destroyed)
void boxThrown(StaticActor& actor, LiftBoxFields& box) {
    box.carried = 1;
    if (box.hitBox.result & kHitDamageMask) {
        if (breakable(actor.variant)) {
            if (box.hitBox.result & kHitBreaks) {
                actor.enterState(uint8_t(BoxState::Destroyed));
                boxDestroy(actor);
                return;
            }
        } else {
            actor.velocity.z = 0;
            actor.spin.x = int16_t(-actor.spin.x);
            if (box.behaviour & kBoxHitKindLight)
                box.behaviour = uint16_t((box.behaviour & ~kBoxHitKindLight) | kBoxHitKindHeavy);
        }
    }
    boxActorHits(actor, box);
    if (box.collisionBits & kBoxHitWall) {
        actor.spin.x = int16_t(-actor.spin.x);
        soundPlayAtPos(bumpSound(actor), actor.pos);
        if (box.collisionBits & kBoxActorHit) actor.velocity.z = 0;
        else actor.velocity.z = int16_t(actor.velocity.z >> 2);
        if (box.behaviour & kBoxHitKindLight)
            box.behaviour = uint16_t((box.behaviour & ~kBoxHitKindLight) | kBoxHitKindHeavy);
    }
    boxSpin(actor);
    if (boxFall(actor, box, box.travelYaw) != 0) {
        soundPlayAtPos(landSound(actor), actor.pos);
        cameraShakeAt(kThrowShakeForce, kThrowShakeAmount, kThrowShakeNear, kThrowShakeFar, actor.pos);
        actor.rotation.x = 0;
        actor.airborne = 0;
        actor.velocity.x = 0;
        actor.velocity.y = 0;
        actor.velocity.z = 0;
        actor.spin.x = 0;
        actor.enterState(uint8_t(BoxState::Main));
        // exe 0x5a9640: landed on a box that itself stands on something -> destroyed
        if (actor.onPlatform != 0 && actor.platform->onPlatform != 0) actor.enterState(uint8_t(BoxState::Destroyed));
    }
    boxCollide(actor, box);
}
// exe 0x5a9310: held by a holder until it lets go
void boxHeld(StaticActor& actor, LiftBoxFields& box) {
    if (actor.step == 0) {
        box.behaviour = kBoxBounces | kBoxNoHitEntry | kBoxSolid | kBoxNoPush;
        actor.velocity.clearWords(); // the exe clears +0x38..+0x3f: the behaviour just set (+0x3e) too
        if (box.holder & kHolderMover) box.behaviour |= kBoxMover;
        actor.enterStep(1);
    }
    if ((box.holder & kHolderMask) != kHolderLetGo) return;
    box.travelYaw = actor.rotation.y;
    // the previous x / y / z (integer parts) pushed back by the move of the last tick:
    // prev = pos * 0x101 - prev * 0x100
    actor.prevPos.x.whole = int16_t(uint16_t(actor.pos.x.whole * 0x101 + actor.prevPos.x.whole * -0x100));
    actor.prevPos.y.whole = int16_t(uint16_t(actor.pos.y.whole * 0x101 + actor.prevPos.y.whole * -0x100));
    box.behaviour = kBoxBounces | kBoxHitKindHeavy | kBoxMover | kBoxNoPush;
    actor.acceleration.y = kGravity;
    actor.airborne = 1;
    actor.spin.x = kSpinSpeed;
    actor.actorHit = 0;
    box.collisionBits = 0;
    actor.prevPos.z.whole = int16_t(uint16_t(actor.pos.z.whole * 0x101 + actor.prevPos.z.whole * -0x100));
    actor.subState = uint8_t(CarriedSub::Flying);
    actor.enterStep(0);
    boxThrown(actor, box);
}
// exe 0x5a8bc0: the model, the hit points, the boxes, its rider follower; resting (or held when spawned held)
void boxInit(StaticActor& actor, LiftBoxFields& box) {
    if (!bindActorModel(actor, actor.variant & kBoxVariantMask)) {
        freeActor(actor);
        return;
    }
    actor.flags |= kActorDrawn;
    const uint16_t hp = hasStaticStats(actor.type) ? uint16_t(statsOf(actor).hitPoints) : uint16_t(0);
    actor.maxHitPoints = int16_t(hp);
    actor.hitPoints = int16_t(hp);
    actor.velocity.clearWords();
    actor.rotation.z = 0;
    actor.rotation.x = 0;
    box.collisionBits = 0;
    box.settled = 0;
    actor.airborne = 0;
    actor.onPlatform = 0;
    actor.collisionBox.pointAt(liftBoxCollisionBox.stateAddress());
    actor.interactionBox.pointAt(liftBoxLiftBox.stateAddress());
    actor.interaction = kInteractLift;
    actor.clearClassBlock();
    actor.acceleration.y = kGravity;
    if (const uint32_t address = allocActorE0()) { // exe 0x5e1440: the rider follower
        ControllerActor& follower = controllerActorAt(address);
        RiderFollowerFields& f = classState<RiderFollowerFields>(follower);
        follower.flags = kActorLive;
        follower.type = kRiderFollowerType;
        follower.loopFlags |= kActorLateLoop | kActorSweepGroup2;
        f.riderSlot = int8_t(actor.slot);
        f.riderType = int8_t(kLiftBoxType);
        f.riderLoopFlags = actor.loopFlags;
    }
    actor.state = uint8_t(BoxState::Main);
    actor.subState = 0;
    actor.step = 0;
    actor.stepCounter = 0;
    if (actor.variant & kBoxSpawnedHeld) {
        box.settled = 1;
        actor.state = uint8_t(BoxState::Held);
        actor.subState = 0;
        actor.step = 0;
        actor.stepCounter = 0;
        box.holder = kHolderHolds;
    }
}
// leaving the ground: settled, a mover, off the platform
void boxSettle(StaticActor& actor, LiftBoxFields& box) {
    box.settled = 1;
    box.behaviour = uint16_t((box.behaviour & ~(kBoxSolid | kBoxMover)) | kBoxNoHitEntry);
    actor.onPlatform = 0;
    actorDetachRiders(actor);
}
// exe 0x5a8cc0: the damage (destroyed / pushed), the sub-state, then held by a holder or lifted by the player
void boxMain(StaticActor& actor, LiftBoxFields& box) {
    const int r = boxDamage(actor, box);
    if (r == 1) {
        actor.enterState(uint8_t(BoxState::Destroyed));
        boxDestroy(actor);
        return;
    }
    if (r == 2 && !(box.behaviour & kBoxNoPush)) {
        actor.subState = uint8_t(BoxSub::Pushed);
        actor.enterStep(0);
    }
    switch (BoxSub(actor.subState)) {
    case BoxSub::Resting: boxResting(actor, box); break;
    case BoxSub::Falling: boxFalling(actor, box); break;
    case BoxSub::Pushed: boxPushed(actor, box); break;
    default: notPortedAt(kLiftBoxPort, "sub-state", actor.subState);
    }
    if ((box.holder & kHolderMask) == kHolderHolds) {
        actor.enterState(uint8_t(BoxState::Held));
        boxSettle(actor, box);
        return;
    }
    if (box.liftLock == 0 && playerLiftTargetCheck(actor, 0)) { // exe 0x573a60
        actor.enterState(uint8_t(BoxState::Carried));
        boxSettle(actor, box);
        box.carried = 1;
    }
}
// exe 0x5a89b0's body: the state, the out-of-map / out-of-range frees, the hit entry, the collision queues
void liftBoxStates(StaticActor& actor) {
    LiftBoxFields& box = classState<LiftBoxFields>(actor);
    actor.tint = kTintNeutral;
    actor.savePreviousPose();
    actor.wasAirborne = actor.airborne;
    switch (BoxState(actor.state)) {
    case BoxState::Init: boxInit(actor, box); break;
    case BoxState::Main: boxMain(actor, box); break;
    case BoxState::Carried:
        if (actor.subState == uint8_t(CarriedSub::InHands)) boxInHands(actor, box);
        else if (actor.subState == uint8_t(CarriedSub::Flying)) boxThrown(actor, box);
        else notPortedAt(kLiftBoxPort, "state 2 sub-state", actor.subState);
        break;
    case BoxState::Held:
        if (actor.subState == uint8_t(CarriedSub::InHands)) boxHeld(actor, box);
        else if (actor.subState == uint8_t(CarriedSub::Flying)) boxThrown(actor, box);
        else notPortedAt(kLiftBoxPort, "state 3 sub-state", actor.subState);
        break;
    case BoxState::Destroyed: boxDestroy(actor); break;
    default: notPortedAt(kLiftBoxPort, "state", actor.state);
    }
    if (!(actor.flags & kActorLive)) return;
    // exe 0x5a9660: a mover never keeps a previous y below the current one
    if ((box.behaviour & kBoxMover) && actor.prevPos.y - actor.pos.y < 0) actor.prevPos.y = actor.pos.y;
    actor.wasOnPlatform = actor.onPlatform;
    if ((box.collisionBits & kBoxOffGrid) &&
        distance3(actor.pos, player().pos) > int(uint32_t(stage().depthRange) << 2)) {
        freeActor(actor); // fell out of the map, far away
        return;
    }
    if (box.settled == 0) {
        const HeightRange range = heightRanges[actor.variant & kBoxVariantMask];
        const int y = actor.pos.y.whole;
        if ((y < range.min || range.max < y) && pointVisibleOrNear(actor.pos, 0) != 0) {
            freeActor(actor);
            return;
        }
    }
    const uint16_t f = box.behaviour;
    const int16_t pos[3] = {actor.pos.x.whole, int16_t(actor.pos.y.whole - kHitHeight), actor.pos.z.whole};
    box.hitBox.result = 0;
    box.hitBox.previousX = actor.prevPos.x.whole;
    box.hitBox.previousZ = actor.prevPos.z.whole;
    if (!(f & kBoxNoHitEntry)) {
        const uint32_t owner = hitOwner(actor.slot) | kHitNoLockOn | kHitContactMotion | hitRadius(0x58);
        switch (f & kBoxHitKindMask) {
        case 0:
        case kBoxHitKindResting:
            hitListSubmit(pos, box.hitBox, owner | hitGroup(kHitGroupLiftable),
                          kHitAttackHarmless | kHitAttackBump);
            break;
        case kBoxHitKindLight:
            hitListSubmit(pos, box.hitBox, owner | hitGroup(kHitGroupPlayerAttacks),
                          statsOf(actor).damage | kHitAttackSolid);
            break;
        case kBoxHitKindHeavy:
            hitListSubmit(pos, box.hitBox, owner | hitGroup(kHitGroupHeavyThrown),
                          statsOf(actor).power | kHitAttackThrown | kHitAttackSolid);
            break;
        default: break;
        }
    }
    if (f & kBoxSolid) actorCollisionSubmitSolid(actor, SolidType::StandOn, kNoBone, 0);
    if (f & kBoxMover) actorCollisionSubmitMover(actor);
    // +0x50: exe 0x5f20f0(a, 0x4b, 0x100, 1) shadow list entry (render output)
}

} // namespace

void liftBoxUpdate(const PlayerEnv& env, uint32_t address) {
    E = &env;
    liftBoxStates(staticActorAt(address));
}

} // namespace od::port
