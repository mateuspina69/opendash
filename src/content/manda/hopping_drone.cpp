// The Manda ruin's hopping drone (ST13 pool 0x20 type 0x31 sub 1, exe 0x49f660; the name is a hypothesis from its
// behaviour): it waits, then homes on the player in short hops, sidesteps to keep facing it, jumps at a jumping player
// and hurts it on contact. The player can lift it and throw it (it then hurts what it hits); a fall stuns it; killed it
// flashes, explodes and leaves drops. Its variant: the idle behaviour (& 0xf), a flame it carries (the stage's effect
// type 0x10, put out by the extinguisher's water), a lateral drift range and a partner drone spawned beside it (the
// two linked by their slots: the survivor widens its drift when the other dies).
#include "content/manda/hopping_drone.hpp"
#include "content/manda/hopping_drone_state.hpp"

#include "engine/data_source.hpp"
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
#include "game/collision.hpp"
#include "game/distances.hpp"
#include "game/explosions.hpp"
#include "game/field_alias.hpp"
#include "game/fixed_point.hpp"
#include "game/global_class_types.hpp"
#include "game/hit_effects.hpp"
#include "game/hit_list.hpp"
#include "game/hit_points.hpp"
#include "game/pickups.hpp"
#include "game/player/player_motion.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/territory.hpp"

namespace od::port {

using namespace hopping_drone_state;

namespace {

inline constexpr const char* kDroneArea = "hopping drone";

// the variant bits (+6)
constexpr uint8_t kDroneIdleKindMask = 0x0f; // the idle behaviour (0: hunt, 1: wait, 2: sidestep)
constexpr uint8_t kDroneFlame = 0x10;        // carries a flame (and its model variant 1)
constexpr uint8_t kDronePaired = 0x20;       // has a partner drone
constexpr uint8_t kDroneDrifts = 0x40;       // drifts sideways (its drift limit)
constexpr uint8_t kDroneIsPartner = 0x80;    // the partner spawned by another drone
// the contact bits (+0x158, set every tick)
constexpr uint8_t kContactWallX = 0x01;
constexpr uint8_t kContactWallZ = 0x02;
constexpr uint8_t kContactLanded = 0x04;     // landed / bounced off a surface
constexpr uint8_t kContactGroundLost = 0x08; // the ground dropped more than kGroundStep below
// the behaviour bits (+0x159, set by the sub-states)
constexpr uint8_t kBehaveNoHitEntry = 0x01;
constexpr uint8_t kBehaveNoDamage = 0x02;
constexpr uint8_t kBehaveMoving = 0x04;     // set by the moving sub-states (no reader in this class)
constexpr uint8_t kBehaveKeepOnFall = 0x08; // a lost ground does not start the fall
constexpr uint8_t kBehaveThrown = 0x10;     // thrown by the player: hurts what it hits
constexpr uint8_t kBehaveStrongBody = 0x20; // its touch is a strong attack (jumping at the player)
constexpr uint8_t kBehaveBounce = 0x40;     // bounces off the walls it hits
constexpr uint8_t kBehaveNoLockOn = 0x80;   // not a lock-on target (dying)

// [0x893aa4]
enum class DroneState : uint8_t {
    Spawn = 0,
    Active = 1,
    Carried = 2,
    Dying = 3
}; // exe 0x49f7c0 / 0x49f9d0 /
   // 0x4a08d0 / 0x4a0bc0
// [0x893ab4] the active sub-states
enum class DroneSub : uint8_t {
    Reset = 0,            // exe 0x49faa0
    Wait = 1,             // exe 0x49fb10: until the player comes close
    Hop = 2,              // exe 0x49fbf0: hops toward the player
    JumpAtPlayer = 3,     // exe 0x49fec0
    TurnBack = 4,         // exe 0x49fff0: hops back home
    Sidestep = 5,         // exe 0x4a0150
    Fall = 6,             // exe 0x4a0340: the ground lost
    Stunned = 7,          // exe 0x4a03d0
    GetUp = 8,            // exe 0x4a0420
    Hunt = 9,             // exe 0x4a0500
    KnockedDown = 10,     // exe 0x4a06f0: a deadly hit on the ground
    KnockedAirborne = 11, // exe 0x4a07c0: ... in the air
};
// [0x893ae4] the carried sub-states
enum class CarriedSub : uint8_t { Hands = 0, Flying = 1 }; // exe 0x4a0940 / 0x4a0a60
// the animations (+0xa0)
enum class DroneAnim : uint8_t {
    Idle = 0,
    Crouch = 1,
    Hop = 2,
    Land = 3,
    JumpCrouch = 4,
    Jump = 5,
    JumpLand = 6,
    Knocked = 7,
    Down = 8,
    KnockedUp = 9,
    Tumble = 10,
    RiseCrouch = 0xb,
    Rise = 0xc,
    RiseLand = 0xd,
};
constexpr uint8_t kNoAnim = 0xff;

// The flame's spawn parameters (the stage's effect type 0x10): its owner's slot (0: put out), its size
struct DroneFlameParams {
    explicit DroneFlameParams(const ActorHeader& actor) : DroneFlameParams(const_cast<ActorHeader&>(actor)) {}
    explicit DroneFlameParams(ActorHeader& actor) : owner(actor.params[0]), ownerPad(actor.params[1]), size(actor.params, 2) {}
    uint8_t& owner;
    uint8_t& ownerPad; // cleared with it
    BytesField<uint16_t, 4> size;
};

const DataTable<CollisionBoxRecord> droneBox{TableId::HoppingDroneBox};
const DataTable<InteractionBoxRecord> droneLiftBox{TableId::HoppingDroneLiftBox};
constexpr uint8_t kDroneFlameEffectType = 0x10; // the stage's effect class
constexpr uint8_t kDronePartnerType = 0x31, kDronePartnerSub = 1;
constexpr uint16_t kIdleSound = 0x11b, kHopSound = 0x119, kJumpSound = 0x11a, kKnockedSound = 0x8d;
constexpr uint16_t kHurtBlinkTint = 0x4214;
constexpr int kTumbleSpin = 0x200; // the yaw a tick while it tumbles after a bounce
constexpr int16_t kGravity = 0x40;
constexpr int16_t kHopUp = -0xc0, kHopForward = -0x200, kJumpUp = -0x320, kTurnHopUp = -0x200, kHuntHop = -0x200;
constexpr int16_t kJumpFollow = 0x20; // the jump's vertical speed follows the player's by this a tick
constexpr int16_t kHopDistanceStep = 0x20;
constexpr int16_t kDriftStep = 0x10;
constexpr int16_t kTurnBackStep = 0x66;
constexpr int kNearFront = 0xe00, kNearSide = 0x700; // the wait ends when the player comes this close
constexpr int kJumpNear = 0x400, kHuntJumpNear = 0x200;
constexpr int kTurnStep = 0x200;
constexpr int16_t kSidestepWindow = 0x20, kSidestepJumpX = 0x90, kSidestepJumpZ = 0x400;
constexpr uint8_t kHuntBeats = 7;
constexpr int16_t kStunTicks = 8;
constexpr int16_t kGroundStep = 0x10;
constexpr int16_t kKnockBack = 0x100, kKnockUp = -0x100, kKnockUpAirborne = -0x180;
constexpr int16_t kDeathFallTicks = 0x28, kDeathTicks = 8, kNoLockOnTicks = 0x19;
constexpr int16_t kExplosionRaise = 0x60;
constexpr uint16_t kExplosionSpread = 0x30;
constexpr int16_t kFlameHeight = -0x50;
constexpr uint16_t kFlameSize = 0x20;
constexpr int16_t kDriftLimit = 0xb0, kPairedDriftLimit = 0x30;
constexpr int16_t kPartnerDriftShift = 0x70, kPartnerDriftWiden = 0x80;
constexpr int kPartnerSpacing = 8; // the partner 0x10 beside it (the 64-step sine << this >> 12)
constexpr int16_t kThrowUp = -0x100, kThrowForward = -0x380, kDropUp = 0x300, kDropForward = -0x40;
constexpr int16_t kCarryHeight = -0x18;
constexpr uint32_t kHitRadius = 0x58; // the hit entry's radius (its shape bits 0x16)

// a sub-state entered (its step and step counter cleared: the exe's word store)
void setSub(AnimatedActor& actor, DroneSub sub) {
    actor.subState = uint8_t(sub);
    actor.enterStep(0);
}
// the animation asked for
void setAnim(AnimatedActor& actor, DroneAnim anim) { actor.anim = uint8_t(anim); }

// the drone's facing against the direction from the player (exe 0x5e3bc0(player, drone), wrapped as the exe does)
int facingVsPlayer(const AnimatedActor& actor) {
    int d = int(uint16_t(actor.rotation.y) & kAngleMask) - (angleOf(player().pos, actor.pos) & kAngleMask);
    if (uint32_t(d + kHalfTurn) > uint32_t(kFullTurn)) d = int((uint32_t(-d) ^ uint32_t(kAngleMask)) + 1u);
    return d;
}
// the player in the air in one of its jumps
bool playerJumping() {
    const Player& p = player();
    if (p.airborne == 0) return false;
    const PlayerSub s = PlayerSub(p.subState);
    return s == PlayerSub::Jump || s == PlayerSub::RunningJump || s == PlayerSub::SkateFall;
}
// the player jumping within dist (x / z) of the drone
bool playerJumpingNear(const AnimatedActor& actor, int dist) {
    return playerJumping() && planarDistance(player().pos, actor.pos) < dist;
}

// the idle behaviour of the variant (the shared tail of the reset and of the fall's landing)
void idleByKind(AnimatedActor& actor) {
    const uint8_t k = actor.variant & kDroneIdleKindMask;
    if (k == 0) {
        actor.subState = uint8_t(DroneSub::Hunt);
    } else if (k == 1) {
        setSub(actor, DroneSub::Wait);
        return;
    } else if (k == 2) {
        setSub(actor, DroneSub::Sidestep);
        return;
    }
    actor.enterStep(0);
}

// exe 0x4a0b60: a tick in the air -- landed / bounced: the speeds halved and reflected, the pitch levelled, true; else
// moved along its yaw and pulled by the gravity
bool airStep(AnimatedActor& actor, const DroneFields& drone) {
    if (drone.contact & kContactLanded) {
        actor.velocity.z = int16_t(actor.velocity.z >> 1);
        actor.velocity.y = wrap16(int(uint32_t(-int(actor.velocity.y)) >> 1));
        actor.rotation.x = 0;
        actor.airborne = kAirborneFalling;
        return true;
    }
    integrateVelocity(actor, 0, actor.velocity.y, actor.velocity.z);
    actor.velocity.y = wrap16(actor.velocity.y + actor.acceleration.y);
    return false;
}
// exe 0x4a0f30: the actor collision's result into the contact bits (landed on an actor: on the ground)
void contactFromActors(AnimatedActor& actor, DroneFields& drone) {
    const uint32_t r = actor.actorHit;
    if (r == 0) return;
    if (r & (kWallOnXPlus | kWallOnXMinus)) drone.contact |= kContactWallX;
    if (r & (kWallOnZPlus | kWallOnZMinus)) drone.contact |= kContactWallZ;
    if (r & (kWallOnYPlus | kWallOnYMinus)) {
        actor.airborne = 0;
        drone.contact |= kContactLanded;
    }
}
// exe 0x4a0d70: the damage of the hit box -- the water puts its flame out; only the player's / thrown / strong attacks
// hurt (a white flash); dead: knocked down (on the ground / in the air)
void takeDamage(AnimatedActor& actor, DroneFields& drone) {
    const uint32_t v = drone.hitBox.result;
    if ((actor.variant & kDroneFlame) && drone.flameSlot != 0 && (v & kHitAttackWater)) {
        actor.variant &= uint8_t(~kDroneFlame);
        DroneFlameParams(effectPool()[size_t(drone.flameSlot - kEffectClass)]).owner = 0; // the flame is put out
    }
    if (v & kHitDamageMask) {
        int d = 0;
        if (v & (kHitAttackByPlayer | kHitAttackThrown | kHitAttackStrong)) {
            actor.tint = kTintFlash;
            d = int(v & kHitDamageMask);
        }
        if (hitPointsAdd(actor, wrap16(-d)) != 0) {
            actor.subState = uint8_t(actor.airborne ? DroneSub::KnockedAirborne : DroneSub::KnockedDown);
            actor.enterStep(0);
        }
    }
}
// exe 0x4a0e10: the walls (bouncing off them with kBehaveBounce), the territory, the actor collision, the ground;
// true = the ground was lost
bool collide(const PlayerEnv& env, AnimatedActor& actor, DroneFields& drone) {
    drone.contact = 0;
    uint32_t r = 0;
    if (int32_t(actorTestMove(env, actor)) < 0) {
        r = actorPushOut(env, actor);
        if (int32_t(r) < 0) actor.pos = actor.prevPos;
        if (r & (kWallOnXPlus | kWallOnXMinus)) drone.contact |= kContactWallX;
        if (r & (kWallOnZPlus | kWallOnZMinus)) drone.contact |= kContactWallZ;
        if ((drone.behaviour & kBehaveBounce) && reflectRotationOffWalls(actor.rotation, r))
            drone.contact |= kContactLanded;
    } else {
        const int rect = territoryViolated(actor.pos);
        if (rect != 0) {
            r = territoryPushBack(actor, rect);
            if (r & (kWallOnXPlus | kWallOnXMinus)) drone.contact |= kContactWallX;
            if (r & (kWallOnZPlus | kWallOnZMinus)) drone.contact |= kContactWallZ;
        }
    }
    actorCollisionSubmitMover(actor); // exe 0x4140b0
    const int ground = actorGround(env, actor);
    if (actor.airborne == 0) {
        if (ground - actor.pos.y.whole > kGroundStep) {
            drone.contact |= kContactGroundLost;
            return true;
        }
        actor.pos.y.whole = int16_t(ground);
    }
    if (!(drone.behaviour & kBehaveBounce) && (r & kWallOnYPlus)) drone.contact |= kContactLanded;
    return false;
}

// on the ground, its forward and vertical speeds stopped
void stopAndLand(AnimatedActor& actor) {
    actor.airborne = 0;
    actor.velocity.z = 0;
    actor.velocity.y = 0;
}

// ---- the active sub-states ------------------------------------------------------------------------------------------
void reset(AnimatedActor& actor, DroneFields& drone) { // exe 0x49faa0 (exe 0x49faf0, then the idle behaviour)
    setAnim(actor, DroneAnim::Idle);
    actor.velocity.setXY(0, 0);
    actor.velocity.z = 0;
    actor.acceleration.z = 0;
    resetModelScale(actor);
    drone.behaviour = 0;
    idleByKind(actor);
}

void wait(AnimatedActor& actor, DroneFields& drone) { // exe 0x49fb10: in front 0xe00, else 0x700 -> hop
    if (actor.step == 0) {
        drone.behaviour = kBehaveKeepOnFall;
        setAnim(actor, DroneAnim::Idle);
        actor.enterStep(1);
    }
    if ((rngStep() & 0x1f) == 0) soundPlayAtPos(kIdleSound, actor.pos);
    const int d = facingVsPlayer(actor);
    const int dist = planarDistance(player().pos, actor.pos);
    if (uint32_t(d + kQuarterTurn) < uint32_t(kHalfTurn)) {
        if (dist < kNearFront) setSub(actor, DroneSub::Hop);
    } else if (dist < kNearSide) {
        setSub(actor, DroneSub::Hop);
    }
}

void hop(AnimatedActor& actor, DroneFields& drone) { // exe 0x49fbf0: steps 4 jump, 5 air, 6 land; past the range back
    const uint8_t st = actor.step;
    switch (st) {
    case 0:
        drone.behaviour = kBehaveMoving | kBehaveKeepOnFall;
        actor.velocity.z = 0;
        actor.velocity.y = 0;
        actor.acceleration.y = 0;
        setAnim(actor, DroneAnim::Idle);
        actor.enterStep(1);
        break;
    case 1:
        if ((rngStep() & 0x1f) == 0) soundPlayAtPos(kIdleSound, actor.pos);
        if (actor.animEnded()) ++actor.step;
        break;
    case 2:
        setAnim(actor, DroneAnim::Crouch);
        actor.step = uint8_t(st + 1);
        break;
    case 3:
        if (!actor.animEnded()) break;
        actor.step = uint8_t(st + 1);
        [[fallthrough]];
    case 4: {
        soundPlayAtPos(kHopSound, actor.pos);
        const int d = facingVsPlayer(actor);
        const int dist = planarDistance(player().pos, actor.pos);
        int c = sin64(uint32_t(d >> 6)) * dist >> 12; // the player's lateral offset
        const int16_t drift = drone.drift;
        if (c > 0) {
            const int room = int(drone.driftLimit) - drift;
            if (c > room) c = room;
            drone.drift = wrap16(drift + c);
        } else if (c < 0) {
            const int room = -(int(drone.driftLimit) + drift);
            if (c < room) c = room;
            drone.drift = wrap16(drift + c);
        }
        const int q = c / 6;
        const uint32_t i = ((uint32_t(uint16_t(actor.rotation.y)) + kQuarterTurn) >> 6) & 0x3f;
        drone.hopStepX = wrap16(sin64(i) * q >> 12);
        drone.hopStepZ = wrap16(cos64(i) * q >> 12);
        actor.velocity.z = kHopForward;
        actor.velocity.y = kHopUp;
        actor.acceleration.y = kGravity;
        actor.airborne = kAirborneFalling;
        ++actor.step;
        setAnim(actor, DroneAnim::Hop);
        break;
    }
    case 5:
        drone.hopDistance = wrap16(drone.hopDistance + kHopDistanceStep);
        if (!airStep(actor, drone)) {
            actor.pos.x.whole = wrap16(actor.pos.x.whole + drone.hopStepX);
            actor.pos.z.whole = wrap16(actor.pos.z.whole + drone.hopStepZ);
        } else {
            stopAndLand(actor);
            ++actor.step;
            setAnim(actor, DroneAnim::Land);
        }
        break;
    case 6:
        if (actor.animEnded()) {
            setAnim(actor, DroneAnim::Hop);
            actor.step = 4;
            if (drone.hopRange < drone.hopDistance) {
                drone.drift = wrap16(-int(drone.drift));
                actor.subState = uint8_t(DroneSub::TurnBack);
                setAnim(actor, DroneAnim::Idle);
                actor.enterStep(0);
                return;
            }
        }
        break;
    default: break;
    }
    if (playerJumpingNear(actor, kJumpNear)) setSub(actor, DroneSub::JumpAtPlayer);
}

void jumpAtPlayer(AnimatedActor& actor, DroneFields& drone) { // exe 0x49fec0: follows the player's vertical speed
    switch (actor.step) {
    case 0:
        drone.behaviour = kBehaveMoving | kBehaveKeepOnFall | kBehaveStrongBody;
        actor.velocity.z = 0;
        actor.velocity.y = 0;
        actor.acceleration.y = 0;
        setAnim(actor, DroneAnim::JumpCrouch);
        actor.step = 2;
        return;
    case 1:
        if (!actor.animEnded()) return;
        ++actor.step;
        [[fallthrough]];
    case 2:
        soundPlayAtPos(kJumpSound, actor.pos);
        actor.velocity.z = 0;
        actor.velocity.y = kJumpUp;
        actor.acceleration.y = kGravity;
        actor.airborne = kAirborneFalling;
        ++actor.step;
        setAnim(actor, DroneAnim::Jump);
        return;
    case 3:
        actor.velocity.y = wrap16(actor.velocity.y + (player().velocity.y < 0 ? -kJumpFollow : kJumpFollow));
        [[fallthrough]];
    case 4:
        if (airStep(actor, drone)) {
            stopAndLand(actor);
            ++actor.step;
            setAnim(actor, DroneAnim::JumpLand);
        }
        return;
    case 5:
        if (actor.animEnded()) {
            const uint8_t k = actor.variant & kDroneIdleKindMask;
            if (k == 0)
                setSub(actor, DroneSub::Hunt);
            else if (k == 1)
                setSub(actor, DroneSub::Hop);
            else if (k == 2)
                setSub(actor, DroneSub::Sidestep);
        }
        return;
    default: return;
    }
}

void turnBack(AnimatedActor& actor, DroneFields& drone) { // exe 0x49fff0: hops back home, turning on the way
    switch (actor.step) {
    case 0:
        drone.behaviour = kBehaveKeepOnFall;
        actor.velocity.z = 0;
        actor.velocity.y = 0;
        actor.acceleration.y = 0;
        drone.homeYaw = uint16_t(drone.homeYaw + kHalfTurn);
        drone.hopDistance = wrap16(drone.hopRange - drone.hopDistance);
        drone.side ^= 1u;
        setAnim(actor, DroneAnim::Idle);
        actor.enterStep(1);
        return;
    case 1:
        if (actor.animEnded()) {
            setAnim(actor, DroneAnim::Crouch);
            ++actor.step;
        }
        return;
    case 2:
        soundPlayAtPos(kJumpSound, actor.pos);
        if (!actor.animEnded()) return;
        ++actor.step;
        [[fallthrough]];
    case 3:
        actor.velocity.z = 0;
        actor.velocity.y = kTurnHopUp;
        actor.acceleration.y = kGravity;
        actor.airborne = kAirborneFalling;
        ++actor.step;
        setAnim(actor, DroneAnim::Hop);
        return;
    case 4:
        if (airStep(actor, drone)) {
            setAnim(actor, DroneAnim::Land);
            stopAndLand(actor);
            ++actor.step;
            actor.rotation.y = int16_t(drone.homeYaw);
            return;
        }
        actor.rotation.y = wrap16(uint16_t(actor.rotation.y) + kTurnBackStep);
        return;
    case 5:
        if (actor.animEnded()) {
            const uint8_t k = actor.variant & kDroneIdleKindMask;
            if (k < 2)
                setSub(actor, DroneSub::Hop);
            else if (k == 2)
                setSub(actor, DroneSub::Sidestep);
        }
        return;
    default: return;
    }
}

void sidestep(AnimatedActor& actor, DroneFields& drone) { // exe 0x4a0150: drifts to keep facing the player
    if (actor.step == 0) {
        drone.behaviour = 0;
        setAnim(actor, DroneAnim::Idle);
        ++actor.step;
        actor.velocity.z = 0;
        const uint32_t i = ((uint32_t(uint16_t(actor.rotation.y)) + kQuarterTurn) >> 6) & 0x3f;
        actor.velocity.y = 0;
        actor.acceleration.y = 0;
        drone.hopStepX = int16_t((sin64(i) << 4) >> 12);
        drone.hopStepZ = int16_t((cos64(i) << 4) >> 12);
    }
    const int d = facingVsPlayer(actor);
    if (uint32_t(d + kQuarterTurn) < uint32_t(kHalfTurn)) {
        if (!playerJumping()) {
            if (d <= kSidestepWindow) {
                if (d < -kSidestepWindow && -int(drone.driftLimit) < drone.drift) {
                    drone.drift = wrap16(drone.drift - kDriftStep);
                    actor.pos.x.whole = wrap16(actor.pos.x.whole - drone.hopStepX);
                    actor.pos.z.whole = wrap16(actor.pos.z.whole - drone.hopStepZ);
                }
            } else if (drone.drift < drone.driftLimit) {
                drone.drift = wrap16(drone.drift + kDriftStep);
                actor.pos.x.whole = wrap16(actor.pos.x.whole + drone.hopStepX);
                actor.pos.z.whole = wrap16(actor.pos.z.whole + drone.hopStepZ);
            }
        } else { // the player jumps right in front: jump at it
            const uint32_t i = uint32_t(d >> 6) & 0x3f;
            const int dist = planarDistance(player().pos, actor.pos);
            const int16_t sx = wrap16(sin64(i) * dist >> 12);
            if (sx <= kSidestepJumpX && sx >= -kSidestepJumpX) {
                const int16_t sz = wrap16(cos64(i) * dist >> 12);
                if (sz <= kSidestepJumpZ && sz >= 0) setSub(actor, DroneSub::JumpAtPlayer);
            }
        }
    } else if (planarDistance(player().pos, actor.pos) < kNearFront) {
        drone.drift = wrap16(-int(drone.drift));
        setSub(actor, DroneSub::TurnBack);
    }
}

void fall(AnimatedActor& actor, DroneFields& drone) { // exe 0x4a0340: the ground lost, until it lands
    if (actor.step == 0) {
        drone.behaviour = kBehaveMoving | kBehaveKeepOnFall | kBehaveBounce;
        actor.step = 1;
        setAnim(actor, DroneAnim::Idle);
        actor.acceleration.y = kGravity;
        resetModelScale(actor);
    } else if (actor.step != 1) {
        return;
    }
    if (airStep(actor, drone)) {
        actor.airborne = 0;
        actor.velocity.y = 0;
        idleByKind(actor);
    }
}

void stunned(AnimatedActor& actor, DroneFields& drone) { // exe 0x4a03d0
    if (actor.step == 0) {
        drone.behaviour = kBehaveKeepOnFall;
        setAnim(actor, DroneAnim::Down);
        actor.enterStep(1);
        drone.timer = kStunTicks;
    }
    drone.timer = wrap16(drone.timer - 1);
    if (drone.timer == 0) setSub(actor, DroneSub::GetUp);
}

void getUp(AnimatedActor& actor, DroneFields& drone) { // exe 0x4a0420: a hop, then the hunt with the idle kind cleared
    switch (actor.step) {
    case 0:
        drone.behaviour = kBehaveNoDamage | kBehaveMoving | kBehaveKeepOnFall;
        setAnim(actor, DroneAnim::RiseCrouch);
        actor.enterStep(1);
        actor.velocity.z = 0;
        actor.velocity.y = kTurnHopUp;
        actor.acceleration.y = kGravity;
        actor.rotation.x = 0;
        actor.airborne = kAirborneFalling;
        return;
    case 1:
        if (actor.animEnded()) {
            setAnim(actor, DroneAnim::Rise);
            ++actor.step;
            soundPlayAtPos(kJumpSound, actor.pos);
        }
        [[fallthrough]];
    case 2:
        if (airStep(actor, drone)) {
            setAnim(actor, DroneAnim::RiseLand);
            actor.airborne = 0;
            actor.velocity.y = 0;
            actor.velocity.z = 0;
            ++actor.step;
        }
        return;
    case 3:
        if (actor.animEnded()) {
            setSub(actor, DroneSub::Hunt);
            actor.variant &= uint8_t(~kDroneIdleKindMask);
        }
        return;
    default: return;
    }
}

void hunt(AnimatedActor& actor, DroneFields& drone) { // exe 0x4a0500: turns toward the player and hops at it
    const uint8_t st = actor.step;
    switch (st) {
    case 0:
        drone.behaviour = kBehaveMoving | kBehaveKeepOnFall | kBehaveBounce;
        actor.velocity.z = 0;
        actor.velocity.y = 0;
        actor.acceleration.y = 0;
        drone.beats = kHuntBeats;
        setAnim(actor, DroneAnim::Idle);
        actor.enterStep(1);
        break;
    case 1:
        if ((rngStep() & 0x1f) == 0) soundPlayAtPos(kIdleSound, actor.pos);
        if (actor.animEnded()) ++actor.step;
        break;
    case 2:
        actor.step = uint8_t(st + 1);
        setAnim(actor, DroneAnim::Crouch);
        if ((rngStep() & 1) == 0) { // an idle beat (at most kHuntBeats in a row)
            if (drone.beats == 0) {
                drone.beats = kHuntBeats;
            } else {
                setAnim(actor, DroneAnim::Idle);
                actor.step = 1;
                --drone.beats;
            }
        }
        break;
    case 3:
        if (!actor.animEnded()) break;
        actor.step = uint8_t(st + 1);
        [[fallthrough]];
    case 4:
        soundPlayAtPos(kJumpSound, actor.pos);
        turnYawTo(actor.rotation.y, uint32_t(angleOf(player().pos, actor.pos)), kTurnStep);
        actor.velocity.z = kHuntHop;
        actor.velocity.y = kHuntHop;
        actor.acceleration.y = kGravity;
        actor.airborne = kAirborneFalling;
        ++actor.step;
        setAnim(actor, DroneAnim::Hop);
        break;
    case 5:
        if (airStep(actor, drone)) {
            stopAndLand(actor);
            ++actor.step;
            setAnim(actor, DroneAnim::Land);
        }
        break;
    case 6:
        if (actor.animEnded()) {
            setAnim(actor, DroneAnim::Idle);
            actor.step = 2;
        }
        break;
    default: break;
    }
    if (playerJumpingNear(actor, kHuntJumpNear)) setSub(actor, DroneSub::JumpAtPlayer);
}

// after a knock-down: dying when its hit points ran out, else stunned
void dyingOrStunned(AnimatedActor& actor) {
    if (actor.hitPoints < 0) {
        actor.enterState(uint8_t(DroneState::Dying));
        return;
    }
    setSub(actor, DroneSub::Stunned);
}

// exe 0x4a06f0: a deadly hit on the ground -- knocked back, then dying (or stunned)
void knockedDown(AnimatedActor& actor, DroneFields& drone) {
    const uint8_t st = actor.step;
    if (st == 0) {
        actor.enterStep(1);
        drone.behaviour = kBehaveNoDamage | kBehaveMoving | kBehaveKeepOnFall | kBehaveBounce;
        setAnim(actor, DroneAnim::Knocked);
        actor.velocity.z = kKnockBack;
        const uint32_t r = rngStep();
        actor.airborne = kAirborneFalling;
        actor.velocity.y = wrap16(kKnockUp - int(r & 0x3f));
        actor.acceleration.y = kGravity;
        actor.rotation.x = 0;
        soundPlayAtPos(kKnockedSound, actor.pos);
        resetModelScale(actor);
    } else if (st == 1) {
        airStep(actor, drone);
        if (actor.animEnded()) {
            actor.airborne = 0;
            actor.velocity.y = 0;
            actor.velocity.z = 0;
            setAnim(actor, DroneAnim::Down);
            dyingOrStunned(actor);
        }
    }
}
// exe 0x4a07c0: ... in the air -- tumbles, lands after a bounce
void knockedAirborne(AnimatedActor& actor, DroneFields& drone) {
    const uint8_t st = actor.step;
    if (st == 0) {
        actor.enterStep(1);
        drone.behaviour = kBehaveNoDamage | kBehaveMoving | kBehaveKeepOnFall | kBehaveBounce;
        setAnim(actor, DroneAnim::KnockedUp);
        actor.velocity.z = kKnockBack;
        const uint32_t r = rngStep();
        actor.velocity.y = wrap16(kKnockUpAirborne - int(r & 0x3f));
        actor.acceleration.y = kGravity;
        actor.rotation.x = 0;
        actor.airborne = kAirborneFalling;
        soundPlayAtPos(kKnockedSound, actor.pos);
        resetModelScale(actor);
    } else if (st == 1) {
        if (actor.animEnded()) {
            setAnim(actor, DroneAnim::Tumble);
            actor.step = 2;
        }
    } else if (st == 2 && actor.animEnded()) {
        setAnim(actor, DroneAnim::Down);
        actor.step = 3;
    }
    if (airStep(actor, drone)) {
        if (actor.stepCounter == 0) { // the first landing bounces
            actor.stepCounter = 1;
            return;
        }
        actor.airborne = 0;
        actor.velocity.y = 0;
        actor.velocity.z = 0;
        dyingOrStunned(actor);
    }
}

void active(const PlayerEnv& env, AnimatedActor& actor, DroneFields& drone) { // exe 0x49f9d0
    contactFromActors(actor, drone);
    actor.prevAnim = actor.anim;
    actor.tint = kTintNeutral;
    actor.savePreviousPose();
    actor.savePreviousContact();
    if (!(drone.behaviour & kBehaveNoDamage)) takeDamage(actor, drone);
    switch (DroneSub(actor.subState)) {
    case DroneSub::Reset: reset(actor, drone); break;
    case DroneSub::Wait: wait(actor, drone); break;
    case DroneSub::Hop: hop(actor, drone); break;
    case DroneSub::JumpAtPlayer: jumpAtPlayer(actor, drone); break;
    case DroneSub::TurnBack: turnBack(actor, drone); break;
    case DroneSub::Sidestep: sidestep(actor, drone); break;
    case DroneSub::Fall: fall(actor, drone); break;
    case DroneSub::Stunned: stunned(actor, drone); break;
    case DroneSub::GetUp: getUp(actor, drone); break;
    case DroneSub::Hunt: hunt(actor, drone); break;
    case DroneSub::KnockedDown: knockedDown(actor, drone); break;
    case DroneSub::KnockedAirborne: knockedAirborne(actor, drone); break;
    default: notPortedAt(kDroneArea, "0x49f9d0 sub-state", actor.subState);
    }
    if (collide(env, actor, drone)) {
        actor.airborne = kAirborneFalling;
        if (!(drone.behaviour & kBehaveKeepOnFall)) {
            actor.subState = uint8_t(DroneSub::Fall);
            actor.step = 0;
            actor.stepCounter = 0;
        }
    }
    if (actor.hitPoints >= 0 && playerLiftTargetCheck(actor, 0)) {
        actor.state = uint8_t(DroneState::Carried);
        actor.subState = uint8_t(CarriedSub::Hands);
        actor.step = 0;
        actor.stepCounter = 0;
        drone.behaviour |= kBehaveNoHitEntry;
    }
}

// the partner told this drone died (its +0xe)
void tellPartner(const AnimatedActor& actor, const DroneFields& drone) {
    if (actor.variant & kDronePaired) classState<DroneFields>(animatedPool()[size_t(int(drone.partner))]).partnerDied = 1;
}

void dying(const PlayerEnv& env, AnimatedActor& actor, DroneFields& drone) { // exe 0x4a0bc0: flash, explosion, drops
    contactFromActors(actor, drone);
    actor.prevAnim = actor.anim;
    actor.tint = kTintFlash;
    actor.savePreviousPose();
    actor.savePreviousContact();
    if (actor.step == 0) {
        actor.step = 1;
        if (actor.airborne == 0) {
            drone.clearVelocityBytes();
            drone.timer = kDeathFallTicks;
        } else {
            drone.timer = kDeathTicks;
        }
        actor.acceleration.y = kGravity;
        setAnim(actor, DroneAnim::Down);
        countDefeat();                            // exe 0x5e53e0
        if (const uint32_t x = allocActor60(0)) { // exe 0x5e1300: the explosion
            StaticActor& explosion = staticActorAt(x);
            explosion.flags = kActorLive;
            explosion.type = typeByte(GlobalStaticType::Explosion);
            explosion.variant = 0;
            explosion.loopFlags |= kActorSweepGroup2;
            explosion.pos.x.whole = actor.pos.x.whole;
            explosion.pos.y.whole = wrap16(actor.pos.y.whole - kExplosionRaise);
            explosion.pos.z.whole = actor.pos.z.whole;
            auto&& params = ExplosionParams(actorHeaderAt(x));
            params.spread = kExplosionSpread;
            params.flashes = uint16_t(drone.timer);
        }
        tellPartner(actor, drone);
    }
    if (drone.timer & 1) actor.tint = kHurtBlinkTint;
    if (drone.timer < kNoLockOnTicks) drone.behaviour |= kBehaveNoLockOn;
    drone.timer = wrap16(drone.timer - 1);
    if (drone.timer == 0) {
        spawnDrops(actor.pos, animatedStats(actor.type).dropTable);
        freeActor(actor); // exe 0x5e1490
        return;
    }
    integrateVelocity(actor, actor.velocity.x, actor.velocity.y, actor.velocity.z);
    if (actor.airborne != 0) actor.velocity.y = wrap16(actor.velocity.y + actor.acceleration.y);
    collide(env, actor, drone);
    if (actor.wasAirborne != 0 && actor.airborne == 0) {
        actor.velocity.z = 0;
        actor.velocity.x = 0;
    }
}

// exe 0x4a0a60: thrown (hurting what it hits) or dropped; two bounces, then it lands
void carriedFlying(const PlayerEnv& env, AnimatedActor& actor, DroneFields& drone) {
    if (actor.step == 0) {
        drone.flightYaw = uint16_t(actor.rotation.y);
        ++actor.step;
    }
    if ((drone.hitBox.result & (kHitAttackSolid | kHitAttackDeflects)) && actor.hitPoints >= 0) { // hit: dead at once
        const int16_t y = actor.pos.y.whole, z = actor.pos.z.whole;
        actor.velocity.z = wrap16(int(uint32_t(-int(actor.velocity.z)) >> 1));
        actor.hitPoints = -1;
        spawnHitSparkWithoutSize(kActorSweepGroup2, actor.pos.x.whole, wrap16(y - 0x40), z);
        drone.behaviour |= kBehaveNoHitEntry;
    }
    if (actor.stepCounter != 0) actor.rotation.y = wrap16(uint16_t(actor.rotation.y) + kTumbleSpin);
    bool landed; // exe 0x4a0b60 along the flight yaw
    if (drone.contact & kContactLanded) {
        actor.velocity.z = int16_t(actor.velocity.z >> 1);
        actor.velocity.y = wrap16(int(uint32_t(-int(actor.velocity.y)) >> 1));
        actor.rotation.x = 0;
        actor.airborne = kAirborneFalling;
        landed = true;
    } else {
        moveAlongYaw(actor.pos, int16_t(drone.flightYaw), 0, actor.velocity.y, actor.velocity.z);
        actor.velocity.y = wrap16(actor.velocity.y + actor.acceleration.y);
        landed = false;
    }
    if (landed) {
        if (actor.stepCounter < 2) {
            ++actor.stepCounter;
            integrateVelocity(actor, 0, actor.velocity.y, actor.velocity.z);
            drone.flightYaw = uint16_t(drone.flightYaw + kHalfTurn);
        } else { // dying, or stunned
            if (actor.hitPoints < 0)
                actor.enterState(uint8_t(DroneState::Dying));
            else
                actor.enterState(uint8_t(DroneState::Active), uint8_t(DroneSub::Stunned));
            actor.velocity.y = 0;
            actor.airborne = 0;
        }
    }
    collide(env, actor, drone);
    if (drone.contact & (kContactWallX | kContactWallZ)) drone.flightYaw = uint16_t(drone.flightYaw + kHalfTurn);
}

void carriedHands(const PlayerEnv& env, AnimatedActor& actor, DroneFields& drone) { // exe 0x4a0940
    if (actor.step == 0) {
        drone.behaviour = kBehaveNoHitEntry | kBehaveBounce;
        actor.step = 1;
        setAnim(actor, DroneAnim::Down);
        resetModelScale(actor);
        tellPartner(actor, drone);
    }
    carryAtHands(actor, 0, kCarryHeight, 0);
    const CarryRelease r = carryReleased();
    if (r == CarryRelease::Held) return;
    drone.behaviour &= uint8_t(~kBehaveNoHitEntry);
    drone.flightYaw = uint16_t(player().rotation.y);
    if (r == CarryRelease::Thrown) {
        int32_t v[3];
        playerAimVector(0, kThrowUp, kThrowForward, v); // exe 0x5e3e50
        actor.velocity.x = 0;
        actor.velocity.y = wrap16(v[1]);
        actor.velocity.z = wrap16(v[2]);
        drone.behaviour |= kBehaveThrown;
    } else {
        actor.velocity.y = kDropUp;
        actor.velocity.z = kDropForward;
    }
    actor.acceleration.y = kGravity;
    actor.rotation.y = int16_t(drone.flightYaw);
    actor.airborne = kAirborneFalling;
    carryDropFromPlayer(actor);
    actor.subState = uint8_t(CarriedSub::Flying);
    actor.step = 0;
    actor.stepCounter = 0;
    carriedFlying(env, actor, drone);
}

void carried(const PlayerEnv& env, AnimatedActor& actor, DroneFields& drone) { // exe 0x4a08d0
    if (actor.subState != 0) contactFromActors(actor, drone);
    actor.prevAnim = actor.anim;
    actor.savePreviousPose();
    actor.tint = kTintNeutral;
    actor.savePreviousContact();
    switch (CarriedSub(actor.subState)) {
    case CarriedSub::Hands: carriedHands(env, actor, drone); break;
    case CarriedSub::Flying: carriedFlying(env, actor, drone); break;
    default: notPortedAt(kDroneArea, "0x4a08d0 sub-state", actor.subState);
    }
}

// exe 0x49f7c0: the model (variant 1 with the flame), the boxes, the stats' hit points, the flame, the drift limit, the
// partner beside it
void spawn(AnimatedActor& actor, DroneFields& drone) {
    const int modelVariant = (actor.variant & kDroneFlame) != 0 ? 1 : 0;
    actor.flags |= kActorDrawn;
    if (!bindActorModel(actor, modelVariant)) {
        freeActor(actor);
        return;
    }
    const uint8_t runFlags = actor.runFlags, type = actor.type;
    actor.rotation.x = 0;
    actor.rotation.z = 0;
    setAnim(actor, DroneAnim::Idle);
    actor.prevAnim = kNoAnim;
    actor.runFlags = uint8_t(runFlags | kActorRunMarked);
    const uint16_t hp = statTableRow(actorStatTables().animated, type) != 0u ? animatedStats(type).hitPoints : uint16_t(0);
    actor.maxHitPoints = int16_t(hp);
    actor.hitPoints = int16_t(hp);
    actor.collisionBox.pointAt(droneBox.stateAddress());
    actor.interactionBox.pointAt(droneLiftBox.stateAddress());
    actor.interaction = kInteractLift;
    actor.airborne = 0;
    actor.onPlatform = 0;
    actor.clearClassBlock(); // exe 0x40dc40(+0x14c, +0x168)
    drone.hitBox.previousX = actor.pos.x.whole;
    drone.hitBox.previousZ = actor.pos.z.whole;
    drone.hitBox.result = 0;
    drone.drift = 0;
    drone.flameSlot = 0;
    if (modelVariant) {
        if (const uint32_t e = allocActorA0(kActorStageClass)) { // exe 0x5e13a0: the flame
            EffectActor& flame = effectActorAt(e);
            flame.flags = kActorLive;
            flame.type = kDroneFlameEffectType;
            flame.variant = 1;
            flame.loopFlags |= kActorSweepGroup2;
            auto&& params = DroneFlameParams(actorHeaderAt(e));
            params.ownerPad = 0;
            params.owner = actor.slot;
            flame.pos.x = 0;
            flame.pos.y = kFlameHeight;
            flame.pos.z = 0;
            params.size = kFlameSize;
            drone.flameSlot = flame.slot;
        }
    }
    if (!(actor.variant & kDroneDrifts))
        drone.driftLimit = 0;
    else if (!(actor.variant & kDronePaired))
        drone.driftLimit = kDriftLimit;
    else
        drone.driftLimit = kPairedDriftLimit;
    const uint16_t yaw = uint16_t(actor.rotation.y);
    drone.hopDistance = 0;
    drone.homeYaw = yaw;
    drone.side = 0;
    const uint8_t v = actor.variant;
    if (v & kDroneIdleKindMask) {
        if (!(v & kDroneIsPartner)) {
            const uint32_t p = (v & kDronePaired) ? allocActor20() : 0;
            if (p) { // the partner, kPartnerSpacing beside it
                AnimatedActor& partner = animatedActorAt(p);
                partner.flags = kActorLive;
                partner.type = kDronePartnerType;
                partner.loopFlags |= kActorSweepGroup2;
                partner.subtype = kDronePartnerSub;
                partner.variant = uint8_t(actor.variant | kDroneIsPartner);
                partner.rotation = actor.rotation;
                const uint32_t k = ((uint32_t(uint16_t(actor.rotation.y)) + kQuarterTurn) >> 6) & 0x3f;
                partner.pos.x.whole = wrap16(((int32_t(sin64(k)) << kPartnerSpacing) >> 12) + actor.pos.x.whole);
                partner.pos.z.whole = wrap16(((int32_t(cos64(k)) << kPartnerSpacing) >> 12) + actor.pos.z.whole);
                partner.pos.y.whole = actor.pos.y.whole;
                DroneFields& partnerFields = classState<DroneFields>(partner);
                partnerFields.copyParamsFrom(drone);
                partnerFields.partner = int8_t(actor.slot - kAnimatedClass);
                drone.partner = int8_t(partner.slot - kAnimatedClass);
            }
        } else {
            drone.side = 1;
        }
    }
    drone.hopRange = int16_t(int32_t(int8_t(uint16_t(drone.hopRange) & 0xff)) << 8);
    actor.enterState(uint8_t(DroneState::Active));
}

} // namespace

void hoppingDroneUpdate(const PlayerEnv& env, uint32_t address) {
    AnimatedActor& actor = animatedActorAt(address);
    auto&& drone = classState<DroneFields>(actor);
    switch (DroneState(actor.state)) {
    case DroneState::Spawn: spawn(actor, drone); break;
    case DroneState::Active: active(env, actor, drone); break;
    case DroneState::Carried: carried(env, actor, drone); break;
    case DroneState::Dying: dying(env, actor, drone); break;
    default: notPortedAt(kDroneArea, "0x49f660 state", actor.state);
    }
    if ((actor.variant & kDronePaired) && drone.partnerDied != 0) { // the partner died: widen the drift
        if (actor.variant & kDroneDrifts) {
            drone.drift = wrap16(drone.drift + (drone.side ? kPartnerDriftShift : -kPartnerDriftShift));
            drone.driftLimit = wrap16(drone.driftLimit + kPartnerDriftWiden);
        }
        actor.variant &= uint8_t(~kDronePaired);
    }
    if (!(actor.flags & kActorLive)) return;
    if (actor.anim == actor.prevAnim) // exe 0x5e1e80 / 0x5e1c30: the animation and the pose
        animStep(actor);
    else
        animSet(actor, actor.anim, 0);
    animPose(actor);
    const uint8_t b = drone.behaviour;
    const AnimatedActorStats stats = animatedStats(actor.type);
    const ShortVec4& point = bonePoint(actor, 0, kDroneArea);
    drone.hitBox.result = 0;
    drone.hitBox.previousX = actor.prevPos.x.whole;
    drone.hitBox.previousZ = actor.prevPos.z.whole;
    if (!(b & kBehaveNoHitEntry)) {
        const uint32_t noLockOn = (b & kBehaveNoLockOn) ? kHitNoLockOn : 0u;
        uint32_t shape, attack;
        if (DroneState(actor.state) == DroneState::Dying) {
            attack = kHitAttackBump;
            shape = hitOwner(actor.slot) | noLockOn | hitGroup(kHitGroupBodies) | hitRadius(kHitRadius);
        } else if (b & kBehaveThrown) {
            attack = uint32_t(stats.bodyAttack) | kHitAttackByPlayer;
            shape = hitOwner(actor.slot) | hitGroup(kHitGroupPlayerAttacks) | hitRadius(kHitRadius);
        } else {
            uint32_t c = (b & kBehaveStrongBody) ? (kHitAttackThrown | kHitAttackStrong | stats.strongAttack)
                                                 : (kHitAttackSolid | stats.attack);
            if (actor.variant & kDroneFlame) c |= kHitAttackBurning;
            attack = c;
            shape = hitOwner(actor.slot) | noLockOn | hitGroup(kHitGroupBodies) | hitRadius(kHitRadius);
        }
        const int16_t pos[3] = {point.x, point.y, point.z};
        hitListSubmit(pos, drone.hitBox, shape, attack);
    }
    // exe 0x5f20f0: a shadow list entry (render output) unless carried in the hands
}

} // namespace od::port
