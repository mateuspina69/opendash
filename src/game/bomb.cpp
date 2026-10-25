// The bomb, class 0x5a7330 (see bomb.hpp). States 0x8cee7c: 0 init 0x5a7500, 1 0x5a75b0 (sub-states 0x8cee90: 0
// falling / bouncing 0x5a75f0, 1 resting with the fuse lit 0x5a76f0), 2 carried / thrown 0x5a7770, 3 held by its
// carrier 0x5a78f0 (+0xc: 1 let go, 2 blow up), 4 the blast 0x5a7920. Its stats row (game/actor_stats): the hit
// points, the hit entry's damage, the blast's size.
#include "game/actor_state.hpp"
#include "game/bomb_state.hpp"
#include "game/bomb.hpp"

#include "engine/data_source.hpp"
#include "game/actor_collision.hpp"
#include "game/actor_model.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_stats.hpp"
#include "game/audio_logic.hpp"
#include "game/carry.hpp"
#include "game/collision.hpp"
#include "game/distances.hpp"
#include "game/explosions.hpp"
#include "game/global_class_types.hpp"
#include "game/hit_list.hpp"
#include "game/hit_points.hpp"
#include "game/player/player_motion.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/stage_state.hpp"

namespace od::port {

using namespace bomb_state;

namespace {

const PlayerEnv* E = nullptr;

constexpr const char* kBombPort = "bomb"; // the area of its port-gap errors


constexpr uint8_t kBombHitWall = 0x01, kBombBounced = 0x02, kBombOffGrid = 0x08;
constexpr uint8_t kBombBounces = 0x01;     // walls reflect its rotation
constexpr uint8_t kBombThrownEntry = 0x02; // the hit entry of a thrown bomb (else the falling one's)
constexpr uint8_t kBombNoHitEntry = 0x04;
constexpr uint8_t kBombHurtsPlayer = 0x08; // its blast hits the player too

enum class BombState : uint8_t {
    Init = 0,    // exe 0x5a7500
    Loose = 1,   // exe 0x5a75b0
    Carried = 2, // exe 0x5a7770
    Held = 3,    // exe 0x5a78f0
    Blast = 4,   // exe 0x5a7920
};
enum class LooseSub : uint8_t {
    Falling = 0, // exe 0x5a75f0
    Resting = 1, // exe 0x5a76f0
};
enum class CarriedSub : uint8_t {
    InHands = 0, // exe 0x5a7790
    Flying = 1,  // exe 0x5a7860
};

// the hit result flags it reacts to
constexpr uint32_t kHitShot = kHitAttackByPlayer;                 // the player's attacks hurt it
constexpr uint32_t kHitBreaks = kHitAttackSolid | kHitAttackBump; // a body's touch sets it off
constexpr int16_t kGravity = 0x40, kMaxFall = 0x800, kSpinStep = 0x100;
constexpr int16_t kDropUp = -0x200, kDropForward = -0x100;
constexpr int16_t kFuseTicks = 0x96, kFuseBlinkFrom = 0x5a;
constexpr uint16_t kTintFuseBlink = 0x1f;
constexpr uint8_t kBounces = 2;
constexpr uint16_t kBlastSize = 0x100;
constexpr uint16_t kSoundBounce = 0x18b, kSoundFuse = 0x18c;
constexpr int kPlayerHitHeight = 100;
constexpr int kHandsRaise = -0x14;     // its y at the player's hands
constexpr int kThrowUp = -0x100, kThrowForward = -0x400; // the throw's velocity (rotated by the player's pitch / yaw)
constexpr uint32_t kYawKickMask = 0x31; // a bounce turns it by (r & 0x31) * 2 - 0x20
constexpr int kYawKickBias = 0x20;

const DataTable<CollisionBoxRecord> bombCollisionBox{TableId::BombCollisionBox};
const DataTable<InteractionBoxRecord> bombLiftBox{TableId::BombLiftBox};

// the bomb's stats row (by its variant)
StaticActorStats statsOf(const StaticActor& actor) { return staticStats(actor.type, actor.variant); }

// exe 0x5a7a80: the room's wall test and push-out (its rotation reflected while it bounces)
void bombCollide(StaticActor& actor, BombFields& bomb) {
    bomb.collisionBits = 0;
    uint32_t r = actorTestMove(*E, actor); // exe 0x40f570
    if (r & kMoveNoCell) bomb.collisionBits |= kBombOffGrid;
    if (r & kMoveBlocked) {
        r = actorPushOut(*E, actor); // exe 0x410960
        actor.wallHit = r;
        if (r & kWallOnSides) bomb.collisionBits = kBombHitWall;
        if (bomb.behaviour & kBombBounces) {
            int16_t pitch = actor.rotation.x, yaw = actor.rotation.y;
            const int floor = reflectRotationOffWalls(pitch, yaw, r); // exe 0x410740 on +0x28
            actor.rotation.x = pitch;
            actor.rotation.y = yaw;
            if (floor != 0) bomb.collisionBits |= kBombBounced;
        }
    }
}
// exe 0x5a7a10: bounced -> 1, the speeds damped
int bombFall(StaticActor& actor, BombFields& bomb, int yaw) {
    if (bomb.collisionBits & kBombBounced) {
        actor.velocity.z = int16_t(actor.velocity.z >> 2);
        actor.velocity.y = int16_t(uint16_t(-int(actor.velocity.y) >> 2));
        actor.rotation.x = 0;
        return 1;
    }
    moveAlongYaw(actor.pos, yaw, 0, actor.velocity.y, actor.velocity.z);
    actor.velocity.y = int16_t(uint16_t(actor.velocity.y + actor.acceleration.y));
    if (actor.velocity.y > kMaxFall) actor.velocity.y = kMaxFall;
    return 0;
}
// exe 0x5a7920: the blast (its size from the stats)
void bombBlast(StaticActor& actor, BombFields& bomb) {
    if (actor.subState != 0) {
        freeActor(actor); // exe 0x5e1540
        return;
    }
    const uint8_t size = statsOf(actor).power;
    if (const uint32_t address = allocActor60(0)) { // exe 0x5e1300
        StaticActor& blast = staticActorAt(address);
        BlastParams params(blast);
        blast.flags = kActorLive;
        blast.loopFlags |= kActorSweepGroup2;
        blast.type = typeByte(GlobalStaticType::Blast);
        blast.variant = 0;
        blast.pos.x = actor.pos.x;
        blast.pos.y = actor.pos.y;
        params.size = kBlastSize;
        params.attack = size;
        blast.pos.z = actor.pos.z;
    }
    if (bomb.behaviour & kBombHurtsPlayer) {
        const Player& p = player();
        const int16_t at[3] = {p.pos.x.whole, int16_t(p.pos.y.whole - kPlayerHitHeight), p.pos.z.whole};
        hitListSubmit(at, bomb.hitBox,
                      hitOwner(actor.slot) | hitGroup(kHitGroupBodies) | kHitNoLockOn | hitRadius(0x80),
                      size | kHitAttackHarmless | kHitAttackStrong);
    }
    actor.subState = 1;
    actor.flags &= uint8_t(~kActorDrawn);
}
// the fuse ran out or it was set off: the blast state
void bombToBlast(StaticActor& actor, BombFields& bomb) {
    actor.enterState(uint8_t(BombState::Blast));
    bombBlast(actor, bomb);
}
// exe 0x5a7500: the model, the hit points, the boxes; loose (variant 0) or held by its carrier
void bombInit(StaticActor& actor) {
    if (!bindActorModel(actor, 0)) {
        freeActor(actor);
        return;
    }
    actor.flags |= kActorDrawn;
    const uint16_t hp = hasStaticStats(actor.type) ? uint16_t(statsOf(actor).hitPoints) : uint16_t(0);
    actor.maxHitPoints = int16_t(hp);
    actor.hitPoints = int16_t(hp);
    actor.velocity.x = 0;
    actor.velocity.y = 0;
    actor.rotation.z = 0;
    actor.rotation.x = 0;
    actor.airborne = 1;
    actor.onPlatform = 0;
    actor.collisionBox.pointAt(bombCollisionBox.stateAddress());
    actor.interactionBox.pointAt(bombLiftBox.stateAddress());
    actor.interaction = kInteractLift;
    actor.clearClassBlock();
    actor.acceleration.y = kGravity;
    actor.enterState(uint8_t(actor.variant == 0 ? BombState::Loose : BombState::Held));
}
// a hit by a shot: the flash and the hit points (none left -> the blast); true = blown up
bool bombShot(StaticActor& actor, BombFields& bomb, uint32_t h) {
    if ((h & kHitDamageMask) && (h & kHitShot)) {
        actor.tint = kTintFlash;
        if (hitPointsAdd(actor, -int(h & kHitDamageMask)) != 0) {
            bombToBlast(actor, bomb);
            return true;
        }
    }
    return false;
}
// exe 0x5a75f0: three bounces, then resting
void bombFalling(StaticActor& actor, BombFields& bomb) {
    const uint32_t h = bomb.hitBox.result;
    if (bombShot(actor, bomb, h)) return;
    if (h & kHitBreaks) {
        bombToBlast(actor, bomb);
        return;
    }
    if (actor.step == 0) {
        bomb.behaviour = kBombBounces;
        actor.step = 1;
        actor.stepCounter = 0;
    }
    if (bombFall(actor, bomb, actor.rotation.y) != 0) {
        soundPlayAtPos(kSoundBounce, actor.pos);
        if (actor.stepCounter < kBounces) {
            const uint32_t r = rngStep();
            actor.rotation.y = int16_t(uint16_t(actor.rotation.y + int((r & kYawKickMask) * 2) - kYawKickBias));
            integrateVelocity(actor, 0, actor.velocity.y, actor.velocity.z);
            actor.velocity.y = int16_t(uint16_t(actor.velocity.y + actor.acceleration.y));
            actor.airborne = 1;
            actor.rotation.x = int16_t(uint16_t(actor.rotation.x + kSpinStep));
            ++actor.stepCounter;
            return;
        }
        actor.airborne = 0;
        actor.subState = uint8_t(LooseSub::Resting);
        actor.step = 0;
        actor.stepCounter = 0;
    }
    actor.rotation.x = int16_t(uint16_t(actor.rotation.x + kSpinStep));
}
// exe 0x5a76f0: the fuse lit once
void bombResting(StaticActor& actor, BombFields& bomb) {
    if (bombShot(actor, bomb, bomb.hitBox.result)) return;
    if (actor.step != 0) return;
    bomb.behaviour = kBombNoHitEntry;
    if (bomb.fuseLit == 0) {
        bomb.fuseLit = 1;
        bomb.fuse = kFuseTicks;
    }
    actor.step = 1;
    actor.stepCounter = 0;
}
// exe 0x5a7860: thrown / dropped -- one bounce, then resting
void bombFlying(StaticActor& actor, BombFields& bomb) {
    if (bomb.hitBox.result & kHitBreaks) {
        bombToBlast(actor, bomb);
        return;
    }
    actor.rotation.x = int16_t(uint16_t(actor.rotation.x + kSpinStep));
    if (bombFall(actor, bomb, actor.rotation.y) != 0) {
        if (actor.step == 0) {
            integrateVelocity(actor, 0, actor.velocity.y, actor.velocity.z);
            actor.velocity.y = int16_t(uint16_t(actor.velocity.y + actor.acceleration.y));
            actor.airborne = 1;
            actor.step = 1;
            actor.stepCounter = 0;
            bombCollide(actor, bomb);
            return;
        }
        actor.airborne = 0;
        actor.enterState(uint8_t(BombState::Loose), uint8_t(LooseSub::Resting));
    }
    bombCollide(actor, bomb);
}
// exe 0x5a7790: until let go (thrown / dropped)
void bombInHands(StaticActor& actor, BombFields& bomb) {
    if (actor.step == 0) bomb.behaviour = kBombBounces | kBombNoHitEntry | kBombHurtsPlayer;
    carryAtHands(actor, 0, kHandsRaise, 0);
    const CarryRelease r = carryReleased();
    if (r == CarryRelease::Held) return;
    if (r == CarryRelease::Thrown) {
        int32_t v[3];
        playerAimVector(0, kThrowUp, kThrowForward, v); // exe 0x5e3e50
        actor.velocity.x = 0;
        actor.velocity.y = int16_t(v[1]);
        actor.velocity.z = int16_t(v[2]);
        bomb.behaviour = kBombBounces | kBombThrownEntry;
    } else {
        actor.velocity.y = kDropUp;
        actor.velocity.z = kDropForward;
        bomb.behaviour = kBombBounces;
    }
    actor.acceleration.y = kGravity;
    actor.rotation.y = player().rotation.y;
    actor.airborne = 1;
    carryDropFromPlayer(actor);
    actor.subState = uint8_t(CarriedSub::Flying);
    actor.enterStep(0);
    bombFlying(actor, bomb);
}
// exe 0x5a7330's body: the state, the out-of-map free, the fuse, the hit entry, the collision queues
void bombStates(StaticActor& actor) {
    BombFields& bomb = classState<BombFields>(actor);
    actor.tint = kTintNeutral;
    actor.savePreviousPose();
    actor.savePreviousContact();
    switch (BombState(actor.state)) {
    case BombState::Init: bombInit(actor); break;
    case BombState::Loose:
        if (actor.subState == uint8_t(LooseSub::Falling)) bombFalling(actor, bomb);
        else if (actor.subState == uint8_t(LooseSub::Resting)) bombResting(actor, bomb);
        else notPortedAt(kBombPort, "sub-state", actor.subState);
        bombCollide(actor, bomb);
        if (playerLiftTargetCheck(actor, 0)) { // exe 0x573a60
            actor.enterState(uint8_t(BombState::Carried));
            bomb.behaviour |= kBombNoHitEntry | kBombHurtsPlayer;
        }
        break;
    case BombState::Carried:
        if (actor.subState == uint8_t(CarriedSub::InHands)) bombInHands(actor, bomb);
        else bombFlying(actor, bomb);
        break;
    case BombState::Held:
        if (actor.subState == 0) {
            bomb.behaviour = kBombNoHitEntry;
            actor.subState = 1;
        }
        if (bomb.carrierCommand == kCarrierLetGo) actor.enterState(uint8_t(BombState::Loose));
        else if (bomb.carrierCommand == kCarrierBlowUp) actor.enterState(uint8_t(BombState::Blast));
        break;
    case BombState::Blast: bombBlast(actor, bomb); break;
    default: notPortedAt(kBombPort, "state", actor.state);
    }
    if (!(actor.flags & kActorLive)) return;
    const int far = int(uint32_t(stage().depthRange) << 2);
    if ((bomb.collisionBits & kBombOffGrid) && distance3(actor.pos, player().pos) > far) {
        freeActor(actor);
        return;
    }
    if (actor.pos.y.whole > far) {
        freeActor(actor);
        return;
    }
    if (bomb.fuseLit != 0) {
        const int16_t t = int16_t(bomb.fuse - 1);
        bomb.fuse = t;
        if (t < kFuseBlinkFrom && (t & 1)) {
            actor.tint = kTintFuseBlink;
            if (t % 3 == 0) soundPlayAtPos(kSoundFuse, actor.pos);
        }
        if (t < 0) {
            bomb.fuseLit = 0;
            actor.enterState(uint8_t(BombState::Blast));
        }
    }
    const uint8_t f = bomb.behaviour;
    if (!(f & kBombNoHitEntry)) {
        const int16_t p[3] = {actor.pos.x.whole, actor.pos.y.whole, actor.pos.z.whole};
        bomb.hitBox.result = 0;
        bomb.hitBox.previousX = actor.prevPos.x.whole;
        bomb.hitBox.previousZ = actor.prevPos.z.whole;
        const uint32_t owner = hitOwner(actor.slot) | kHitNoLockOn | hitRadius(0x28), d = statsOf(actor).damage;
        if (!(f & kBombThrownEntry))
            hitListSubmit(p, bomb.hitBox, owner | hitGroup(kHitGroupBodies),
                          d | kHitAttackHarmless | kHitAttackStrong);
        else
            hitListSubmit(p, bomb.hitBox, owner | hitGroup(kHitGroupPlayerAttacks), d | kHitAttackByPlayer);
    }
    // state != 2: exe 0x5f20f0(a, 0x20, 0x100, 0) shadow list entry (render output)
}

} // namespace

void bombUpdate(const PlayerEnv& env, uint32_t address) {
    E = &env;
    bombStates(staticActorAt(address));
}

} // namespace od::port
