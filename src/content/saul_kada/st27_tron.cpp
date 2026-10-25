// ST27 sub 8, the lava mission: Tron (pool 0x20 type 0x48 sub 1, exe 0x4cf850; hypothesis names), one routine per exe
// routine. See st27_tron.hpp.
//
// Her bytes are the partners' (content/saul_kada/st27_partners). The Reaverbots she hunts (the hopping Reaverbot,
// content/saul_kada/hopping_reaverbot) read their handling byte set here: grabbed, carried (every tick), thrown into
// the lava / at the player.
#include "content/saul_kada/st27_tron.hpp"

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

// [0x895284]
enum class TronState : uint8_t { Init = 0, Run = 1, Free = 2 };
// [0x895290]
enum class TronSub : uint8_t { WaitForMission = 0, Hunt = 1, Knocked = 2, Carry = 3 };
// [0x4d0224]
enum class HuntStep : uint8_t { Pick = 0, Walk = 1, Leap = 2, Land = 3, TurnFromWall = 4 };
enum class KnockedStep : uint8_t { Start = 0, Down = 1, Climb = 2 };
enum class CarryStep : uint8_t { Lift = 0, Carry = 1, Settle = 2, Throw = 3, Recover = 4 };
enum TronAnim : uint8_t { kAnimWalk = 0, kAnimLeap = 10, kAnimCarry = 0xb, kAnimThrow = 0xc, kAnimKnocked = 0xd };

constexpr uint32_t kFlagMissionAccepted = 0x681;
constexpr int16_t kWalkSpeed = int16_t(0xfec0);
constexpr int kFloorY = -0x200;
constexpr uint16_t kHurtSound = 0x8d, kKnockedSound = 0x22f, kAtPlayerSound = 0x230, kLiftSound = 0x231,
                   kThrowSound = 0x232;
constexpr int8_t kLeapGrabKey = 6, kLiftKey = 8, kReleaseKey = 6;
// her hit entries (bodies, not the lock-on's), the hand's deflecting the player's shots
constexpr uint32_t kPartnerShape = hitGroup(kHitGroupBodies) | kHitNoLockOn;
// where the carried Reaverbot sits: bone 3's offset
constexpr uint32_t kCarryBone = 3;
constexpr int16_t kCarryOffset[3] = {0x48, -0x50, -0x10};

// the Reaverbot of a target index (a pool-0x20 slot index) by the exe's unchecked arithmetic (kPartnerNoTarget reads
// past the pool, as in the exe)
AnimatedActor& reaverbotAt(uint8_t index) { return animatedActorAt(animatedPoolSlot(int32_t(kAnimatedClass) + index)); }
HoppingReaverbotParams handlingOf(const AnimatedActor& reaverbot) { return HoppingReaverbotParams(reaverbot); }

uint32_t pushOut(const PlayerEnv& env, AnimatedActor& actor) { return actorPushOut(env, actor); } // exe 0x410960
void move(AnimatedActor& actor, int y, int z) { integrateVelocity(actor, 0, y, z); }              // exe 0x5e3690

// the bearing from a point to the actor
int bearingFrom(const AnimatedActor& actor, const Fixed16Vec3& point) { return angleOf(point, actor.pos); }
int bearingFrom(const AnimatedActor& actor, const int32_t point[3]) {
    return angleOf(point[0], point[2], actor.pos.x, actor.pos.z);
}

// the height rule shared by exe 0x4cfdc0 step 3 / 0x4d0240 step 2 / 0x4d0340 step 1: on the low floor climb back up
// to y = -0x200 (true when there), else fall below -0x200, then mark the low floor
bool floorStep(AnimatedActor& actor, PartnerFields& tron, int up) {
    if (tron.flags & kPartnerLowFloor) {
        if (actor.pos.y.whole > kFloorY) {
            actor.velocity.y = 0;
            actor.pos.y.whole = int16_t(kFloorY);
            return true;
        }
        actor.velocity.y += uint32_t(up);
    } else if (actor.pos.y.whole > kFloorY) {
        actor.velocity.y -= 0x14;
    } else if (actor.velocity.y < 0) {
        actor.velocity.y += 0x14;
    } else {
        actor.velocity.y = 0;
        tron.flags |= kPartnerLowFloor;
    }
    return false;
}

// exe 0x4d0850: among the live Reaverbots (slots 0..15) not grabbed / carried / taken, and not marked by the other
// partner (unless it is the current target): the farthest stunned one nearer to her than to the partner (or the
// partner is busy) -> kPartnerReadyTarget; else the farthest one. kPartnerNoTarget = none
uint8_t pickReaverbot(AnimatedActor& actor, PartnerFields& tron) {
    const PartnerFields& partner = classState<PartnerFields>(*tron.partner);
    const AnimatedActor& partnerActor = *tron.partner;
    uint32_t bestReady = 0, best = 0;
    uint8_t selReady = kPartnerNoTarget, sel = kPartnerNoTarget;
    for (uint32_t i = 0; i < kPartnerScanSlots; ++i) {
        AnimatedActor& r = animatedPool()[i];
        if (r.flags == 0 || r.type != kHoppingReaverbotType || (handlingOf(r).handling & kHandlingBusy)) continue;
        const uint32_t d = uint32_t(planarDistance(actor.pos, r.pos));
        if ((handlingOf(r).handling & kHandlingStunned) && bestReady < d) {
            const uint32_t marks = enemyGroupMarks(int32_t(tron.group), EnemyGroupMarks::Either);
            if (!((1u << (i & 31)) & marks) || tron.target == i) {
                if ((partner.flags & (kPartnerReadyTarget | kPartnerCarrying)) ||
                    d < uint32_t(planarDistance(partnerActor.pos, r.pos))) {
                    bestReady = d;
                    selReady = uint8_t(i);
                }
            }
        }
        if (best < d) {
            const uint32_t marks = enemyGroupMarks(int32_t(tron.group), EnemyGroupMarks::Either);
            if (!((1u << (i & 31)) & marks) || tron.target == i) {
                best = d;
                sel = uint8_t(i);
            }
        }
    }
    if (selReady != kPartnerNoTarget) {
        tron.flags |= kPartnerReadyTarget;
        return selReady;
    }
    tron.flags &= uint8_t(~kPartnerReadyTarget);
    return sel;
}

// exe 0x4d0aa0: the lob from here to the point (target distance * 0x20) ahead, at the pitch from (target x, -0x3e0,
// target z) to her; her position is put back after the probe move
void leapToTarget(AnimatedActor& actor, PartnerFields& tron) {
    const AnimatedActor& target = reaverbotAt(tron.target);
    // the exe's stack point: its fractions are not initialised (the pitch reads the integer parts)
    const int32_t above[3] = {int32_t(uint32_t(uint16_t(target.pos.x.whole)) << 16), int32_t(0xfc200000u),
                              int32_t(uint32_t(uint16_t(target.pos.z.whole)) << 16)};
    const int32_t here[3] = {actor.pos.x, actor.pos.y, actor.pos.z};
    const int pitch = pitchOf(above, here); // exe 0x5e3ca0
    const int angle = int16_t(uint16_t(0x800 - pitch));
    const int32_t saved[3] = {actor.pos.x, actor.pos.y, actor.pos.z};
    move(actor, 0, int(uint32_t(planarDistance(actor.pos, target.pos)) * uint32_t(-0x20)));
    const int32_t landing[3] = {actor.pos.x, actor.pos.y, actor.pos.z};
    int16_t vy, vz;
    lobVelocity(saved, landing, angle, 0x14, 0xffffffffu, vy, vz); // exe 0x5e3f50
    actor.pos.x = saved[0];
    actor.pos.y = saved[1];
    actor.pos.z = saved[2];
    actor.velocity.y = int16_t(-int32_t(vy));
    actor.velocity.z = int16_t(-int32_t(vz));
}

// exe 0x4d09a0: facing the target (0x40) -> face it, the next step, the leap (false); else turn toward it and brake
bool approachTarget(const PlayerEnv& env, AnimatedActor& actor, PartnerFields& tron) {
    const AnimatedActor& target = reaverbotAt(tron.target);
    if (facingWithin(actor, target.pos, 0x40)) {
        actor.rotation.y = int16_t(bearingFrom(actor, target.pos));
        ++actor.step;
        actor.stepCounter = 0;
        actor.anim = kAnimLeap;
        leapToTarget(actor, tron);
        return false;
    }
    turnYawTo(actor.rotation.y, uint32_t(bearingFrom(actor, target.pos)), 0x20);
    const int16_t z = actor.velocity.z;
    if (z < 0) {
        actor.velocity.z = int16_t(z + 4);
        move(actor, 0, int16_t(uint16_t(z + 4)));
        pushOut(env, actor);
        return true;
    }
    actor.velocity.z = 0;
    return true;
}

// exe 0x4cfda0: waits for the player to accept the mission
void waitForMission(AnimatedActor& actor) {
    if (!storyFlag(kFlagMissionAccepted)) return; // exe 0x510e50
    actor.subState = uint8_t(TronSub::Hunt);
    actor.step = 0;
}

// a different pick replaces the target when she faces it (0x100), the target is a ready one, or the current one is
// taken
bool retarget(AnimatedActor& actor, PartnerFields& tron, uint8_t pick) {
    if (tron.target == pick) return false;
    if (facingWithin(actor, reaverbotAt(pick).pos, 0x100) || (tron.flags & kPartnerReadyTarget) ||
        (handlingOf(reaverbotAt(tron.target)).handling & kHandlingTaken)) {
        tron.target = pick;
        return true;
    }
    return false;
}

// exe 0x4cfdc0 [0x4d0224]
void hunt(const PlayerEnv& env, AnimatedActor& actor, PartnerFields& tron) {
    switch (HuntStep(actor.step)) {
    case HuntStep::Pick: { // 0x4cfddc
        const uint8_t pick = pickReaverbot(actor, tron);
        tron.target = pick;
        if (pick != kPartnerNoTarget) {
            actor.stepCounter = 0;
            actor.velocity.z = kWalkSpeed;
            ++actor.step;
        }
        break;
    }
    case HuntStep::Walk: { // 0x4cfe0a
        if (reaverbotAt(tron.target).flags == 0) {
            actor.step = uint8_t(HuntStep::Pick);
            break;
        }
        const uint8_t pick = pickReaverbot(actor, tron);
        if (pick == kPartnerNoTarget) break;
        retarget(actor, tron, pick);
        {
            const AnimatedActor& target = reaverbotAt(tron.target);
            if (planarDistance(actor.pos, target.pos) < 0x300 && (handlingOf(target).handling & kHandlingStunned)) {
                if (planarDistance(actor.pos, reaverbotAt(tron.target).pos) < 0x180) {
                    move(actor, 0, kWalkSpeed);
                    pushOut(env, actor);
                    break;
                }
                if (approachTarget(env, actor, tron)) break; // exe 0x4d09a0
            }
        }
        turnYawTo(actor.rotation.y, uint32_t(bearingFrom(actor, reaverbotAt(tron.target).pos)), 0x20);
        move(actor, 0, kWalkSpeed);
        const uint32_t result = pushOut(env, actor);
        if (result & kWallOnSides) {
            const int16_t yaw = actor.rotation.y;
            reflectRotationOffWalls(actor.rotation, result); // exe 0x410740
            tron.wallYaw = actor.rotation.y;
            actor.rotation.y = yaw;
            actor.step = uint8_t(HuntStep::TurnFromWall);
        }
        break;
    }
    case HuntStep::Leap: { // 0x4cffa0: grab it when close
        if (reaverbotAt(tron.target).flags == 0) {
            actor.step = uint8_t(HuntStep::Land);
            actor.anim = kAnimWalk;
            tron.flags &= uint8_t(~kPartnerHoldPose);
            break;
        }
        const uint8_t pick = pickReaverbot(actor, tron);
        if (pick == kPartnerNoTarget) break;
        if (retarget(actor, tron, pick)) {
            actor.step = uint8_t(HuntStep::Land);
            actor.velocity.z = kWalkSpeed;
            actor.anim = kAnimWalk;
            tron.flags &= uint8_t(~kPartnerHoldPose);
        }
        AnimatedActor& target = reaverbotAt(tron.target);
        HoppingReaverbotParams handling = handlingOf(target);
        if (handling.handling & kHandlingStunned) {
            if (int8_t(actor.keyIndex) == kLeapGrabKey) tron.flags |= kPartnerHoldPose;
            if (uint32_t(distance3(actor.pos, target.pos)) < 0x180) { // exe 0x5e3a60
                actor.subState = uint8_t(TronSub::Carry);
                actor.step = uint8_t(CarryStep::Lift);
                handling.handling = kHandlingGrabbed;
                actor.velocity.y -= 0x14;
                tron.flags &= uint8_t(~kPartnerHoldPose);
            } else {
                actor.velocity.y -= 0x14;
            }
        } else {
            actor.step = uint8_t(HuntStep::Land);
            actor.anim = kAnimWalk;
            tron.flags &= uint8_t(~kPartnerHoldPose);
        }
        turnYawTo(actor.rotation.y, uint32_t(bearingFrom(actor, reaverbotAt(tron.target).pos)), 0x20);
        move(actor, actor.velocity.y, actor.velocity.z);
        pushOut(env, actor);
        break;
    }
    case HuntStep::Land: // 0x4d0138: missed: land, back to the walk on the upper floor
        tron.target = pickReaverbot(actor, tron);
        actor.velocity.y += 8;
        if (actor.velocity.z < kWalkSpeed) actor.velocity.z += 4;
        else actor.velocity.z = kWalkSpeed;
        move(actor, actor.velocity.y, actor.velocity.z);
        pushOut(env, actor);
        if (floorStep(actor, tron, 4)) actor.step = uint8_t(HuntStep::Walk);
        break;
    case HuntStep::TurnFromWall: // 0x4d01e5: turn to the reflected yaw, then pick again
        if (turnYawTo(actor.rotation.y, uint32_t(int32_t(tron.wallYaw)), 0x20) != 0) break;
        actor.step = uint8_t(HuntStep::Pick);
        break;
    default: break;
    }
    enemyGroupMark(int32_t(tron.group), uint16_t(1u << (tron.target & 31))); // exe 0x5e6d40
}

// exe 0x4d0240: knocked back, then back up to the floor
void knockedBack(const PlayerEnv& env, AnimatedActor& actor, PartnerFields& tron) {
    switch (KnockedStep(actor.step)) {
    case KnockedStep::Start:
        actor.anim = kAnimKnocked;
        actor.step = uint8_t(KnockedStep::Down);
        break;
    case KnockedStep::Down: {
        if (!actor.animEnded()) return;
        uint8_t flags = uint8_t(tron.flags & ~kPartnerKnocked);
        tron.flags = flags;
        const int16_t y = actor.pos.y.whole;
        if (y == kFloorY) {
            actor.subState = uint8_t(TronSub::Hunt);
            actor.step = uint8_t(HuntStep::Pick);
            actor.anim = kAnimWalk;
            return;
        }
        flags = y < kFloorY ? uint8_t(flags | kPartnerLowFloor) : uint8_t(flags & ~kPartnerLowFloor);
        tron.flags = flags;
        actor.anim = kAnimWalk;
        ++actor.step;
        break;
    }
    case KnockedStep::Climb:
        if (floorStep(actor, tron, 4)) {
            actor.subState = uint8_t(TronSub::Hunt);
            actor.step = uint8_t(HuntStep::Pick);
        }
        move(actor, actor.velocity.y, actor.velocity.z);
        pushOut(env, actor);
        break;
    default: break;
    }
}

// exe 0x4d0340: lift (from key 8 it is carried), walk to the throw point (the lava, or the player with
// kPartnerThrowAtPlayer), turn, throw (released at key 6)
void carryAndThrow(const PlayerEnv& env, AnimatedActor& actor, PartnerFields& tron) {
    const uint8_t flags0 = tron.flags;
    int32_t goal[3];
    if (flags0 & kPartnerThrowAtPlayer) {
        goal[0] = player().pos.x;
        goal[1] = player().pos.y;
        goal[2] = player().pos.z;
    } else {
        goal[0] = kLavaX;
        goal[1] = kLavaY;
        goal[2] = kLavaZ;
    }
    switch (CarryStep(actor.step)) {
    case CarryStep::Lift: // 0x4d03a9
        if (actor.animEnded()) {
            tron.flags = uint8_t(flags0 | kPartnerCarrying);
            ++actor.step;
            actor.anim = kAnimCarry;
            actor.stepCounter = 0;
            tron.carryYaw = actor.rotation.y;
        } else {
            if (int8_t(actor.keyIndex) == kLiftKey) soundPlayAtPos(kLiftSound, actor.pos); // exe 0x5d5380
            if (int8_t(actor.keyIndex) >= kLiftKey) tron.flags |= kPartnerCarrying;
        }
        actor.velocity.y -= 0x14;
        move(actor, actor.velocity.y, actor.velocity.z);
        pushOut(env, actor);
        pushOut(env, actor);
        return;
    case CarryStep::Carry: { // 0x4d043c: toward the throw point along the carry heading
        floorStep(actor, tron, 4);
        int16_t yaw = actor.rotation.y;
        actor.rotation.y = tron.carryYaw;
        if (facingWithin(actor, goal[0], goal[2], 0x200)) {
            actor.rotation.y = yaw;
            turnYawTo(actor.rotation.y, uint32_t(bearingFrom(actor, goal)), 0x20);
            if (actor.velocity.z < kWalkSpeed) actor.velocity.z += 0x10;
            else actor.velocity.z = kWalkSpeed;
            move(actor, actor.velocity.y, actor.velocity.z);
            if (!facingWithin(actor, goal[0], goal[2], 0x40)) break;
            const int reach = (tron.flags & kPartnerThrowAtPlayer) ? 0x800 : 0x300;
            if (planarDistance(actor.pos, goal) >= reach) break;
            if (actor.pos.y.whole < kFloorY) tron.flags |= kPartnerLowFloor;
            else tron.flags &= uint8_t(~kPartnerLowFloor);
            ++actor.step;
            pushOut(env, actor);
            return;
        }
        turnYawTo(actor.rotation.y, uint32_t(bearingFrom(actor, goal)), 0x20);
        const int diff = int(actor.rotation.y) - int(tron.carryYaw);
        tron.carryYaw = actor.rotation.y;
        if (actor.stepCounter != 0) {
            yaw = int16_t(uint16_t(yaw) + uint16_t(diff));
            actor.rotation.y = tron.carryYaw;
        } else {
            actor.rotation.y = yaw;
            if (facingWithin(actor, goal[0], goal[2], 0x200)) {
                ++actor.stepCounter;
                actor.rotation.y = tron.carryYaw;
            } else {
                turnYawTo(actor.rotation.y, uint32_t(bearingFrom(actor, goal)), 0x40);
                yaw = actor.rotation.y;
                actor.rotation.y = tron.carryYaw;
            }
        }
        if (actor.velocity.z < kWalkSpeed) actor.velocity.z += 0x10;
        else actor.velocity.z = kWalkSpeed;
        move(actor, actor.velocity.y, actor.velocity.z);
        actor.rotation.y = yaw;
        pushOut(env, actor);
        return;
    }
    case CarryStep::Settle: // 0x4d0680: settle at y = -0x200, then face the throw point
        if (flags0 & kPartnerLowFloor) {
            if (actor.pos.y.whole >= kFloorY) {
                actor.velocity.y = 0;
                actor.pos.y.whole = int16_t(kFloorY);
                tron.flags &= uint8_t(~kPartnerLowFloor);
            } else {
                actor.velocity.y += 4;
            }
        } else if (actor.pos.y.whole > kFloorY) {
            actor.velocity.y -= 0x14;
        } else {
            actor.velocity.y = 0;
            tron.flags |= kPartnerLowFloor;
        }
        if (actor.pos.y.whole != kFloorY) {
            move(actor, actor.velocity.y, 0);
            pushOut(env, actor);
            return;
        }
        if (facingWithin(actor, goal[0], goal[2], 0x20)) {
            actor.rotation.y = int16_t(bearingFrom(actor, goal));
            ++actor.step;
            actor.anim = kAnimThrow;
            tron.flags &= uint8_t(~kPartnerLowFloor);
            pushOut(env, actor);
            return;
        }
        turnYawTo(actor.rotation.y, uint32_t(bearingFrom(actor, goal)), 0x20);
        pushOut(env, actor);
        return;
    case CarryStep::Throw: // 0x4d077f: released at key 6
        if (int8_t(actor.keyIndex) == kReleaseKey) {
            HoppingReaverbotParams handling = handlingOf(reaverbotAt(tron.target));
            if (flags0 & kPartnerThrowAtPlayer) {
                handling.handling = kHandlingThrown | kHandlingAtPlayer;
                soundPlayAtPos(kAtPlayerSound, actor.pos);
            } else {
                handling.handling = kHandlingThrown;
            }
            soundPlayAtPos(kThrowSound, actor.pos);
            tron.flags &= uint8_t(~kPartnerCarrying);
            ++actor.step;
            pushOut(env, actor);
            return;
        }
        break;
    case CarryStep::Recover: // 0x4d080e: back to the hunt when the animation ends
        if (actor.animEnded()) {
            actor.anim = kAnimWalk;
            actor.subState = uint8_t(TronSub::Hunt);
            actor.step = uint8_t(HuntStep::Pick);
        }
        break;
    default: break;
    }
    pushOut(env, actor);
}

// exe 0x4cf9d0
void init(AnimatedActor& actor, PartnerFields& tron) {
    if (!bindActorModel(actor, 0)) { // exe 0x5e0ba0
        freeActor(actor);            // exe 0x5e1490
        return;
    }
    actor.flags |= kActorDrawn;
    actor.anim = kAnimWalk;
    tron.animKey = 0;
    actor.prevAnim = kNoAnimShown;
    const uint16_t hp = uint16_t(animatedStats(actor.type, actor.subtype).hitPoints * 4u);
    actor.collisionBox.pointAt(TableId::St27TronBox);
    actor.interactionBox = nullptr;
    actor.interaction = 0;
    actor.maxHitPoints = int16_t(hp);
    actor.hitPoints = int16_t(hp);
    actor.velocity.clearWords();
    actor.rotation.z = 0;
    actor.rotation.x = 0;
    actor.airborne = 1;
    actor.onPlatform = 0;
    actor.platform = nullptr;
    actor.actorHit = 0;
    tron.bodyBox.result = 0;
    tron.handBox.result = 0;
    tron.flags = 0;
    actor.type = kHoppingReaverbotType; // the enemy-group lookup runs under the Reaverbots' type
    tron.group = uint8_t(uint32_t(enemyGroupFindFor(actor, 0)) & 0xff); // exe 0x5e6cd0(a, 0)
    actor.subState = uint8_t(TronSub::WaitForMission);
    actor.step = 0;
    actor.stepCounter = 0;
    actor.type = kPartnerTronType;
    actor.state = uint8_t(TronState::Run);
}

// one hit-list entry at the bone's offset
void submitHit(const AnimatedActor& actor, const AnimatedActorStats& stats, uint32_t bone, int16_t x, int16_t y,
               int16_t z, HitBoxHead& box, uint32_t shape, uint32_t attack) {
    const int16_t v[3] = {x, y, z};
    int16_t point[3];
    bonePointOffset(actor, bone, v, point); // exe 0x5e1970
    hitListSubmit(point, box, hitOwner(actor.slot) | hitHeightLevel(stats.hitHeight) | shape,
                  stats.attack | attack); // exe 0x5e4120
}
// exe 0x4cfaa0: the hits, the sub-state, the hit-list entries
void run(const PlayerEnv& env, AnimatedActor& actor, PartnerFields& tron) {
    if (actor.hitPoints <= 0) tron.flags |= kPartnerThrowAtPlayer;
    if ((tron.bodyBox.result & kHitAttackByPlayer) && !(tron.flags & kPartnerKnocked)) {
        actor.tint = kTintFlash;
        soundPlayAtPos(kHurtSound, actor.pos); // exe 0x5d5380
        if (hitPointsAdd(actor, -int32_t(tron.bodyBox.result & kHitDamageMask)) == 0) { // exe 0x5e52f0: still alive
            if (TronSub(actor.subState) == TronSub::Carry) // the carried one dropped
                handlingOf(reaverbotAt(tron.target)).handling &= uint8_t(~kHandlingHeld);
            tron.flags = uint8_t((tron.flags & ~(kPartnerHoldPose | kPartnerCarrying | kPartnerLowFloor)) | kPartnerKnocked);
            actor.subState = uint8_t(TronSub::Knocked);
            actor.step = uint8_t(KnockedStep::Start);
            soundPlayPanned(kKnockedSound, 0, 0); // exe 0x5d5340
        }
    }
    switch (TronSub(actor.subState)) { // [0x895290]
    case TronSub::WaitForMission: waitForMission(actor); break;
    case TronSub::Hunt: hunt(env, actor, tron); break;
    case TronSub::Knocked: knockedBack(env, actor, tron); break;
    case TronSub::Carry: carryAndThrow(env, actor, tron); break;
    default: notPortedAt(kStoryPort, "0x4cfaa0 sub-state", actor.subState);
    }
    tron.handBox.previousX = actor.prevPos.x.whole;
    tron.bodyBox.previousX = actor.prevPos.x.whole;
    tron.handBox.previousZ = actor.prevPos.z.whole;
    tron.bodyBox.previousZ = actor.prevPos.z.whole;
    tron.handBox.result = 0;
    tron.bodyBox.result = 0;
    const AnimatedActorStats stats = animatedStats(actor.type, actor.subtype);
    submitHit(actor, stats, 1, 0, 0x20, -0xe0, tron.bodyBox, kPartnerShape | hitRadius(0x64), kHitAttackSolid);
    submitHit(actor, stats, 0, 0x40, -0x40, 0, tron.handBox, kPartnerShape | hitRadius(0x3c), kHitAttackDeflects);
    submitHit(actor, stats, 0, -0x40, -0x40, 0, tron.handBox, kPartnerShape | hitRadius(0x3c), kHitAttackDeflects);
    submitHit(actor, stats, 0, 0, -0x40, 0x40, tron.handBox, kPartnerShape | hitRadius(0x3c), kHitAttackDeflects);
    submitHit(actor, stats, 0, 0, -0x40, -0x58, tron.handBox, kPartnerShape | hitRadius(0x64), kHitAttackDeflects);
    submitHit(actor, stats, 7, 0, -0x40, 0, tron.handBox, kPartnerShape | hitRadius(0xa0), kHitAttackDeflects);
}

// exe 0x4cf850
void tron(const PlayerEnv& env, AnimatedActor& actor) {
    PartnerFields& tron = classState<PartnerFields>(actor);
    actor.prevAnim = actor.anim;
    actor.tint = kTintNeutral;
    actor.savePreviousPose();
    actor.savePreviousContact();
    switch (TronState(actor.state)) { // [0x895284]
    case TronState::Init: init(actor, tron); break;
    case TronState::Run: run(env, actor, tron); break;
    case TronState::Free: freeActor(actor); return; // 0x468fa0 (the exe still runs the animation tail on the freed actor)
    default: notPortedAt(kStoryPort, "0x4cf850 state", actor.state);
    }
    if (actor.anim != actor.prevAnim) animSet(actor, actor.anim, tron.animKey); // exe 0x5e1c50
    else if (tron.flags & kPartnerHoldPose) animDecodeCurrent(actor);          // exe 0x5e1f30
    else animStep(actor);                                                       // exe 0x5e1ea0
    animPose(actor);                                                            // exe 0x5e2d90
    if (!(tron.flags & kPartnerCarrying)) return;
    int16_t point[3];
    bonePointOffset(actor, kCarryBone, kCarryOffset, point); // exe 0x5e1970
    AnimatedActor& carried = reaverbotAt(tron.target);
    carried.pos.x = int32_t(uint32_t(int32_t(point[0])) << 16);
    carried.pos.y = int32_t(uint32_t(int32_t(point[1])) << 16);
    carried.pos.z = int32_t(uint32_t(int32_t(point[2])) << 16);
    carried.rotation.y = actor.rotation.y;
    handlingOf(carried).handling = kHandlingCarried;
}

} // namespace

void tronPartnerUpdate(const PlayerEnv& env, uint32_t actor) { tron(env, animatedActorAt(actor)); }

} // namespace od::port
