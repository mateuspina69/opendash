// ST27 sub 8, the lava mission: Bon Bonne (pool 0x20 type 0x51 sub 1, exe 0x4dc340; hypothesis names), one routine per
// exe routine. See st27_bon.hpp.
//
// His bytes are the partners' (content/saul_kada/st27_partners); the Reaverbots he picks, carries and throws are the
// hopping Reaverbots (content/saul_kada/hopping_reaverbot), told through their handling byte.
#include "content/saul_kada/st27_bon.hpp"

#include "content/saul_kada/hopping_reaverbot.hpp"
#include "content/saul_kada/st27_partners.hpp"

#include "game/actor_animation.hpp"
#include "game/actor_model.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actor_stats.hpp"
#include "game/angles.hpp"
#include "game/enemy_motion.hpp"
#include "game/audio_logic.hpp"
#include "game/collision.hpp"
#include "game/cutscene_actor.hpp"
#include "game/distances.hpp"
#include "game/encounter.hpp"
#include "game/hit_list.hpp"
#include "game/hit_points.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/story.hpp"
#include "game/story_flags.hpp"
#include "game/vectors.hpp"

namespace od::port {

namespace {

// [0x8957d4]
enum class BonState : uint8_t { Init = 0, Main = 1, Free = 2 };
// [0x8957e0]
enum class BonSub : uint8_t { WalkIn = 0, Chase = 1, KnockedDown = 2, Carry = 3 };
enum class WalkInStep : uint8_t { Back = 0, Brake = 1 };
enum class ChaseStep : uint8_t { Pick = 0, Walk = 1, TurnFromWall = 2 };
enum class KnockedStep : uint8_t { Start = 0, Fall = 1, Rise = 2 };
enum class CarryStep : uint8_t { Lift = 0, Carry = 1, Face = 2, Throw = 3, Recover = 4 };
enum BonAnim : uint8_t {
    kAnimIdle = 0, kAnimWalk = 2, kAnimKnocked = 7, kAnimGetUp = 8, kAnimLift = 0x19, kAnimCarry = 0x1a,
    kAnimThrow = 0x1b,
};

constexpr uint32_t kFlagMissionAccepted = 0x681;
constexpr int16_t kWalkSpeed = int16_t(0xfec0), kApproachSpeed = int16_t(0xff60), kWalkInAccel = 0x140;
constexpr uint16_t kHurtSound = 0x8d, kAtPlayerSound = 0x230, kLiftSound = 0x231, kThrowSound = 0x232;
constexpr uint32_t kKnockedVoice = 0x8039;
constexpr uint8_t kLiftKey = 8, kReleaseKey = 0xb;
constexpr uint32_t kPartnerShape = hitGroup(kHitGroupBodies) | kHitNoLockOn;
// where the carried Reaverbot sits: bone 4's offset
constexpr uint32_t kCarryBone = 4;
constexpr int16_t kCarryOffset[3] = {2, 0x123, -0x17};

// the Reaverbot of a target index (a pool-0x20 slot index) by the exe's unchecked arithmetic (kPartnerNoTarget reads
// past the pool, as in the exe)
AnimatedActor& reaverbotAt(uint8_t index) { return animatedActorAt(animatedPoolSlot(int32_t(kAnimatedClass) + index)); }
HoppingReaverbotParams handlingOf(const AnimatedActor& reaverbot) { return HoppingReaverbotParams(reaverbot); }

int bearingFrom(const AnimatedActor& actor, const Fixed16Vec3& point) { return angleOf(point, actor.pos); }
int bearingFrom(const AnimatedActor& actor, const int32_t point[3]) {
    return angleOf(point[0], point[2], actor.pos.x, actor.pos.z);
}

// the tail of every exit of exe 0x4dc960: the target marked in his entry of the enemy groups
void markTarget(PartnerFields& bon) {
    enemyGroupMark(int32_t(bon.group), uint16_t(1u << (bon.target & 0x1f)));
}
// the slot not marked in his entry, or it is his own target
bool slotFree(PartnerFields& bon, uint32_t i) {
    const uint32_t marks = enemyGroupMarks(int32_t(bon.group), EnemyGroupMarks::Either); // exe 0x5e6d60(+0x163, 0)
    return !((1u << (i & 0x1f)) & marks) || bon.target == i;
}

// exe 0x4dcf70: among the live Reaverbots (slots 0..15) not grabbed / carried / taken, the farthest one (planar,
// unsigned compare) not marked by someone else; among the stunned ones the farthest that is nearer to him than to the
// partner, unless the partner already has a ready target. A stunned one found -> kPartnerReadyTarget and it is
// returned; else the plain farthest (kPartnerNoTarget = none)
uint8_t pickReaverbot(AnimatedActor& actor, PartnerFields& bon) {
    const PartnerFields& partner = classState<PartnerFields>(*bon.partner);
    const AnimatedActor& partnerActor = *bon.partner;
    uint32_t bestReady = 0, bestAny = 0;
    uint8_t ready = kPartnerNoTarget, any = kPartnerNoTarget;
    for (uint32_t i = 0; i < kPartnerScanSlots; ++i) {
        AnimatedActor& r = animatedPool()[i];
        if (r.flags == 0 || r.type != kHoppingReaverbotType || (handlingOf(r).handling & kHandlingBusy)) continue;
        const uint32_t d = uint32_t(planarDistance(actor.pos, r.pos)); // exe 0x5e3b00
        if ((handlingOf(r).handling & kHandlingStunned) && bestReady < d && slotFree(bon, i)) {
            if ((partner.flags & kPartnerReadyTarget) || d < uint32_t(planarDistance(partnerActor.pos, r.pos))) {
                bestReady = d;
                ready = uint8_t(i);
            }
        }
        if (bestAny < d && slotFree(bon, i)) {
            bestAny = d;
            any = uint8_t(i);
        }
    }
    if (ready != kPartnerNoTarget) {
        bon.flags |= kPartnerReadyTarget;
        return ready;
    }
    bon.flags &= uint8_t(~kPartnerReadyTarget);
    return any;
}

// exe 0x4dd0c0: facing the target (0x40): the yaw onto it; within 0xdf -> carry it (grabbed, the carry heading = the
// yaw); false. Not facing: turn 0x20, the speed toward -0xa0 by 4 (moved and pushed out while faster); true
bool approachAndGrab(const PlayerEnv& env, AnimatedActor& actor, PartnerFields& bon) {
    const AnimatedActor& target = reaverbotAt(bon.target);
    if (facingWithin(actor, target.pos, 0x40)) {
        actor.rotation.y = int16_t(bearingFrom(actor, target.pos));
        if (planarDistance(actor.pos, target.pos) <= 0xdf) {
            actor.subState = uint8_t(BonSub::Carry);
            actor.step = uint8_t(CarryStep::Lift);
            actor.anim = kAnimLift;
            handlingOf(reaverbotAt(bon.target)).handling = kHandlingGrabbed;
            bon.carryYaw = actor.rotation.y;
        }
        return false;
    }
    turnYawTo(actor.rotation.y, uint32_t(bearingFrom(actor, target.pos)), 0x20); // exe 0x5e3df0
    const int16_t v = actor.velocity.z;
    if (v <= -0xa1) {
        actor.velocity.z = int16_t(v + 4);
        integrateVelocity(actor, 0, 0, int16_t(v + 4)); // exe 0x5e3690
        actorPushOut(env, actor);                       // exe 0x410960
        return true;
    }
    actor.velocity.z = kApproachSpeed;
    return true;
}

// exe 0x4dc890: before the mission, his x follows the player's; then he walks backwards (yaw + 0x800, the speed toward
// -0x140) until his z is within 0x14 of 0, then brakes to a stop -> the chase
void walkIn(AnimatedActor& actor) {
    if (!storyFlag(kFlagMissionAccepted)) {
        actor.pos.x.whole = player().pos.x.whole;
        return;
    }
    // the exe's stack point: his integer x, y = z = 0 (the planar distance: |z|)
    const int32_t line[3] = {int32_t(uint32_t(uint16_t(actor.pos.x.whole)) << 16), 0, 0};
    if (planarDistance(actor.pos, line) <= 0x14) ++actor.step;
    moveAlongYaw(actor.pos, actor.rotation.y + 0x800, 0, 0, actor.velocity.z); // exe 0x5e36f0
    const int16_t speed = actor.velocity.z;
    if (WalkInStep(actor.step) == WalkInStep::Back) {
        if (speed > kWalkSpeed) actor.velocity.z = int16_t(uint16_t(speed) - uint16_t(actor.acceleration.z));
        else actor.velocity.z = kWalkSpeed;
        return;
    }
    if (speed < 0) {
        actor.velocity.z = int16_t(uint16_t(actor.acceleration.z) + uint16_t(speed));
        return;
    }
    actor.velocity.z = 0;
    actor.subState = uint8_t(BonSub::Chase);
    actor.step = uint8_t(ChaseStep::Pick);
}

// exe 0x4dc960: pick a target; walk to it while it lives (a new pick replaces it when he faces the new one within
// 0x100, it is a ready one, or the old one is taken), within 0x1ff of a stunned one -> approach and grab, else walk --
// a wall turns him to the reflected yaw; every exit marks the target
void chase(const PlayerEnv& env, AnimatedActor& actor, PartnerFields& bon) {
    const ChaseStep step = ChaseStep(actor.step);
    if (step == ChaseStep::Pick) {
        const uint8_t pick = pickReaverbot(actor, bon);
        bon.target = pick;
        if (pick == kPartnerNoTarget) {
            markTarget(bon);
            return;
        }
        ++actor.step;
        markTarget(bon);
        return;
    }
    if (step == ChaseStep::Walk) {
        if (reaverbotAt(bon.target).flags != 0) {
            const uint8_t pick = pickReaverbot(actor, bon);
            if (pick == kPartnerNoTarget) {
                markTarget(bon);
                return;
            }
            if (bon.target != pick) {
                if (facingWithin(actor, reaverbotAt(pick).pos, 0x100) || (bon.flags & kPartnerReadyTarget) ||
                    (handlingOf(reaverbotAt(bon.target)).handling & kHandlingTaken))
                    bon.target = pick;
            }
            if (planarDistance(actor.pos, reaverbotAt(bon.target).pos) <= 0x1ff &&
                (handlingOf(reaverbotAt(bon.target)).handling & kHandlingStunned)) {
                if (approachAndGrab(env, actor, bon)) {
                    markTarget(bon);
                    return;
                }
            }
            turnYawTo(actor.rotation.y, uint32_t(bearingFrom(actor, reaverbotAt(bon.target).pos)), 0x20);
            integrateVelocity(actor, 0, 0, kWalkSpeed);
            const uint32_t result = actorPushOut(env, actor); // exe 0x410960
            if (!(result & kWallOnSides)) {
                markTarget(bon);
                return;
            }
            const int16_t yaw = actor.rotation.y;
            reflectRotationOffWalls(actor.rotation, result); // exe 0x410740
            bon.wallYaw = actor.rotation.y;
            actor.rotation.y = yaw;
            ++actor.step;
            markTarget(bon);
            return;
        }
    } else if (step == ChaseStep::TurnFromWall) {
        if (turnYawTo(actor.rotation.y, uint32_t(int32_t(bon.wallYaw)), 0x20) != 0) {
            markTarget(bon);
            return;
        }
    } else {
        markTarget(bon);
        return;
    }
    actor.step = uint8_t(ChaseStep::Pick);
    markTarget(bon);
}

// exe 0x4dcb30: knocked down (animation 7), getting up (8; each until the animation ends), then the chase
void knockedDown(AnimatedActor& actor, PartnerFields& bon) {
    const uint8_t step = actor.step;
    if (KnockedStep(step) == KnockedStep::Start) {
        actor.anim = kAnimKnocked;
        actor.step = uint8_t(KnockedStep::Fall);
        return;
    }
    if (step > uint8_t(KnockedStep::Rise) || !actor.animEnded()) return;
    if (step >= uint8_t(KnockedStep::Rise)) {
        bon.flags &= uint8_t(~kPartnerKnocked);
        actor.subState = uint8_t(BonSub::Chase);
        actor.step = uint8_t(ChaseStep::Pick);
        actor.anim = kAnimWalk;
        return;
    }
    actor.anim = kAnimGetUp;
    actor.step = uint8_t(step + 1u);
}

// exe 0x4dcb90: the goal = the player (kPartnerThrowAtPlayer) or the lava. Lift (from key 8 it is carried), carry
// toward the goal (the carry heading turns, the body turns separately until facing within 0x200) until facing within
// 0x40 and nearer than 0x300 (0x800 to the player), face it, throw (released at key 0xb), back to the chase
void carryAndThrow(const PlayerEnv& env, AnimatedActor& actor, PartnerFields& bon) {
    const uint8_t toPlayer = bon.flags & kPartnerThrowAtPlayer;
    int32_t goal[3];
    if (toPlayer) {
        goal[0] = player().pos.x;
        goal[1] = player().pos.y;
        goal[2] = player().pos.z;
    } else {
        goal[0] = kLavaX;
        goal[1] = kLavaY;
        goal[2] = kLavaZ;
    }
    switch (CarryStep(actor.step)) {
    case CarryStep::Lift: {
        if (actor.animEnded()) {
            bon.flags |= kPartnerCarrying;
            actor.anim = kAnimCarry;
            ++actor.step;
            actor.stepCounter = 0;
        } else {
            if (actor.keyIndex == kLiftKey) soundPlayAtPos(kLiftSound, actor.pos); // exe 0x5d5380
            if (int8_t(actor.keyIndex) >= int8_t(kLiftKey)) bon.flags |= kPartnerCarrying;
        }
        const int16_t speed = actor.velocity.z;
        if (speed < kWalkSpeed) actor.velocity.z = int16_t(speed + 4);
        else actor.velocity.z = kWalkSpeed;
        integrateVelocity(actor, 0, 0, actor.velocity.z);
        actorPushOut(env, actor);
        actorPushOut(env, actor);
        return;
    }
    case CarryStep::Carry: {
        int16_t yaw = actor.rotation.y;
        actor.rotation.y = bon.carryYaw;
        if (facingWithin(actor, goal[0], goal[2], 0x200)) {
            actor.rotation.y = yaw;
            turnYawTo(actor.rotation.y, uint32_t(bearingFrom(actor, goal)), 0x20);
            const int16_t speed = actor.velocity.z;
            if (speed > kWalkSpeed) actor.velocity.z = int16_t(speed - 4);
            else actor.velocity.z = kWalkSpeed;
            integrateVelocity(actor, 0, 0, actor.velocity.z);
            if (facingWithin(actor, goal[0], goal[2], 0x40)) {
                const int reach = (bon.flags & kPartnerThrowAtPlayer) ? 0x800 : 0x300;
                if (planarDistance(actor.pos, goal) < reach) {
                    ++actor.step;
                    actorPushOut(env, actor);
                    return;
                }
            }
            break;
        }
        // not facing: the carry heading (in the yaw) turns 0x20 toward the goal
        turnYawTo(actor.rotation.y, uint32_t(bearingFrom(actor, goal)), 0x20);
        const int16_t turned = actor.rotation.y;
        const int16_t delta = int16_t(int32_t(turned) - int32_t(bon.carryYaw));
        bon.carryYaw = turned;
        if (actor.stepCounter != 0) {
            yaw = int16_t(delta + yaw); // the body follows the carry heading's turn
            actor.rotation.y = bon.carryYaw;
        } else {
            actor.rotation.y = yaw;
            if (facingWithin(actor, goal[0], goal[2], 0x200)) {
                actor.stepCounter = 1;
                actor.rotation.y = bon.carryYaw;
            } else {
                turnYawTo(actor.rotation.y, uint32_t(bearingFrom(actor, goal)), 0x40);
                yaw = actor.rotation.y;
                actor.rotation.y = bon.carryYaw;
            }
        }
        const int16_t speed = actor.velocity.z;
        if (speed > kWalkSpeed) actor.velocity.z = int16_t(speed - 4);
        else actor.velocity.z = kWalkSpeed;
        integrateVelocity(actor, 0, 0, actor.velocity.z);
        actor.rotation.y = yaw;
        actorPushOut(env, actor);
        return;
    }
    case CarryStep::Face:
        if (!facingWithin(actor, goal[0], goal[2], 0x20)) {
            turnYawTo(actor.rotation.y, uint32_t(bearingFrom(actor, goal)), 0x20);
            actorPushOut(env, actor);
            return;
        }
        actor.rotation.y = int16_t(bearingFrom(actor, goal));
        ++actor.step;
        actor.anim = kAnimThrow;
        actorPushOut(env, actor);
        return;
    case CarryStep::Throw:
        if (actor.keyIndex == kReleaseKey) {
            handlingOf(reaverbotAt(bon.target)).handling =
                toPlayer ? uint8_t(kHandlingThrown | kHandlingAtPlayer) : kHandlingThrown;
            soundPlayAtPos(kThrowSound, actor.pos);
            bon.flags &= uint8_t(~kPartnerCarrying);
            ++actor.step;
            actorPushOut(env, actor);
            return;
        }
        break;
    case CarryStep::Recover:
        if (actor.animEnded()) {
            actor.anim = kAnimIdle;
            actor.subState = uint8_t(BonSub::Chase);
            actor.step = uint8_t(ChaseStep::Pick);
        }
        break;
    default: break;
    }
    actorPushOut(env, actor);
}

// exe 0x4dc4c0: the model (none: freed), the hit points (his row's * 4), his box, his entry of the enemy groups
// (looked up as a Reaverbot)
void init(AnimatedActor& actor, PartnerFields& bon) {
    if (!bindActorModel(actor, 0)) { // exe 0x5e0ba0
        freeActor(actor);            // exe 0x5e1490
        return;
    }
    actor.flags |= kActorDrawn;
    actor.anim = kAnimIdle;
    bon.animKey = 0;
    actor.prevAnim = kNoAnimShown;
    const uint8_t hp = animatedStats(actor.type, actor.subtype).hitPoints;
    actor.collisionBox.pointAt(TableId::St27BonBox);
    actor.interactionBox = nullptr;
    actor.interaction = 0;
    actor.maxHitPoints = int16_t(uint32_t(hp) * 4u);
    actor.hitPoints = int16_t(uint32_t(hp) * 4u);
    actor.velocity.clearWords();
    actor.rotation.z = 0;
    actor.rotation.x = 0;
    actor.acceleration.z = kWalkInAccel;
    actor.airborne = 1;
    actor.onPlatform = 0;
    actor.platform = nullptr;
    actor.actorHit = 0;
    bon.bodyBox.result = 0;
    bon.handBox.result = 0;
    bon.flags = 0;
    actor.type = kHoppingReaverbotType;
    bon.group = uint8_t(enemyGroupFindFor(actor, 0)); // exe 0x5e6cd0(a, 0) with the Reaverbots' type
    actor.subState = uint8_t(BonSub::WalkIn);
    actor.step = 0;
    actor.stepCounter = 0;
    actor.type = kPartnerBonType;
    actor.state = uint8_t(BonState::Main);
}

// exe 0x4dc590: out of hit points -> kPartnerThrowAtPlayer; a hit (not while knocked down): the flash, sound 0x8d, the
// damage; still alive -> the carried one dropped, knocked down, voice 0x8039 when none plays. Then the sub-state and
// six hit-list entries on bones 0 / 4 / 6 (the hand) / 7 / 7 / 8
void fight(const PlayerEnv& env, AnimatedActor& actor, PartnerFields& bon) {
    const AnimatedActorStats stats = animatedStats(actor.type, actor.subtype);
    if (actor.hitPoints <= 0) bon.flags |= kPartnerThrowAtPlayer;
    if ((bon.bodyBox.result & kHitAttackByPlayer) && !(bon.flags & kPartnerKnocked)) {
        actor.tint = kTintFlash;
        soundPlayAtPos(kHurtSound, actor.pos);
        if (!hitPointsAdd(actor, -int(bon.bodyBox.result & kHitDamageMask))) { // exe 0x5e52f0
            if (BonSub(actor.subState) == BonSub::Carry) {
                handlingOf(reaverbotAt(bon.target)).handling &= uint8_t(~kHandlingHeld);
                bon.flags &= uint8_t(~kPartnerCarrying);
            }
            bon.flags |= kPartnerKnocked;
            actor.subState = uint8_t(BonSub::KnockedDown);
            actor.step = uint8_t(KnockedStep::Start);
            if (!voiceClipPlaying()) voiceClipPlay(kKnockedVoice); // exe 0x601260 / 0x601050
        }
    }
    switch (BonSub(actor.subState)) {
    case BonSub::WalkIn: walkIn(actor); break;
    case BonSub::Chase: chase(env, actor, bon); break;
    case BonSub::KnockedDown: knockedDown(actor, bon); break;
    case BonSub::Carry: carryAndThrow(env, actor, bon); break;
    default: notPortedAt(kStoryPort, "0x4dc590 sub (table 0x8957e0)", actor.subState);
    }
    bon.handBox.previousX = actor.prevPos.x.whole;
    bon.bodyBox.previousX = actor.prevPos.x.whole;
    bon.handBox.previousZ = actor.prevPos.z.whole;
    bon.bodyBox.previousZ = actor.prevPos.z.whole;
    bon.handBox.result = 0;
    bon.bodyBox.result = 0;
    const uint32_t owner = hitOwner(actor.slot) | hitHeightLevel(stats.hitHeight);
    struct BonHit {
        uint32_t bone;
        int16_t offset[3];
        bool hand;
        uint32_t radius, attack;
    };
    const BonHit hits[6] = {
        {0, {0, 0, 0}, false, 0xa0, kHitAttackSolid},        {4, {0, 0x80, 0}, false, 0x60, kHitAttackSolid},
        {6, {0x50, 0x60, 0}, true, 0x80, kHitAttackDeflects}, {7, {-0x60, 0x10, 0}, false, 0x60, kHitAttackSolid},
        {7, {0x60, 0x10, 0}, false, 0x60, kHitAttackSolid},   {8, {0, 0, 0}, false, 0x60, kHitAttackSolid},
    };
    for (const BonHit& h : hits) {
        int16_t point[3];
        bonePointOffset(actor, h.bone, h.offset, point); // exe 0x5e1970
        hitListSubmit(point, h.hand ? bon.handBox : bon.bodyBox, owner | kPartnerShape | hitRadius(h.radius),
                      stats.attack | h.attack); // exe 0x5e4120
    }
}

// exe 0x4dc340
void bonBonne(const PlayerEnv& env, AnimatedActor& actor) {
    PartnerFields& bon = classState<PartnerFields>(actor);
    actor.prevAnim = actor.anim;
    actor.tint = kTintNeutral;
    actor.savePreviousPose();
    actor.savePreviousContact();
    switch (BonState(actor.state)) {
    case BonState::Init: init(actor, bon); break;
    case BonState::Main: fight(env, actor, bon); break;
    case BonState::Free: freeActor(actor); break; // 0x468fa0
    default: notPortedAt(kStoryPort, "0x4dc340 state (table 0x8957d4)", actor.state);
    }
    if (actor.anim != actor.prevAnim) animSet(actor, actor.anim, bon.animKey); // exe 0x5e1c50
    else if (bon.flags & kPartnerHoldPose) animDecodeCurrent(actor);          // exe 0x5e1f30
    else animStep(actor);                                                      // exe 0x5e1ea0
    animPose(actor);                                                           // exe 0x5e2d90
    if (!(bon.flags & kPartnerCarrying)) return;
    // carrying: the Reaverbot at bone 4's offset, his yaw, carried
    int16_t point[3];
    bonePointOffset(actor, kCarryBone, kCarryOffset, point); // exe 0x5e1970
    AnimatedActor& carried = reaverbotAt(bon.target);
    carried.pos.x = int32_t(uint32_t(int32_t(point[0])) << 16);
    carried.pos.y = int32_t(uint32_t(int32_t(point[1])) << 16);
    carried.pos.z = int32_t(uint32_t(int32_t(point[2])) << 16);
    carried.rotation.y = actor.rotation.y;
    handlingOf(carried).handling = kHandlingCarried;
}

} // namespace

void bonPartnerUpdate(const PlayerEnv& env, uint32_t actor) { bonBonne(env, animatedActorAt(actor)); }

} // namespace od::port
