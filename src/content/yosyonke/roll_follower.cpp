// Roll as Megaman's companion in the chapter after the crash landing (story 0, ST08 / ST09 / ST0D; see
// roll_follower.hpp). Bit-exact against tests/data/story_*.bin; The shadows (exe 0x5f20f0) are render output and
// left out.
#include "content/yosyonke/roll_follower.hpp"
#include "content/yosyonke/roll_follower_state.hpp"

#include "engine/data_source.hpp"
#include "game/actor_state.hpp"
#include "game/cutscene_actor.hpp"
#include "game/actor_animation.hpp"
#include "game/actor_collision.hpp"
#include "game/actor_model.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_stats.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/carry.hpp"
#include "game/distances.hpp"
#include "game/enemy_neighbours.hpp"
#include "game/event.hpp"
#include "game/event_state.hpp"
#include "game/hit_list.hpp"
#include "game/hit_points.hpp"
#include "game/line_of_sight.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/rewards.hpp"
#include "game/story_flags.hpp"
#include "game/walker_collision.hpp"

namespace od::port {

using namespace roll_follower_state;

namespace {

constexpr const char* kRollPort = "town story port"; // the area of its port-gap errors

// ==== what the three classes share ===================================================================================
// The head the followers turn (kept in the spare lanes of the velocity / the acceleration)
constexpr size_t kHeadEntry = 2; // the decoded frame's entry of the head part (part 1)
constexpr int16_t kHeadYawLimit = 0x300, kHeadPitchLimit = 0x200;
constexpr int kMaxDrop = 0x20;
constexpr uint8_t kSightBlockedBit = 0x01; // the probe's result: blocked (kSightBlocked's low bit)
// the hit-list words of the followers' entries
constexpr uint32_t kHarmlessSolid = kHitAttackHarmless | kHitAttackSolid;
constexpr uint32_t kHarmlessSolidBump = kHitAttackHarmless | kHitAttackSolid | kHitAttackBump;

// the sub-state s at its step 0
void setSub(AnimatedActor& a, uint8_t s) {
    a.subState = s;
    a.restartStep();
}
template <class Sub> void setSub(AnimatedActor& a, Sub s) { setSub(a, uint8_t(s)); }
// the talk event at the actor (exe 0x543af0(a, 0, 0, message))
void talk(AnimatedActor& a, uint16_t message) { eventStartAtActor(a, 0, 0, message); }
void moveForward(AnimatedActor& a, int z) { moveAlongYaw(a.pos, a.rotation.y, 0, 0, z); }
// a point given by its integer x / z (the exe's stack 16.16 point: the low words 0)
int32_t wholeToFixed(int16_t v) { return int32_t(uint32_t(uint16_t(v)) << 16); }
// the direction of the point seen from the actor
int angleFromPoint(const AnimatedActor& a, int16_t x, int16_t z) {
    return angleOf(wholeToFixed(x), wholeToFixed(z), a.pos.x, a.pos.z);
}
// the planar distance of the point from the actor
int distanceFromPoint(const AnimatedActor& a, int16_t x, int16_t z) {
    const int32_t p[3] = {wholeToFixed(x), 0, wholeToFixed(z)};
    return planarDistance(a.pos, p);
}
int distanceFromPlayer(const AnimatedActor& a) { return planarDistance(player().pos, a.pos); }
// an angle difference wrapped into -0x800 .. 0x800 (the exe's inline form)
int wrappedDiff(int d) {
    if (uint32_t(d + kHalfTurn) > 0x1000u) d = int((uint32_t(-d) ^ 0xfffu) + 1u);
    return d;
}
// the yaw against the player's direction from the actor
int yawFromPlayerDiff(const AnimatedActor& a) {
    return wrappedDiff(int(uint16_t(a.rotation.y) & kAngleMask) - (angleOf(player().pos, a.pos) & kAngleMask));
}
// the hit points of the type's row (0 without one), as the exe reads the animated table
uint16_t rowHitPoints(const AnimatedActor& a) { return hasAnimatedStats(a.type) ? animatedStats(a.type).hitPoints : 0; }
// the velocity and the acceleration cleared (the exe's four dword stores +0x38 .. +0x44)
void clearMotionWords(AnimatedActor& a) {
    a.velocity.clearWords();
    a.acceleration.clearWords();
}
// the speaker's animation the message asks for (consumed); none: the animation back to `idle` once it holds its frame
void followSpeakerAnimation(AnimatedActor& a, uint8_t idle) {
    EventState& ev = eventState();
    if (ev.speakerAnimation != kNoSpeakerAnimation) {
        a.anim = ev.speakerAnimation;
        a.prevAnim = kNoAnimShown;
        ev.speakerAnimation = kNoSpeakerAnimation;
    } else if (a.animKey.flags == kKeyHold) {
        a.anim = idle;
    }
}

// a head lane relaxing toward 0 by `step` (two stores when it crosses 0, as the exe)
void relaxHead(int16_t& lane, int step) {
    const int16_t v = lane;
    if (v < 0) {
        lane = int16_t(uint16_t(v + step));
        if (int16_t(v + step) > 0) lane = 0;
    } else {
        lane = int16_t(uint16_t(v - step));
        if (int16_t(v - step) < 0) lane = 0;
    }
}
// the head lane turned by `step` toward the side d says, clamped at +-limit (two stores at the clamp, as the exe)
void turnHead(int16_t& lane, int d, int dead, int step, int16_t limit) {
    const int16_t h = lane;
    if (d < -dead) {
        lane = int16_t(uint16_t(h + step));
        if (int16_t(h + step) > limit) lane = limit;
    } else if (d > dead) {
        lane = int16_t(uint16_t(h - step));
        if (int16_t(h - step) < -limit) lane = int16_t(-limit);
    }
}
// the player's direction from the actor (its 16.16 differences)
uint32_t playerDirection(const AnimatedActor& a) {
    const Player& p = player();
    return uint32_t(atanAngle(float(int32_t(uint32_t(a.pos.x) - uint32_t(p.pos.x))),
                              float(int32_t(uint32_t(a.pos.z) - uint32_t(p.pos.z))))) &
           kAngleMask;
}
// the head lanes added to the head part's decoded angles (before the pose)
void applyHead(const int16_t& pitch, const int16_t& yaw) {
    LaneVec4 head = decodedFrame().entries[kHeadEntry];
    head.x = int16_t(uint16_t(uint16_t(pitch) + uint16_t(head.x)));
    head.y = int16_t(uint16_t(uint16_t(head.y) + uint16_t(yaw)));
}

// the behaviour bits (+0x168; the walls reflect kWalkerBounces)
constexpr uint8_t kRollNoHitEntry = 0x01;
constexpr uint8_t kRollNoHurt = 0x02;      // the field: the hits are not taken
constexpr uint8_t kRollNoTalk = 0x04;
constexpr uint8_t kRollNoReactions = 0x08; // no bump / no fall
constexpr uint8_t kRollAttackEntry = 0x10; // her hit entry attacks (with the body's damage)
constexpr uint8_t kRollNoHeadLook = 0x20;
constexpr uint8_t kRollNoLift = 0x80;      // the field: the player cannot lift her
enum class RollState : uint8_t { Init = 0, Main = 1, Lifted = 2, Fainted = 3 };

// the head turn toward the player of the followers (inline in exe 0x445b00 / 0x4e4c20); relaxed while not looking
void followerHeadLook(AnimatedActor& a, RollFields& f) {
    if (f.behaviour & kRollNoHeadLook) {
        relaxHead(f.headYaw, 0x40);
        relaxHead(f.headPitch, 0x40);
        return;
    }
    const uint32_t ang = playerDirection(a);
    const uint16_t h = uint16_t(f.headYaw);
    const int d = wrappedDiff(int((uint32_t(uint16_t(a.rotation.y)) + h) & kAngleMask) - int(ang & kAngleMask));
    turnHead(f.headYaw, d, 0x20, 0x40, kHeadYawLimit);
    const int16_t p = f.headPitch;
    const int e = ((int(a.pos.y.whole) - int(player().pos.y.whole)) >> 1) + p;
    turnHead(f.headPitch, e, 0x20, 0x40, kHeadPitchLimit);
}
// bone point 0 raised by dy (written into the bone block), a line-of-sight probe from it toward the player
void rollProbe(AnimatedActor& a, RollFields& f, int dy) {
    ShortVec4& point = bonePoint(a, 0, kRollPort);
    point.y += uint32_t(dy);
    lineOfSightSpawn(a, 2, point, 0x400040, 0, f.sight, 8);
}
// once the probe is done: the point to walk to (the last sighting, unless blocked), the new sighting, the next probe
void rollSighting(AnimatedActor& a, RollFields& f, int dy) {
    const uint8_t los = f.sight;
    if (!(los & kSightDone)) return;
    if (!(los & kSightBlockedBit)) {
        f.targetX = f.seenX;
        f.targetZ = f.seenZ;
    }
    f.seenX = player().pos.x.whole;
    f.seenZ = player().pos.z.whole;
    rollProbe(a, f, dy);
}
// the hit-list box cleared with the previous position (before the entry)
void resetBox(AnimatedActor& a, HitBoxHead& box) {
    box.result = 0;
    box.previousX = a.prevPos.x.whole;
    box.previousZ = a.prevPos.z.whole;
}

// ==== Roll the follower (exe 0x445b00, pool 0x20 type 0 sub 1 of the ST08 / ST09 tables) =============================
// States (the exe's table 0x88e21c): 0 exe 0x445d70 (the model, the boxes 0x88e200 / 0x88e210, the hit points of the
// type's row, the first probe), 1 exe 0x445ec0 -> the sub-states (the exe's table 0x88e224).
enum class FollowerSub : uint8_t {
    Idle = 0,   // exe 0x446010: the player farther than 0x200 -> Walk
    Walk = 1,   // exe 0x446060: to the sighting point (-0x80)
    Run = 2,    // exe 0x446170: (-0x180)
    Search = 3, // exe 0x446270: random headings near the player's direction, until a probe sees him
    Talk = 4,   // exe 0x4463b0
    Fall = 5,   // exe 0x446430
    Bumped = 6, // exe 0x446490: turns away
};
const DataTable<CollisionBoxRecord> followerCollisionBox{TableId::RollFollowerCollisionBox};
const DataTable<InteractionBoxRecord> followerTalkBox{TableId::RollFollowerTalkBox};
constexpr uint8_t kBumpCoolDown = 0x40;

// exe 0x4466b0: an actor bumped her (the actor collision's result): its sides into the hits; a side hit marks the sight
// blocked and, off the cool-down, starts the bump (1)
int followerBumped(AnimatedActor& a, RollFields& f) {
    const uint32_t r = a.actorHit;
    if (r != 0) {
        if (r & (kSolidOnZPlus | kSolidOnZMinus)) f.hits |= kWalkHitSidesLow;
        if (r & (kSolidOnXPlus | kSolidOnXMinus)) f.hits |= kWalkHitSidesHigh;
        if (r & kSolidAboveOrBelow) f.hits |= kWalkLanded;
        if (f.hits & kWalkHitSides) {
            f.sight |= kSightBlockedBit;
            if (f.coolDown == 0) {
                f.coolDown = kBumpCoolDown;
                return 1;
            }
        }
    }
    --f.coolDown;
    return 0;
}
// exe 0x445d70
void followerInit(const PlayerEnv& env, AnimatedActor& a, RollFields& f) {
    a.flags |= kActorDrawn;
    if (!bindActorModel(a, a.variant)) {
        freeActor(a);
        return;
    }
    a.rotation.x = 0;
    a.rotation.z = 0;
    clearMotionWords(a);
    resetModelScale(a);
    a.anim = 1;
    a.prevAnim = kNoAnimShown;
    a.runFlags |= kActorRunMarked;
    const uint32_t hp = rowHitPoints(a);
    a.maxHitPoints = int16_t(hp);
    a.hitPoints = int16_t(hp << 4);
    a.collisionBox.pointAt(followerCollisionBox.stateAddress());
    a.interactionBox.pointAt(followerTalkBox.stateAddress());
    a.interaction = kInteractTalk;
    a.airborne = 0;
    a.onPlatform = 0;
    a.actorHit = 0;
    a.lastMover = nullptr;
    a.clearClassBlock();
    f.box.previousX = a.pos.x.whole;
    f.box.previousZ = a.pos.z.whole;
    f.box.result = 0;
    f.targetX = a.pos.x.whole;
    f.targetZ = a.pos.z.whole;
    f.seenX = player().pos.x.whole;
    f.seenZ = player().pos.z.whole;
    f.coolDown = 0;
    f.headPitch = 0;
    f.headYaw = 0;
    if (eventState().active) a.enterState(uint8_t(RollState::Main), uint8_t(FollowerSub::Talk));
    else a.enterState(uint8_t(RollState::Main));
    a.pos.y.whole = int16_t(actorGround(env, a));
    rollProbe(a, f, -0x100);
}
void followerSub(AnimatedActor& a, RollFields& f) {
    switch (FollowerSub(a.subState)) {
    case FollowerSub::Idle:
        if (a.step == 0) {
            f.behaviour = 0;
            a.anim = 0;
            a.enterStep(1);
        }
        if (distanceFromPlayer(a) > 0x200) setSub(a, FollowerSub::Walk);
        return;
    case FollowerSub::Walk: {
        const uint8_t st = a.step;
        if (st == 0) {
            f.behaviour = 0;
            a.anim = 1;
            a.enterStep(1);
            a.velocity.z = -0x80;
        }
        if (st <= 1) {
            turnYawTo(a.rotation.y, uint32_t(angleFromPoint(a, f.targetX, f.targetZ)), 0x40);
            moveForward(a, a.velocity.z);
        }
        const int dp = distanceFromPlayer(a);
        if (dp < 0x100) {
            setSub(a, FollowerSub::Idle);
            return;
        }
        if (distanceFromPoint(a, f.targetX, f.targetZ) < 0x80) {
            setSub(a, FollowerSub::Search);
            return;
        }
        if (f.hits & kWalkHitSides) {
            if (f.sight & kSightBlockedBit) setSub(a, FollowerSub::Search);
            return;
        }
        if (dp > 0x400) setSub(a, FollowerSub::Run);
        return;
    }
    case FollowerSub::Run: {
        const uint8_t st = a.step;
        if (st == 0) {
            f.behaviour = 0;
            a.anim = 2;
            a.velocity.z = -0x180;
        }
        if (st <= 1) {
            const int turn = player().velocity.z != 0 ? 0x30 : 0x60;
            turnYawTo(a.rotation.y, uint32_t(angleFromPoint(a, f.targetX, f.targetZ)), turn);
            moveForward(a, a.velocity.z);
        }
        if (distanceFromPlayer(a) < 0x100) {
            setSub(a, FollowerSub::Idle);
            return;
        }
        if (distanceFromPoint(a, f.targetX, f.targetZ) < 0x80) {
            setSub(a, FollowerSub::Walk);
            return;
        }
        if ((f.hits & kWalkHitSides) && (f.sight & kSightBlockedBit)) setSub(a, FollowerSub::Search);
        return;
    }
    case FollowerSub::Search: {
        const uint8_t st = a.step;
        bool pick = false, count = false;
        if (st == 0) {
            f.behaviour = kWalkerBounces | kRollNoHeadLook;
            a.anim = 1;
            a.velocity.z = -0x80;
            a.step = 1;
            pick = true;
        } else if (st == 1) {
            pick = true;
        } else if (st == 2) {
            count = true;
        }
        if (pick) {
            a.step = 2;
            f.timer = 0x20;
            const int ang = angleOf(player().pos, a.pos);
            f.searchYaw = int16_t(ang);
            const int d = wrappedDiff(int(uint16_t(a.rotation.y) & kAngleMask) - (ang & kAngleMask));
            const uint32_t r = rngStep() & 0x3ff;
            if (d < 0) f.searchYaw = int16_t(uint32_t(uint16_t(f.searchYaw)) - r);
            else f.searchYaw = int16_t(uint32_t(uint16_t(f.searchYaw)) + r);
            count = true;
        }
        if (count) {
            --f.timer;
            if (f.timer == 0) a.step = 1;
        }
        turnYawTo(a.rotation.y, uint32_t(int32_t(f.searchYaw)), 0x40);
        moveForward(a, a.velocity.z);
        if (f.hits & kWalkHitSides) {
            f.searchYaw = a.rotation.y;
            f.timer = 0x20;
        }
        if ((f.sight & kSightDone) && !(f.sight & kSightBlockedBit)) setSub(a, FollowerSub::Run);
        return;
    }
    case FollowerSub::Talk: { // the message script's animation requests through the event's speaker byte
        const uint8_t st = a.step;
        if (st == 0) {
            f.behaviour = kRollNoHeadLook | kRollNoTalk | kRollNoHurt;
            a.anim = 0;
            eventState().speakerAnimation = kNoSpeakerAnimation;
            a.enterStep(1);
        } else if (st != 1) {
            if (st == 2 && a.animEnded()) setSub(a, FollowerSub::Idle);
            return;
        }
        if (eventState().speakerAnimation == kNoSpeakerAnimation) {
            if (a.animKey.flags == kKeyHold) a.anim = 0;
        } else {
            a.anim = eventState().speakerAnimation;
            a.prevAnim = kNoAnimShown;
            eventState().speakerAnimation = kNoSpeakerAnimation;
        }
        if (eventState().active == 0) a.step = 2;
        return;
    }
    case FollowerSub::Fall: {
        const uint8_t st = a.step;
        if (st == 0) {
            f.behaviour = kWalkerBounces | kRollNoReactions | kRollNoTalk | kRollNoHitEntry;
            a.step = 1;
            a.acceleration.y = 0x40;
            resetModelScale(a);
        } else if (st != 1) {
            return;
        }
        if (walkerFallStep(a, f.hits, a.rotation.y)) { // exe 0x4844b0
            a.airborne = 0;
            a.velocity.y = 0;
            setSub(a, FollowerSub::Walk);
        }
        return;
    }
    case FollowerSub::Bumped: { // turns away until the cool-down is over
        const uint8_t st = a.step;
        if (st == 0) {
            f.behaviour = kRollNoHeadLook;
            a.anim = 1;
            a.velocity.z = -0x80;
            const int d = yawFromPlayerDiff(a);
            f.timer = d < 1 ? int16_t(-0x20) : int16_t(0x20);
        } else if (st != 1) {
            return;
        }
        if (f.coolDown == 0) setSub(a, FollowerSub::Search);
        a.rotation.y += uint16_t(f.timer);
        moveForward(a, a.velocity.z);
        return;
    }
    default: notPortedAt(kRollPort, "0x445b00 sub-state", a.subState);
    }
}
// exe 0x445ec0
void followerMain(const PlayerEnv& env, AnimatedActor& a, RollFields& f) {
    if (followerBumped(a, f) && !(f.behaviour & kRollNoReactions)) setSub(a, FollowerSub::Bumped);
    a.prevAnim = a.anim;
    a.prevPos = a.pos;
    a.tint = kTintNeutral;
    a.prevRotation = a.rotation;
    a.savePreviousContact();
    followerSub(a, f);
    if (walkerCollide(env, a, f.hits, f.behaviour, true, kMaxDrop)) { // exe 0x446580
        a.airborne = kAirborneFalling;
        if (!(f.behaviour & kRollNoReactions)) setSub(a, FollowerSub::Fall);
    }
    if (!(f.behaviour & kRollNoTalk) && player().talksTo(a) && eventState().active == 0) {
        talk(a, uint16_t(int16_t(f.message.get())));
        setSub(a, FollowerSub::Talk);
    }
    rollSighting(a, f, -0x40);
}
// exe 0x445b00
void follower(const PlayerEnv& env, AnimatedActor& a) {
    RollFields& f = classState<RollFields>(a);
    f.kind = 0;
    switch (RollState(a.state)) {
    case RollState::Init: followerInit(env, a, f); break;
    case RollState::Main: followerMain(env, a, f); break;
    default: notPortedAt(kRollPort, "0x445b00 state", a.state);
    }
    if (!(a.flags & kActorLive)) return;
    if (a.anim == a.prevAnim) animStep(a);
    else animSet(a, a.anim, 0);
    followerHeadLook(a, f);
    applyHead(f.headPitch, f.headYaw);
    animPose(a);
    const int8_t b = int8_t(f.behaviour);
    resetBox(a, f.box);
    if (!(b & kRollNoHitEntry)) {
        const ShortVec4& bone = bonePoint(a, 0, kRollPort);
        const int16_t pos[3] = {bone.x, bone.y, bone.z};
        if (!(b & kRollAttackEntry))
            hitListSubmit(pos, f.box,
                          hitOwner(a.slot) | hitGroup(kHitGroupBodies) | kHitContactMotion | hitRadius(0x40),
                          kHitAttackHarmless);
        else
            hitListSubmit(pos, f.box,
                          hitOwner(a.slot) | hitGroup(kHitGroupBodies) | kHitContactMotion | hitRadius(0x50),
                          animatedStats(a.type).bodyAttack | kHarmlessSolid);
    }
    // exe 0x5f20f0(actor, 0x30, 0x160, 0): the shadow (render output)
    actorCollisionSubmitSolid(a, SolidType::PushOut, kNoBone, 0);
}

constexpr uint8_t kShedNoHitEntry = 0x01, kShedTalking = 0x02, kShedNoTalk = 0x04, kShedNoFall = 0x08; // 0x02: set
// while she talks (as the followers' kRollNoHurt; this class does not read it)
constexpr uint8_t kShedLookAside = 0x10; // the head looks 0x180 to the side and 0xc0 lower
constexpr uint8_t kShedNoHeadLook = 0x20;
constexpr uint8_t kShedNoLockOn = 0x80;  // her entry is never the lock-on's target
enum class ShedSub : uint8_t {
    Idle = 0, // exe 0x443770
    Talk = 1, // exe 0x443790
    Fall = 2, // exe 0x443860
};
constexpr uint32_t kFlagShedLookAside = 0x681, kFlagShedLookOff = 0x682;
const DataTable<CollisionBoxRecord> shedCollisionBox{TableId::RollFollowerShedCollisionBox};
const DataTable<InteractionBoxRecord> shedTalkBox{TableId::RollFollowerShedTalkBox};
constexpr uint16_t kLandSound = 0xc4, kStepSound = 0xa5;
constexpr uint8_t kStepEvent = 0x80; // the animation key's footfall

// exe 0x443690
void shedMain(const PlayerEnv& env, AnimatedActor& a, ShedRollFields& f) {
    const uint32_t r68 = a.actorHit; // exe 0x443a60
    if (r68) {
        if (r68 & (kSolidOnZPlus | kSolidOnZMinus)) f.hits |= kWalkHitSidesLow;
        if (r68 & (kSolidOnXPlus | kSolidOnXMinus)) f.hits |= kWalkHitSidesHigh;
        if (r68 & kSolidAboveOrBelow) f.hits |= kWalkLanded;
    }
    a.prevAnim = a.anim;
    a.tint = kTintNeutral;
    a.savePreviousPose();
    a.savePreviousContact();
    switch (ShedSub(a.subState)) {
    case ShedSub::Idle:
        if (a.step == 0) {
            f.behaviour = 0;
            a.anim = 0;
            a.enterStep(1);
        }
        break;
    case ShedSub::Talk: {
        const uint8_t st = a.step;
        if (st == 0) {
            f.behaviour = kShedNoHeadLook | kShedNoTalk | kShedTalking;
            a.anim = 0;
            eventState().speakerAnimation = kNoSpeakerAnimation;
            a.enterStep(1);
        }
        if (st <= 1) {
            if (storyFlag(kFlagShedLookAside)) {
                f.behaviour = uint8_t((f.behaviour & uint8_t(~kShedNoHeadLook)) | kShedLookAside);
                a.step = 2;
            }
        } else if (st == 2) {
            if (storyFlag(kFlagShedLookOff)) {
                f.behaviour = uint8_t((f.behaviour & uint8_t(~kShedLookAside)) | kShedNoHeadLook);
                a.step = 3;
            }
        }
        followSpeakerAnimation(a, 0);
        if (eventState().active == 0) setSub(a, ShedSub::Idle);
        break;
    }
    case ShedSub::Fall: {
        const uint8_t st = a.step;
        if (st == 0) {
            f.behaviour = kWalkerBounces | kShedNoFall | kShedNoTalk | kShedNoHitEntry;
            a.step = 1;
            a.acceleration.y = 0x40;
            resetModelScale(a);
        } else if (st != 1) {
            break;
        }
        // exe 0x4438c0: landed -> the bounce, sound 0xc4; else the move along the yaw and the gravity
        bool landed = false;
        if (f.hits & kWalkLanded) {
            a.velocity.z = int16_t(a.velocity.z >> 1);
            a.velocity.y = int16_t(uint32_t(-int(a.velocity.y) >> 1));
            a.rotation.x = 0;
            a.airborne = kAirborneFalling;
            soundPlayAtPos(kLandSound, a.pos);
            landed = true;
        } else {
            moveAlongYaw(a.pos, a.rotation.y, 0, a.velocity.y, a.velocity.z); // exe 0x5e36f0
            a.velocity.y += a.acceleration.y;
        }
        if (landed) {
            a.airborne = 0;
            a.velocity.y = 0;
            setSub(a, ShedSub::Idle);
        }
        break;
    }
    default: notPortedAt(kRollPort, "0x443330 sub-state", a.subState);
    }
    if (a.animKey.events & kStepEvent) soundPlayAtPos(kStepSound, a.pos);
    // exe 0x443930: the walls / the territory / the ground
    if (walkerCollide(env, a, f.hits, f.behaviour, true, kMaxDrop)) {
        a.airborne = kAirborneFalling;
        if (!(f.behaviour & kShedNoFall)) setSub(a, ShedSub::Fall);
    }
    if (!(f.behaviour & kShedNoTalk) && player().talksTo(a) && eventState().active == 0) {
        talk(a, uint16_t(int16_t(f.message.get())));
        setSub(a, ShedSub::Talk);
    }
}
// exe 0x443330
void shedRoll(const PlayerEnv& env, AnimatedActor& a) {
    ShedRollFields& f = classState<ShedRollFields>(a);
    f.kind = 0;
    switch (RollState(a.state)) {
    case RollState::Init: { // exe 0x4435a0
        a.flags |= kActorDrawn;
        if (!bindActorModel(a, a.variant)) {
            freeActor(a);
            return;
        }
        a.rotation.x = 0;
        a.rotation.z = 0;
        clearMotionWords(a);
        resetModelScale(a);
        a.anim = 1;
        a.prevAnim = kNoAnimShown;
        a.runFlags |= kActorRunMarked;
        const uint32_t hp = rowHitPoints(a);
        a.maxHitPoints = int16_t(hp);
        a.hitPoints = int16_t(hp << 4);
        a.collisionBox.pointAt(shedCollisionBox.stateAddress());
        a.interactionBox.pointAt(shedTalkBox.stateAddress());
        a.interaction = kInteractTalk;
        a.airborne = 0;
        a.onPlatform = 0;
        a.clearClassBlock();
        f.box.previousX = a.pos.x.whole;
        f.box.previousZ = a.pos.z.whole;
        f.box.result = 0;
        f.headPitch = 0;
        f.headYaw = 0;
        a.enterState(uint8_t(RollState::Main));
        a.pos.y.whole = int16_t(actorGround(env, a));
        break;
    }
    case RollState::Main: shedMain(env, a, f); break;
    default: notPortedAt(kRollPort, "0x443330 state", a.state);
    }
    if (!(a.flags & kActorLive)) return;
    if (a.anim == a.prevAnim) animStep(a);
    else animSet(a, a.anim, 0);
    const uint8_t bits = f.behaviour;
    if (bits & kShedNoHeadLook) {
        relaxHead(f.headYaw, 0x40);
        relaxHead(f.headPitch, 0x40);
    } else {
        uint32_t ang = playerDirection(a);
        if (bits & kShedLookAside) ang -= 0x180;
        const uint16_t h = uint16_t(f.headYaw);
        const int d = wrappedDiff(int((uint32_t(uint16_t(a.rotation.y)) + h) & kAngleMask) - int(ang & kAngleMask));
        turnHead(f.headYaw, d, 0x20, 0x40, kHeadYawLimit);
        const int16_t p = f.headPitch;
        int e = ((int(a.pos.y.whole) - int(player().pos.y.whole)) >> 1) + p;
        if (f.behaviour & kShedLookAside) e -= 0xc0;
        turnHead(f.headPitch, e, 0x10, 0x20, kHeadPitchLimit);
    }
    applyHead(f.headPitch, f.headYaw);
    animPose(a);
    const int8_t b = int8_t(f.behaviour);
    resetBox(a, f.box);
    if (!(b & kShedNoHitEntry)) {
        const ShortVec4& bone = bonePoint(a, 0, kRollPort);
        const int16_t pos[3] = {bone.x, bone.y, bone.z};
        const uint32_t noLockOn = (uint32_t(int32_t(b)) & kShedNoLockOn) ? kHitNoLockOn : 0u;
        hitListSubmit(pos, f.box,
                      hitOwner(a.slot) | noLockOn | hitGroup(kHitGroupLiftable) | kHitContactMotion | hitRadius(0x28),
                      kHarmlessSolidBump);
    }
    // exe 0x5f20f0(actor, 0x30, 0x160, 0): the shadow (render output)
    actorCollisionSubmitSolid(a, SolidType::StandOn, kNoBone, 0);
}

// ==== Roll in the field (exe 0x4e4c20, ST0D pool 0x20 type 0x60; the spawner 0x41b440) ===============================
// The follower of exe 0x445b00 with hit points and reactions: the player's shots hurt her (exe 0x4e5cf0: the tool-kit
// balance -100, the flags 0x684 / 0x88; at 0 hit points -1000, the flags 0x5d0 / 0x89 and she faints = Fainted), he can
// lift her (exe 0x573a60: Lifted), and along the tracks (x between -0x1200 and -0x800) she walks to fixed points
// instead of his last sighting. States (the exe's table 0x896164): 0 exe 0x4e4ed0, 1 exe 0x4e5020 (the sub-states 0x896174),
// 2 exe 0x4e5af0 lifted, 3 exe 0x4e5bc0 fainted (the sub-states 0x896198: 0x4e5c20, 0x4e5c60 talk).
enum class FieldRollSub : uint8_t {
    Idle = 0,        // exe 0x4e51e0
    Walk = 1,        // exe 0x4e5250
    Run = 2,         // exe 0x4e5420
    Search = 3,      // exe 0x4e5610
    Talk = 4,        // exe 0x4e5760
    Fall = 5,        // exe 0x4e57d0
    KnockedBack = 6, // exe 0x4e5850
    Flinch = 7,      // exe 0x4e5960
    Shocked = 8,     // exe 0x4e59f0
};
enum class FaintedSub : uint8_t { Lie = 0, Talk = 1 };
const DataTable<CollisionBoxRecord> fieldRollCollisionBox{TableId::RollFollowerFieldRollCollisionBox};
const DataTable<InteractionBoxRecord> fieldRollTalkBox{TableId::RollFollowerFieldRollTalkBox};
constexpr uint32_t kFlagRollFainted = 0x5d0, kFlagRollHurt = 0x684, kFlagRollHurtOnce = 0x88, kFlagRollFaintedOnce = 0x89;
constexpr uint32_t kHurtingHits = 0x1e0000;      // the attack kinds that hurt her (& the result)
constexpr uint32_t kStrongPushHit = kHitAttackStrongPush;
constexpr uint32_t kSprayHit = kHitAttackWater;  // the extinguisher's spray: a shock
constexpr uint16_t kHurtSound = 0x15f, kLiftSoundRare = 0x160;
constexpr uint8_t kBlinkTicks = 0x40;
constexpr uint8_t kHitsPerKnockBack = 3;
// the tracks (x between B and A), the fixed points' z
constexpr int16_t kTrackEast = -0x800, kTrackWest = -0x1200, kTrackZ = -0x1300;

// exe 0x4e5cf0: the hit she took: 0 none, 1 a flinch, 2 a knock-back (every third), 3 a shock (the spray)
int fieldRollHurt(AnimatedActor& a, RollFields& f) {
    const uint32_t r = f.box.result;
    uint32_t dmg = r & kHitDamageMask;
    if (dmg == 0) return (r & kSprayHit) ? 3 : 0;
    uint32_t take = 0;
    if (r & kHurtingHits) {
        f.timer = f.box.contact.x;
        a.tint = kTintFlash;
        if (r & kStrongPushHit) {
            toolKitBalanceAdd(-0x64);
            setStoryFlag(kFlagRollHurt);
            setStoryFlag(kFlagRollHurtOnce);
            f.hitsToKnockBack = 1;
            take = dmg;
        } else if (r & kHitAttackByPlayer) {
            toolKitBalanceAdd(-0x64);
            setStoryFlag(kFlagRollHurt);
            setStoryFlag(kFlagRollHurtOnce);
            take = dmg;
        }
        --f.hitsToKnockBack;
        if (f.hitsToKnockBack != 0) {
            dmg = 1;
        } else {
            f.hitsToKnockBack = kHitsPerKnockBack;
            dmg = 2;
        }
    }
    if (hitPointsAdd(a, -int(take)) != 0) {
        toolKitBalanceAdd(-1000);
        setStoryFlag(kFlagRollFainted);
        setStoryFlag(kFlagRollFaintedOnce);
    }
    return int(dmg);
}
// the walk / run target of exe 0x4e5250 / 0x4e5420: on the tracks two fixed points, else the last sighting (true)
bool fieldRollTarget(AnimatedActor& a, RollFields& f, int16_t& tx, int16_t& tz) {
    const int16_t px = player().pos.x.whole, rx = a.pos.x.whole;
    auto fixed = [&](int16_t x) {
        tx = x;
        tz = kTrackZ;
        return false;
    };
    if (px > kTrackEast) {
        if (rx < kTrackWest) return fixed(kTrackWest);
        if (rx < kTrackEast) return fixed(kTrackEast);
    } else if (px > kTrackWest) {
        if (rx < kTrackWest) return fixed(kTrackWest);
        if (rx > kTrackEast) return fixed(kTrackEast);
    } else {
        if (rx > kTrackEast) return fixed(kTrackEast);
        if (rx > kTrackWest) return fixed(kTrackWest);
    }
    tx = f.targetX;
    tz = f.targetZ;
    return true;
}
// the end of a hurt reaction: fainted at no hit points (the animation `faint`), else back to idle
void hurtReactionEnd(AnimatedActor& a, RollFields& f, uint8_t faint, bool blink, int idleAnim) {
    if (a.hitPoints < 0) {
        a.anim = faint;
        a.enterState(uint8_t(RollState::Fainted));
    } else {
        if (blink) f.coolDown = kBlinkTicks;
        if (idleAnim >= 0) a.anim = uint8_t(idleAnim);
        setSub(a, FieldRollSub::Idle);
    }
}
void fieldRollSub(AnimatedActor& a, RollFields& f) {
    switch (FieldRollSub(a.subState)) {
    case FieldRollSub::Idle:
        if (a.step == 0) {
            f.behaviour = 0;
            a.enterStep(1);
        }
        if (a.step == 1 && a.animEnded()) {
            a.anim = 0;
            a.step = 2;
        }
        if (distanceFromPlayer(a) > 0x200 && a.animEnded()) setSub(a, FieldRollSub::Walk);
        return;
    case FieldRollSub::Walk: {
        const uint8_t st = a.step;
        bool sighting = false;
        if (st == 0) {
            f.behaviour = 0;
            a.anim = 1;
            a.enterStep(1);
            a.velocity.z = -0x80;
        }
        if (st <= 1) {
            int16_t tx, tz;
            sighting = fieldRollTarget(a, f, tx, tz);
            turnYawTo(a.rotation.y, uint32_t(angleFromPoint(a, tx, tz)), 0x40);
            moveForward(a, a.velocity.z);
        }
        const int dp = distanceFromPlayer(a);
        if (f.hits & kWalkHitSides) {
            if ((f.sight & kSightBlockedBit) && sighting) setSub(a, FieldRollSub::Search);
            return;
        }
        if (!a.animEnded()) return;
        if (dp < 0x100) {
            a.subState = uint8_t(FieldRollSub::Idle);
            a.anim = 5;
            a.restartStep();
        } else if (dp > 0x400) {
            setSub(a, FieldRollSub::Run);
        }
        return;
    }
    case FieldRollSub::Run: {
        const uint8_t st = a.step;
        bool sighting = false;
        int16_t tx = 0, tz = 0;
        if (st == 0) {
            f.behaviour = 0;
            a.anim = 2;
            a.velocity.z = -0x180;
        }
        if (st <= 1) {
            const int turn = player().velocity.z != 0 ? 0x30 : 0x60;
            sighting = fieldRollTarget(a, f, tx, tz);
            turnYawTo(a.rotation.y, uint32_t(angleFromPoint(a, tx, tz)), turn);
            moveForward(a, a.velocity.z);
        }
        if (!a.animEnded()) return;
        if (distanceFromPlayer(a) < 0x200) {
            a.subState = uint8_t(FieldRollSub::Idle);
            a.anim = 6;
            a.restartStep();
            return;
        }
        // the target's distance: the exe's local keeps its high words from this tick (or stale ones -- unused then)
        if (sighting && distanceFromPoint(a, tx, tz) < 0x100) {
            setSub(a, FieldRollSub::Search);
            return;
        }
        if ((f.hits & kWalkHitSides) && (f.sight & kSightBlockedBit) && sighting) setSub(a, FieldRollSub::Search);
        return;
    }
    case FieldRollSub::Search: {
        const uint8_t st = a.step;
        bool pick = false, count = false;
        if (st == 0) {
            f.behaviour = kWalkerBounces | kRollNoHeadLook;
            a.anim = 1;
            a.velocity.z = -0x80;
            a.step = 1;
            pick = true;
        } else if (st == 1) {
            pick = true;
        } else if (st == 2) {
            count = true;
        }
        if (pick) {
            a.step = 2;
            f.timer = 0x40;
            f.searchYaw = int16_t(angleOf(player().pos, a.pos));
            const int ang = angleOf(player().pos, a.pos);
            f.searchYaw = int16_t(ang);
            const int d = wrappedDiff(int(uint16_t(a.rotation.y) & kAngleMask) - (ang & kAngleMask));
            const uint32_t r = rngStep() & 0x3ff;
            if (d < 0) f.searchYaw = int16_t(uint32_t(uint16_t(f.searchYaw)) - r);
            else f.searchYaw = int16_t(uint32_t(uint16_t(f.searchYaw)) + r);
            count = true;
        }
        if (count) {
            --f.timer;
            if (f.timer == 0) a.step = 1;
        }
        turnYawTo(a.rotation.y, uint32_t(int32_t(f.searchYaw)), 0x40);
        moveForward(a, a.velocity.z);
        if (f.hits & kWalkHitSides) {
            f.searchYaw = a.rotation.y;
            f.timer = 0x40;
        }
        if ((f.sight & kSightDone) && !(f.sight & kSightBlockedBit)) setSub(a, FieldRollSub::Walk);
        return;
    }
    case FieldRollSub::Talk:
        if (a.step == 0) {
            f.behaviour = kRollNoHeadLook | kRollNoTalk | kRollNoHurt;
            a.anim = 3;
            eventState().speakerAnimation = kNoSpeakerAnimation;
            a.enterStep(1);
        }
        followSpeakerAnimation(a, 3);
        if (eventState().active == 0) setSub(a, FieldRollSub::Idle);
        return;
    case FieldRollSub::Fall: {
        const uint8_t st = a.step;
        if (st == 0) {
            f.behaviour = kWalkerBounces | kRollNoReactions | kRollNoHitEntry;
            a.step = 1;
            a.acceleration.y = 0x40;
            resetModelScale(a);
        } else if (st != 1) {
            return;
        }
        if (!walkerFallStep(a, f.hits, a.rotation.y)) return; // exe 0x4844b0
        a.airborne = 0;
        a.velocity.y = 0;
        switch (a.anim) {
        case 1: setSub(a, FieldRollSub::Walk); return;
        case 2: setSub(a, FieldRollSub::Run); return;
        default: setSub(a, FieldRollSub::Idle); return;
        }
    }
    case FieldRollSub::KnockedBack: {
        const uint8_t st = a.step;
        if (st > 3) return;
        if (st == 0) {
            a.enterStep(1);
            f.behaviour = kWalkerBounces | kRollNoReactions | kRollNoTalk | kRollNoHurt;
            a.anim = 9;
            a.airborne = kAirborneFalling;
            a.rotation.y = f.timer;
            a.velocity.z = -0x100;
            a.acceleration.z = 0x20;
            a.velocity.x = 0;
            a.velocity.y = -0x100;
            a.acceleration.y = 0x40;
            soundPlayAtPos(kHurtSound, a.pos);
            resetModelScale(a);
        }
        if (st <= 1 && a.animEnded()) {
            a.anim = 0xa;
            a.step = 2;
        }
        if (st <= 2) {
            if (walkerFallStep(a, f.hits, f.timer)) {
                a.velocity.z = int16_t(uint32_t(uint16_t(a.velocity.z)) << 1);
                a.airborne = 0;
                a.velocity.y = 0;
                a.step = 3;
                a.anim = 0xb;
                f.behaviour &= uint8_t(~(kWalkerBounces | kRollNoReactions));
            }
            return;
        }
        if (a.animEnded()) hurtReactionEnd(a, f, 0xd, true, 0x10);
        return;
    }
    case FieldRollSub::Flinch: {
        const uint8_t st = a.step;
        if (st == 0) {
            f.behaviour = kRollNoHeadLook | kRollNoTalk | kRollNoHurt;
            a.enterStep(1);
            a.rotation.y = int16_t(uint32_t(uint16_t(f.timer)) - uint32_t(kHalfTurn));
            a.anim = 8;
            clearMotionWords(a);
            soundPlayAtPos(kHurtSound, a.pos);
            resetModelScale(a);
        } else if (st != 1) {
            return;
        }
        if (a.animEnded()) hurtReactionEnd(a, f, 0xe, true, -1);
        return;
    }
    case FieldRollSub::Shocked: {
        const uint8_t st = a.step;
        if (st == 2) {
            if (a.animEnded()) hurtReactionEnd(a, f, 0xe, false, -1);
            return;
        }
        if (st > 2) return;
        if (st == 0) {
            f.behaviour = kRollNoHeadLook | kRollNoTalk | kRollNoHurt;
            a.enterStep(1);
            a.anim = 8;
            clearMotionWords(a);
            resetModelScale(a);
            soundPlayAtPos(kHurtSound, a.pos);
            f.animKey = -1;
        }
        if (f.box.result & kSprayHit) {
            f.timer = 3;
        } else {
            --f.timer;
            if (f.timer == 0) a.step = 2;
        }
        a.prevAnim = kNoAnimShown;
        // the shake: the start key swings 4 .. 3 and back (the step counter its direction)
        if (a.stepCounter == 0) {
            ++f.animKey;
            if (f.animKey > 4) a.stepCounter = 1;
        } else if (a.stepCounter == 1) {
            --f.animKey;
            if (f.animKey < 3) a.stepCounter = 0;
        }
        return;
    }
    default: notPortedAt(kRollPort, "0x4e4c20 sub-state", a.subState);
    }
}
// exe 0x4e5020
void fieldRollMain(const PlayerEnv& env, AnimatedActor& a, RollFields& f) {
    a.tint = kTintNeutral;
    uint8_t c = f.coolDown;
    if (c != 0) { // the hurt blink
        c = uint8_t(c - 1);
        f.coolDown = c;
        if (c == 0) {
            a.flags |= kActorDrawn;
            f.behaviour &= uint8_t(~kRollNoHitEntry);
        } else if (c & 3) {
            a.flags |= kActorDrawn;
        } else {
            a.flags &= uint8_t(~kActorDrawn);
        }
    } else if (!(f.behaviour & kRollNoHurt)) {
        const int r = fieldRollHurt(a, f);
        if (r != 0) {
            if (r == 1) a.subState = uint8_t(FieldRollSub::Flinch);
            else if (r == 2) a.subState = uint8_t(FieldRollSub::KnockedBack);
            else if (r == 3) a.subState = uint8_t(FieldRollSub::Shocked);
            a.restartStep();
        }
    }
    fieldRollSub(a, f);
    if (f.coolDown != 0) f.behaviour |= kRollNoHitEntry;
    if (walkerCollide(env, a, f.hits, f.behaviour, false, kMaxDrop)) { // exe 0x4e5df0
        a.airborne = kAirborneFalling;
        if (!(f.behaviour & kRollNoReactions)) setSub(a, FieldRollSub::Fall);
    }
    if (playerLiftTargetCheck(a, kLiftQueryOnly) && !(f.behaviour & kRollNoLift)) { // exe 0x573a60
        f.coolDown = 0;
        a.flags |= kActorDrawn;
        a.enterState(uint8_t(RollState::Lifted));
        f.behaviour |= kRollNoHitEntry;
    } else if (!(f.behaviour & kRollNoTalk) && player().talksTo(a) && eventState().active == 0) {
        talk(a, uint16_t(uint8_t(f.message.get())));
        setSub(a, FieldRollSub::Talk);
    }
    rollSighting(a, f, -0x100);
}
// exe 0x4e4c20
void fieldRoll(const PlayerEnv& env, AnimatedActor& a) {
    RollFields& f = classState<RollFields>(a);
    a.prevAnim = a.anim;
    a.savePreviousPose();
    a.savePreviousContact();
    switch (RollState(a.state)) {
    case RollState::Init: { // exe 0x4e4ed0
        a.flags |= kActorDrawn;
        if (!bindActorModel(a, a.variant)) {
            freeActor(a);
            return;
        }
        a.rotation.x = 0;
        a.rotation.z = 0;
        clearMotionWords(a);
        resetModelScale(a);
        a.anim = 0;
        a.prevAnim = kNoAnimShown;
        a.maxHitPoints = int16_t(rowHitPoints(a));
        if (storyFlag(kFlagRollFainted)) {
            a.hitPoints = 0;
            a.enterState(uint8_t(RollState::Fainted));
        } else {
            a.enterState(uint8_t(RollState::Main));
            a.hitPoints = a.maxHitPoints;
        }
        a.collisionBox.pointAt(fieldRollCollisionBox.stateAddress());
        a.interactionBox.pointAt(fieldRollTalkBox.stateAddress());
        a.interaction = kInteractTalk | kInteractLift;
        a.pos.y.whole = int16_t(actorGround(env, a));
        a.airborne = 0;
        a.onPlatform = 0;
        a.clearClassBlock();
        f.box.previousX = a.pos.x.whole;
        f.box.previousZ = a.pos.z.whole;
        f.box.result = 0;
        f.coolDown = 0;
        f.hitsToKnockBack = kHitsPerKnockBack;
        f.targetX = a.pos.x.whole;
        f.targetZ = a.pos.z.whole;
        f.seenX = player().pos.x.whole;
        f.seenZ = player().pos.z.whole;
        registerEnemy(a.slot, 0);
        rollProbe(a, f, -0x100);
        break;
    }
    case RollState::Main: fieldRollMain(env, a, f); break;
    case RollState::Lifted: { // exe 0x4e5af0: lifted by the player
        const uint8_t st = a.step;
        if (st == 0) {
            f.behaviour = kRollNoLift | kRollNoHurt;
            a.rotation.y = int16_t(angleOf(player().pos, a.pos));
            a.anim = 7;
            resetModelScale(a);
            const uint32_t r = rngStep();
            soundPlayAtPos((r & 3) ? kHurtSound : kLiftSoundRare, a.pos);
            a.tint = kTintNeutral;
            a.step = 1;
        } else if (st == 1) {
            if (a.animKey.frame > 3) {
                f.behaviour |= kRollAttackEntry;
                a.step = 2;
            }
        } else if (st == 2) {
            f.behaviour &= uint8_t(~kRollAttackEntry);
            a.step = 3;
        }
        if (a.animEnded()) a.enterState(uint8_t(RollState::Main));
        break;
    }
    case RollState::Fainted: { // exe 0x4e5bc0
        a.tint = kTintNeutral;
        if (FaintedSub(a.subState) == FaintedSub::Lie) { // exe 0x4e5c20
            if (a.step == 0) {
                f.behaviour = kRollNoTalk;
                a.enterStep(1);
            }
            if (a.step == 1 && a.animEnded()) {
                a.anim = 0xf;
                a.step = 2;
                f.behaviour &= uint8_t(~kRollNoTalk);
            }
        } else if (FaintedSub(a.subState) == FaintedSub::Talk) { // exe 0x4e5c60
            if (a.step == 0) {
                f.behaviour = kRollNoHeadLook | kRollNoTalk | kRollNoHurt;
                a.anim = 0xf;
                eventState().speakerAnimation = kNoSpeakerAnimation;
                a.enterStep(1);
            }
            if (eventState().active == 0) {
                if (storyFlag(kFlagRollFainted)) {
                    setSub(a, FaintedSub::Lie);
                } else {
                    a.anim = 0x10;
                    a.hitPoints = a.maxHitPoints;
                    a.enterState(uint8_t(RollState::Main));
                }
            }
        } else {
            notPortedAt(kRollPort, "0x4e5bc0 sub-state", a.subState);
        }
        if (!(f.behaviour & kRollNoTalk) && player().talksTo(a) && eventState().active == 0) {
            eventStartAtActor(a, 0x10, 0, uint16_t(int16_t(f.faintedMessage.get())));
            setSub(a, FaintedSub::Talk);
        }
        break;
    }
    default: notPortedAt(kRollPort, "0x4e4c20 state", a.state);
    }
    if (!(a.flags & kActorLive)) return;
    if (a.anim == a.prevAnim) animStep(a);
    else animSet(a, a.anim, f.animKey);
    followerHeadLook(a, f);
    applyHead(f.headPitch, f.headYaw);
    animPose(a);
    const int8_t b = int8_t(f.behaviour);
    resetBox(a, f.box);
    if (!(b & kRollNoHitEntry)) {
        const ShortVec4& bone = bonePoint(a, 0, kRollPort);
        const int16_t pos[3] = {bone.x, bone.y, bone.z};
        if (!(b & kRollAttackEntry))
            hitListSubmit(pos, f.box,
                          hitOwner(a.slot) | hitGroup(kHitGroupLiftable) | kHitNoLockOn | kHitContactMotion |
                              hitRadius(0x28),
                          kHarmlessSolidBump);
        else
            hitListSubmit(pos, f.box,
                          hitOwner(a.slot) | hitGroup(kHitGroupBodies) | kHitContactMotion | hitRadius(0xf0),
                          animatedStats(a.type).bodyAttack | kHarmlessSolid);
    }
    // exe 0x5f20f0 (the shadow, unless Lifted: carried) is render output
    actorCollisionSubmitSolid(a, SolidType::PushOut, kNoBone, 0);
}

} // namespace

void rollUpdate(const PlayerEnv& env, uint32_t actor) { follower(env, animatedActorAt(actor)); }
void shedRollUpdate(const PlayerEnv& env, uint32_t actor) { shedRoll(env, animatedActorAt(actor)); }
void fieldRollUpdate(const PlayerEnv& env, uint32_t actor) { fieldRoll(env, animatedActorAt(actor)); }

} // namespace od::port
