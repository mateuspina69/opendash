// The throwable object (see throwable.hpp).
#include "game/actor_state.hpp"
#include "game/throwable.hpp"
#include "game/throwable_state.hpp"

#include "engine/data_source.hpp"
#include "game/actor_model.hpp"
#include "game/actor_pools.hpp"
#include "game/angles.hpp"
#include "game/carry.hpp"
#include "game/collision.hpp"
#include "game/fixed_point.hpp"
#include "game/game_state.hpp"
#include "game/hit_list.hpp"
#include "game/player/player_motion.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/table_records.hpp"

namespace od::port {

using namespace throwable_state;

namespace {

inline constexpr const char* kThrowableArea = "throwable";

// [0x8c4380]
enum class ThrowableState : uint8_t {
    Start = 0, // exe 0x564730
    Loose = 1, // exe 0x564880 (sub-states [0x8c438c])
    Held = 2,  // exe 0x5649d0 (sub-states [0x8c4394])
};
enum class LooseSub : uint8_t { Fall = 0, Rest = 1 };      // exe 0x564900 / 0x564960
enum class HeldSub : uint8_t { Carried = 0, Thrown = 1 };  // exe 0x5649f0 / 0x564b10

constexpr uint8_t kThrowableNoShadow = 0x01; // +0xc4: carried this tick (the shadow exe 0x5f20f0 skipped: render)
// +0xc5: the room collision's result
constexpr uint8_t kThrowablePushed = 0x01;   // pushed out of a wall (the forward speed halved)
constexpr uint8_t kThrowableBounced = 0x02;  // bounced off a wall (its travel yaw reflected)
constexpr uint8_t kThrowableUnsupported = 0x04; // the ground fell away under it at rest
constexpr uint8_t kThrowableLost = 0x08;     // outside the grid's cells: freed

// its kind by the stage and the variant (exe data 0x8c42c0: 4 bytes a stage), the kinds' collision boxes (0x8c4250,
// 12 bytes each) and carry offsets (0x8c4290, 8 bytes each: s16 x / y / z)
const DataTable<ScalarTable<uint8_t, 0x100 * 4>> throwableKinds{TableId::ThrowableKinds};
constexpr uint32_t kKindsPerStage = 4;
// the kinds' boxes (exe data 0x8c4250, 12 bytes each); the actor stores its box's address (the address is the record's identity)
constexpr uint32_t kThrowableBoxSize = 12;
const DataTable<RecordTable<CollisionBoxRecord, 4, kThrowableBoxSize>> throwableBoxes{TableId::ThrowableBoxes};
const DataTable<InteractionBoxRecord> throwableLiftBox{TableId::ThrowableLiftBox};
const DataTable<RecordTable<ShortPoint, 4, 8>> throwableCarryOffsets{TableId::ThrowableCarryOffsets};

constexpr uint32_t kThrowableLayers = kAnimatedLayers; // the room layers it collides with (exe's mask 2)
constexpr int16_t kGravity = 0x48;
constexpr int kUnsupportedDrop = 0x10;        // fallen this far below its box: the ground fell away
constexpr uint16_t kLifeTicks = 0x78, kBrokenLifeTicks = 0x1e; // its life at rest (dropped broken: shorter)
constexpr int16_t kBlinkTicks = 0x1e;         // it blinks its last ticks
constexpr uint16_t kUprightMask = 0x800;      // at rest: the pitch / roll kept upright or upside down
constexpr uint16_t kSpinBase = 0x100, kSpinRandomMask = 0x1ff; // the roll's random start
constexpr int16_t kSpinSpeed = 0x180;         // the pitch's spin (the yaw's: half), less by (RNG & 0xf) * 8
constexpr int kThrowAimY = -0x100, kThrowAimZ = -0x400; // the throw along the player's aim
constexpr int16_t kDropFallSpeed = -0x200, kDropForwardSpeed = -0x100; // dropped (the player hurt)
constexpr int16_t kBroken = -1;               // its hit points: broken (no more hits)
constexpr uint32_t kThrownShape = hitGroup(kHitGroupPlayerAttacks) | kHitNoLockOn | hitRadius(0x40);
constexpr uint32_t kThrownDamage = 8; // the hit entry's damage (& kHitDamageMask of the attack)
constexpr uint32_t kThrownAttack = kHitAttackByPlayer | kThrownDamage;

// exe 0x564c10(actor, yaw): bounced off a wall: the speeds reflected / damped, airborne, 1; else (pushed: the forward
// speed halved) moved along the yaw by (0, fall, forward), the fall speed under gravity, 0
int fallStep(StaticActor& actor, const ThrowableFields& f, int yaw) {
    const uint8_t c = f.contact;
    if (c & kThrowableBounced) {
        actor.velocity.y = wrap16(-int(actor.velocity.y) >> 1);
        actor.velocity.z = int16_t(actor.velocity.z >> 2);
        actor.airborne = kAirborneFalling;
        return 1;
    }
    if (c & kThrowablePushed) actor.velocity.z = int16_t(actor.velocity.z >> 1);
    moveAlongYaw(actor.pos, yaw, 0, actor.velocity.y, actor.velocity.z);
    actor.velocity.y += uint32_t(kGravity);
    return 0;
}
// exe 0x564c70: the room collision -- outside the cells (lost), pushed out of a wall (back to the previous position
// when the push gave up; its travel yaw reflected, bounced), and at rest the ground (fell away when more than its box's
// height + kUnsupportedDrop below; else the box's bottom on it)
void collide(const PlayerEnv& env, StaticActor& actor, ThrowableFields& f) {
    uint8_t contact = 0;
    const int16_t p[3] = {actor.pos.x.whole, actor.pos.y.whole, actor.pos.z.whole};
    const int16_t q[3] = {actor.prevPos.x.whole, actor.prevPos.y.whole, actor.prevPos.z.whole};
    const uint32_t r = testMoveBlocked(env, p, q, actor.collisionBox, kThrowableLayers); // exe 0x40f5a0
    if (r & kMoveNoCell) contact = kThrowableLost;
    if (int32_t(r) < 0) {
        const uint32_t pushed = actorPushOutMask(env, actor, kThrowableLayers); // exe 0x410980
        if (int32_t(pushed) < 0) actor.pos = actor.prevPos;
        if (pushed & kWallOnSides) contact |= kThrowablePushed;
        int16_t pitch = 0, yaw = f.travelYaw;
        if (reflectRotationOffWalls(pitch, yaw, pushed) != 0) contact |= kThrowableBounced; // exe 0x410740
        f.travelYaw = yaw;
    }
    const int16_t p2[3] = {actor.pos.x.whole, actor.pos.y.whole, actor.pos.z.whole};
    const int g = probeGroundAt(env, p2, actor.collisionBox,
                                int(actor.pos.y.whole) - int(actor.prevPos.y.whole), kThrowableLayers); // exe 0x40f1c0
    if (actor.airborne == 0) {
        const int16_t bottom = actor.collisionBox->maxY;
        if (g - int(actor.pos.y.whole) > int(bottom) + kUnsupportedDrop) {
            f.contact = uint8_t(contact | kThrowableUnsupported);
            return;
        }
        actor.pos.y.whole = int16_t(g - bottom);
    }
    f.contact = contact;
}
// exe 0x564730: the model bound (else freed); drawn, kept through the warps, one hit point; its kind's boxes; airborne,
// the class block cleared; launched along its pitch at its launch speed; spinning
void throwableStart(StaticActor& actor, ThrowableFields& f) {
    if (!bindActorModel(actor, actor.variant)) { // exe 0x5e0bc0
        freeActor(actor);
        return;
    }
    actor.flags |= kActorDrawn;
    actor.runFlags |= kActorWarpKept;
    actor.maxHitPoints = 1;
    actor.hitPoints = 1;
    const uint8_t kind = throwableKinds[uint32_t(gameState().stage) * kKindsPerStage + actor.variant];
    actor.interactionBox.pointAt(throwableLiftBox.stateAddress());
    actor.interaction = kInteractLift;
    const CollisionBoxRecord& box = throwableBoxes[kind];
    actor.collisionBox.pointAt(throwableBoxes.stateAddress() + uint32_t(kind) * kThrowableBoxSize);
    actor.pos.y.whole -= uint32_t(uint16_t(box.maxY));
    actor.airborne = kAirborneFalling;
    actor.onPlatform = 0;
    actor.clearClassBlock(); // exe 0x40dc40
    actor.velocity.x = 0;
    const int pitch = uint16_t(actor.rotation.x) & kAngleMask;
    const int speed = ThrowableLaunch(actor).speed;
    actor.velocity.y = wrap16((fixedSin(pitch) * speed) >> 12);
    actor.velocity.z = wrap16(-(fixedCos(pitch) * speed) >> 12);
    f.kind = kind;
    f.travelYaw = actor.rotation.y;
    actor.rotation.z = int16_t((rngStep() & kSpinRandomMask) + kSpinBase);
    const uint32_t r = rngStep();
    actor.enterState(uint8_t(ThrowableState::Loose));
    const int16_t spin = wrap16(kSpinSpeed - int(r & 0xf) * 8);
    actor.spin.x = spin;
    actor.spin.y = int16_t(spin >> 1);
}
// exe 0x564900: falling, spinning; a bounce takes one more move along the yaw, then (the next bounce) at rest
void looseFall(StaticActor& actor, ThrowableFields& f) {
    actor.rotation.x += uint32_t(uint16_t(actor.spin.x));
    if (fallStep(actor, f, f.travelYaw) == 0) return;
    if (actor.step == 0) {
        moveAlongYaw(actor.pos, f.travelYaw, 0, actor.velocity.y, actor.velocity.z);
        actor.enterStep(1);
        return;
    }
    actor.airborne = 0;
    actor.subState = uint8_t(LooseSub::Rest);
    actor.enterStep(0);
}
// exe 0x564960: at rest -- its life armed once (kLifeTicks, or kBrokenLifeTicks when dropped broken), upright; the
// ground gone: falling again
void looseRest(StaticActor& actor, ThrowableFields& f) {
    if (actor.step == 0) {
        if (actor.hitPoints < 0) f.lifeTicks = int16_t(kBrokenLifeTicks);
        else if (f.lifeArmed == 0) f.lifeTicks = int16_t(kLifeTicks);
        f.lifeArmed = 1;
        actor.rotation.x &= kUprightMask;
        actor.rotation.z &= kUprightMask;
        actor.enterStep(1);
    }
    if (f.contact & kThrowableUnsupported) {
        actor.subState = uint8_t(LooseSub::Fall);
        actor.enterStep(0);
        actor.velocity.x = 0;
        actor.velocity.z = 0;
    }
}
// the tail of exe 0x564880: once armed, the life blinks its last kBlinkTicks, then the object is freed
void lifeTick(StaticActor& actor, ThrowableFields& f) {
    if (f.lifeArmed == 0) return;
    if (f.lifeTicks < kBlinkTicks) actor.flags ^= kActorDrawn;
    --f.lifeTicks;
    if (f.lifeTicks == 0) freeActor(actor);
}
// exe 0x564880: the sub-state; live: the room collision, then lifted by the player (unbroken) -> Held
void throwableLoose(const PlayerEnv& env, StaticActor& actor, ThrowableFields& f) {
    switch (LooseSub(actor.subState)) {
    case LooseSub::Fall: looseFall(actor, f); break;
    case LooseSub::Rest: looseRest(actor, f); break;
    default: notPortedAt(kThrowableArea, "0x564880 sub-state", actor.subState);
    }
    if (actor.flags & kActorLive) {
        collide(env, actor, f);
        if (actor.hitPoints >= 0 && playerLiftTargetCheck(actor, 0)) { // exe 0x573a60
            actor.enterState(uint8_t(ThrowableState::Held));
            actor.flags |= kActorDrawn;
            actor.airborne = 0;
            return;
        }
    }
    lifeTick(actor, f);
}
void heldThrown(const PlayerEnv& env, StaticActor& actor, ThrowableFields& f);
// exe 0x5649f0: at the player's hands (its kind's offset), the shadow off; released: thrown along the player's aim, or
// dropped broken (the player hurt); its yaws the player's, its hit box cleared, airborne, put at the player, Thrown
void heldCarried(const PlayerEnv& env, StaticActor& actor, ThrowableFields& f) {
    actor.rotation.x = 0;
    actor.rotation.z = 0;
    const ShortPoint offset = throwableCarryOffsets[f.kind];
    carryAtHands(actor, offset.x, offset.y, offset.z); // exe 0x573ab0
    f.shadow |= kThrowableNoShadow;
    const CarryRelease r = carryReleased(); // exe 0x573950
    if (r == CarryRelease::Held) return;
    if (r == CarryRelease::Thrown) {
        int32_t aim[3];
        playerAimVector(0, kThrowAimY, kThrowAimZ, aim); // exe 0x5e3e50
        actor.velocity.x = 0;
        actor.velocity.y = int16_t(aim[1]);
        actor.velocity.z = int16_t(aim[2]);
    } else {
        actor.hitPoints = kBroken;
        actor.velocity.y = kDropFallSpeed;
        actor.velocity.z = kDropForwardSpeed;
    }
    actor.rotation.y = player().rotation.y;
    f.travelYaw = player().rotation.y;
    f.hitBox.result = 0;
    actor.airborne = kAirborneFalling;
    carryDropFromPlayer(actor); // exe 0x573990
    actor.subState = uint8_t(HeldSub::Thrown);
    actor.enterStep(0);
    actor.rotation.z = int16_t((rngStep() & kSpinRandomMask) + kSpinBase);
    heldThrown(env, actor, f);
}
// exe 0x564b10: flying (a solid hit turns it back, broken); landed -> Loose at rest, broken; the hit entry (its box)
// while not broken
void heldThrown(const PlayerEnv& env, StaticActor& actor, ThrowableFields& f) {
    if (f.hitBox.result & kHitAttackSolid) {
        f.travelYaw += uint32_t(kHalfTurn);
        actor.hitPoints = kBroken;
        actor.velocity.z = int16_t(actor.velocity.z >> 1);
    }
    actor.rotation.x += uint32_t(uint16_t(actor.spin.x));
    if (fallStep(actor, f, f.travelYaw) != 0) {
        if (actor.step == 0) {
            moveAlongYaw(actor.pos, f.travelYaw, 0, actor.velocity.y, actor.velocity.z);
            actor.enterStep(1);
        } else {
            actor.airborne = 0;
            actor.state = uint8_t(ThrowableState::Loose);
            actor.subState = uint8_t(LooseSub::Rest);
            actor.enterStep(0);
            actor.hitPoints = kBroken;
        }
    }
    collide(env, actor, f);
    f.hitBox.result = 0;
    f.hitBox.previousX = actor.prevPos.x.whole;
    f.hitBox.previousZ = actor.prevPos.z.whole;
    if (actor.hitPoints >= 0) {
        const int16_t pos[3] = {actor.pos.x.whole, actor.pos.y.whole, actor.pos.z.whole};
        hitListSubmit(pos, f.hitBox, hitOwner(actor.slot) | kThrownShape, kThrownAttack); // exe 0x5e4120
    }
}

} // namespace

void throwableUpdate(const PlayerEnv& env, uint32_t address) {
    StaticActor& actor = staticActorAt(address);
    auto&& f = classState<ThrowableFields>(actor);
    actor.savePreviousPose();
    actor.savePreviousContact();
    switch (ThrowableState(actor.state)) {
    case ThrowableState::Start: throwableStart(actor, f); break;
    case ThrowableState::Loose: throwableLoose(env, actor, f); break;
    case ThrowableState::Held:
        if (HeldSub(actor.subState) == HeldSub::Carried) heldCarried(env, actor, f);
        else if (HeldSub(actor.subState) == HeldSub::Thrown) heldThrown(env, actor, f);
        else notPortedAt(kThrowableArea, "0x5649d0 sub-state", actor.subState);
        break;
    default: notPortedAt(kThrowableArea, "0x564690 state", actor.state);
    }
    if ((actor.flags & kActorLiveDrawn) == kActorLiveDrawn) f.shadow &= uint8_t(~kThrowableNoShadow);
    if (f.contact & kThrowableLost) freeActor(actor);
}

} // namespace od::port
