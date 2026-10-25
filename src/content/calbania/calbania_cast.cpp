// ST20 (the Calbania town): the cast classes of the stage's pool-0x20 table 0x8e7f14 that the town walker 0x441950
// does not cover -- Shu's brothers Appo and Dah as script puppets (type 0 subs 1 / 2, variants 0x11 / 0x12, spawned
// by the camera op 0x40 of scripts 0x10 / 0x11: records 0x8a0380 / 0x8a0394, 0x8a04a0) and the type 0xa classes.
// Transcribed from dash2.exe one function per exe function (conventions as game/story.cpp). Render output skipped:
// the shadow list exe 0x5f20f0.
//
// Public functions (namespace od::port, ActorUpdateFn signature; content/calbania/register registers them):
//   appoDahPuppetUpdate   exe 0x446720  type 0 sub 1: script puppet walking in / standing / walking off; variant 0x11
//                                       -> 0x446800, 0x12 -> 0x4469d0 ([0x88e240 / 0x88e244]); keyed on the script
//                                       block's step sub and step tick
//   sittingPuppetUpdate   exe 0x446b90  type 0 sub 2: the sitting puppets of script 0x11 (head turned toward a point,
//                                       added to the decoded frame's part 1); variants 0x11 -> 0x447020, 0x12 ->
//                                       0x447260, else 0x446de0 ([0x88e258..0x88e260])
//   townBirdUpdate        exe 0x45d970  type 0xa sub 0: the town birds (story-8 town records 0x8c875c / 0x8c8770
//                                       variant 2, script 0x11 record 0x8a04dc; also other stages' tables)
//   townStandingCastUpdate    exe 0x45f010  type 0xa sub 1: a standing cast member (variant 1: in front of the player)
#include <cstdint>

#include "content/calbania/appo_dah_face.hpp"
#include "content/calbania/calbania_cast_state.hpp"
#include "content/calbania/ch3_story.hpp"
#include "engine/data_source.hpp"
#include "game/actor_animation.hpp"
#include "game/actor_collision.hpp"
#include "game/actor_model.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actor_stats.hpp"
#include "game/address_forms.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/carry.hpp"
#include "game/cutscene_actor.hpp"
#include "game/distances.hpp"
#include "game/enemy_motion.hpp"
#include "game/event.hpp"
#include "game/event_state.hpp"
#include "game/field_alias.hpp"
#include "game/game_state.hpp"
#include "game/hit_effects.hpp"
#include "game/hit_list.hpp"
#include "game/hit_points.hpp"
#include "game/player/player_motion.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/rewards.hpp"
#include "game/script_state.hpp"
#include "game/story.hpp"
#include "game/story_flags.hpp"

namespace od::port {

using namespace calbania_cast_state;

namespace {

using address_forms::E;

int16_t t16(int v) { return int16_t(uint16_t(uint32_t(v))); }
void shadow(const AnimatedActor&) {} // exe 0x5f20f0(actor, 0x30, 0x160, 0): the shadow list entry (render output)
constexpr uint8_t kNoAnim = 0xff;
constexpr uint8_t kAppoVariant = 0x11, kDahVariant = 0x12;

constexpr uint8_t kPuppetHold = 0x01; // the animation restarted at its key every tick (the pose held)
enum class WalkingPuppetState : uint8_t { Bind = 0, WalkIn = 1, Stand = 2, WalkOff = 3, Done = 4 };
enum class WalkInStep : uint8_t { Walk = 0, Standing = 1 };
enum class StandStep : uint8_t { Place = 0, Listen = 1 };
enum class AppoWalkOffStep : uint8_t { Wait = 0, Start = 1, Walk = 2 };
enum class DahWalkOffStep : uint8_t { Start = 0, Walk = 1 };
constexpr uint8_t kAnimStand = 0, kAnimWalk = 2, kAnimTalk = 3;
// the talk cues of scripts 0x10 / 0x11 (a message's fb code sets them; the puppet plays its talk animation)
constexpr uint32_t kFlagTalkCueA = 0x591, kFlagTalkCueB = 0x592, kFlagTalkCueC = 0x593;

// the state 0 of both variants: the record's model (none: freed), the box, the class block cleared, walking (animation
// 2, speed -0x100), the mouth closed
bool walkingPuppetBind(AnimatedActor& actor, WalkingPuppetFields& f) {
    if (!bindActorModel(actor, actor.variant)) { // exe 0x5e0bc0(actor, +6)
        freeActor(actor);                         // exe 0x5e1490
        return false;
    }
    actor.collisionBox.pointAt(TableId::CalbaniaCastWalkingPuppetBox);
    actor.clearClassBlock();
    actor.enterState(uint8_t(WalkingPuppetState::WalkIn));
    actor.anim = kAnimWalk;
    actor.prevAnim = kNoAnim;
    f.castFlags &= uint8_t(~kPuppetHold);
    actor.velocity.z = int16_t(0xff00);
    appoDahFaceReset(actor, f.mouth);
    return true;
}
// the "flag n set -> the talk animation, the flag cleared" step of the stand states
void puppetTalkFlag(AnimatedActor& actor, WalkingPuppetFields& f, uint32_t n, uint8_t anim) {
    if (!storyFlag(n)) return;
    actor.anim = anim;
    actor.prevAnim = kNoAnim;
    f.castFlags &= uint8_t(~kPuppetHold);
    clearStoryFlag(n);
}
// the walk in until the step tick `standTick`, then standing; the step sub 1 -> the stand
void walkIn(AnimatedActor& actor, WalkingPuppetFields& f, const ScriptState& s, int32_t standTick) {
    if (WalkInStep(actor.step) == WalkInStep::Walk) {
        integrateVelocity(actor, 0, 0, actor.velocity.z); // exe 0x5e3690
        if (s.stepTick == standTick) {
            actor.anim = kAnimStand;
            f.castFlags |= kPuppetHold;
            actor.step = uint8_t(WalkInStep::Standing);
        }
    }
    if (s.stepSub == 1) actor.enterState(uint8_t(WalkingPuppetState::Stand));
}
// the stand at its place, talking on its flag; the step sub 2 -> the walk off
void standAt(AnimatedActor& actor, WalkingPuppetFields& f, const ScriptState& s, int16_t z, int16_t yaw, uint32_t flag) {
    switch (StandStep(actor.step)) {
    case StandStep::Place:
        actor.pos.x.whole = 0x5c0;
        actor.pos.y.whole = int16_t(0xfd80);
        actor.pos.z.whole = z;
        actor.rotation.y = yaw;
        actor.step = uint8_t(StandStep::Listen);
        [[fallthrough]];
    case StandStep::Listen: puppetTalkFlag(actor, f, flag, kAnimTalk); break;
    default: break;
    }
    if (s.stepSub == 2) actor.enterState(uint8_t(WalkingPuppetState::WalkOff));
}
// exe 0x446800: variant 0x11 (states [0x4469bc])
void appoPuppet(AnimatedActor& actor, WalkingPuppetFields& f, const ScriptState& s) {
    switch (WalkingPuppetState(actor.state)) {
    case WalkingPuppetState::Bind: walkingPuppetBind(actor, f); return;
    case WalkingPuppetState::WalkIn: walkIn(actor, f, s, 0x1e); return;
    case WalkingPuppetState::Stand: standAt(actor, f, s, 0x100, 0xca0, kFlagTalkCueB); return;
    case WalkingPuppetState::WalkOff:
        switch (AppoWalkOffStep(actor.step)) {
        case AppoWalkOffStep::Wait:
            if (s.stepTick == 3) actor.step = uint8_t(AppoWalkOffStep::Start);
            break;
        case AppoWalkOffStep::Start:
            actor.velocity.z = int16_t(0xfe80);
            actor.anim = kAnimWalk;
            f.castFlags &= uint8_t(~kPuppetHold);
            actor.step = uint8_t(AppoWalkOffStep::Walk);
            [[fallthrough]];
        case AppoWalkOffStep::Walk:
            turnYawTo(actor.rotation.y, 0x560, 0x200);               // exe 0x5e3df0
            moveAlongYaw(actor.pos, 0x560, 0, 0, actor.velocity.z); // exe 0x5e36f0
            break;
        default: break;
        }
        if (s.stepSub == 3) actor.enterState(uint8_t(WalkingPuppetState::Done));
        return;
    default: return;
    }
}
// exe 0x4469d0: variant 0x12 (states [0x446b78])
void dahPuppet(AnimatedActor& actor, WalkingPuppetFields& f, const ScriptState& s) {
    switch (WalkingPuppetState(actor.state)) {
    case WalkingPuppetState::Bind: walkingPuppetBind(actor, f); return;
    case WalkingPuppetState::WalkIn: walkIn(actor, f, s, 0x3c); return;
    case WalkingPuppetState::Stand: standAt(actor, f, s, 0xbf, 0xbd0, kFlagTalkCueC); return;
    case WalkingPuppetState::WalkOff:
        switch (DahWalkOffStep(actor.step)) {
        case DahWalkOffStep::Start:
            actor.velocity.z = int16_t(0xff00);
            actor.anim = kAnimWalk;
            f.castFlags &= uint8_t(~kPuppetHold);
            actor.step = uint8_t(DahWalkOffStep::Walk);
            [[fallthrough]];
        case DahWalkOffStep::Walk:
            turnYawTo(actor.rotation.y, 0x4d0, 0x100);
            moveAlongYaw(actor.pos, 0x4d0, 0, 0, actor.velocity.z);
            break;
        default: break;
        }
        if (s.stepSub == 3) actor.enterState(uint8_t(WalkingPuppetState::Done));
        return;
    default: return;
    }
}
// exe 0x446720's body
void walkingPuppet(AnimatedActor& actor) {
    WalkingPuppetFields& f = classState<WalkingPuppetFields>(actor);
    castSavePose(actor);
    const ScriptState& s = scriptState();
    switch (actor.variant) {
    case kAppoVariant: appoPuppet(actor, f, s); break;
    case kDahVariant: dahPuppet(actor, f, s); break;
    default: break;
    }
    if (!(actor.flags & kActorLive)) return;
    if (f.noGround == 0) actor.pos.y.whole = int16_t(actorGround(*E, actor)); // exe 0x40f190
    if (actor.anim == actor.prevAnim && !(f.castFlags & kPuppetHold)) animStepPose(actor); // exe 0x5e1e80
    else animSetPose(actor, actor.anim, f.animKey);                                      // exe 0x5e1c30
    shadow(actor);
    actorCollisionSubmitSolid(actor, SolidType::PushOut, kNoBone, 0); // exe 0x414050(actor, 0, 0xffff, 0)
}

enum class SittingPuppetState : uint8_t { Bind = 0, Act0 = 1, Act1 = 2, Act2 = 3, Act3 = 4, Act4 = 5 };
enum class SitStep : uint8_t { Wait = 0, Done = 1 };
enum class LastActStep : uint8_t { Look = 0, Wait = 1, Turn = 2 };
constexpr int16_t kHeadYawLimit = 0x300, kHeadPitchLimit = 0x200;

// exe 0x4474a0: the mouth closed (the sitting puppets' own mouth bytes)
void sittingFaceReset(AnimatedActor& actor, SittingPuppetFields& f) {
    actor.parts[kMouthWidePart].flags |= kPartHidden;
    actor.parts[kMouthOpenPart].flags |= kPartHidden;
    actor.parts[kMouthClosedPart].flags &= uint8_t(~kPartHidden);
    f.mouthStep = 0;
    f.mouthTicks = 0;
}
bool sittingPuppetBind(AnimatedActor& actor, SittingPuppetFields& f, bool face) {
    if (!bindActorModel(actor, actor.variant)) { // exe 0x5e0bc0(actor, +6)
        freeActor(actor);
        return false;
    }
    actor.collisionBox.pointAt(TableId::CalbaniaCastSittingPuppetBox);
    actor.clearClassBlock();
    actor.enterState(uint8_t(SittingPuppetState::Act0));
    actor.anim = 0;
    actor.prevAnim = kNoAnim;
    f.castFlags |= kPuppetHold;
    if (face) sittingFaceReset(actor, f);
    f.headYaw = 0;
    f.headPitch = 0;
    return true;
}
// flag n -> the talk animation (the animation free), the flag cleared
void sitTalkFlag(AnimatedActor& actor, SittingPuppetFields& f, uint32_t n) {
    if (!storyFlag(n)) return;
    actor.anim = kAnimTalk;
    actor.prevAnim = kNoAnim;
    f.castFlags &= uint8_t(~kPuppetHold);
    clearStoryFlag(n);
}
void headTarget(SittingPuppetFields& f, uint16_t height, uint16_t yaw, uint16_t rate) {
    f.headRate = int16_t(rate);
    f.lookYaw = int16_t(yaw);
    f.lookHeight = int16_t(height);
}
// the next state on the step sub `sub`
void followSub(AnimatedActor& actor, const ScriptState& s, uint8_t sub) {
    if (s.stepSub == sub) actor.enterState(uint8_t(actor.state + 1u));
}
// exe 0x446de0: the variants other than 0x11 / 0x12 (states [0x447000])
void otherSitter(AnimatedActor& actor, SittingPuppetFields& f, const ScriptState& s) {
    const bool waiting = SitStep(actor.subState) == SitStep::Wait;
    switch (SittingPuppetState(actor.state)) {
    case SittingPuppetState::Bind: sittingPuppetBind(actor, f, false); return;
    case SittingPuppetState::Act0:
        if (waiting && s.stepTick == 0xa) {
            f.looking = 1;
            headTarget(f, 0xff80, 0, 0x10);
            actor.subState = uint8_t(SitStep::Done);
        }
        followSub(actor, s, 1);
        return;
    case SittingPuppetState::Act1:
        if (waiting) sitTalkFlag(actor, f, kFlagTalkCueC);
        followSub(actor, s, 2);
        return;
    case SittingPuppetState::Act2:
        if (waiting && s.stepTick == 0x14) {
            headTarget(f, 0xfd80, 0xe50, 0x20);
            actor.subState = uint8_t(SitStep::Done);
        }
        followSub(actor, s, 3);
        return;
    case SittingPuppetState::Act3:
        if (waiting) sitTalkFlag(actor, f, kFlagTalkCueC);
        followSub(actor, s, 4);
        return;
    case SittingPuppetState::Act4:
        switch (LastActStep(actor.subState)) {
        case LastActStep::Look:
            headTarget(f, 0xfd80, 0xd40, 0x40);
            actor.subState = uint8_t(LastActStep::Wait);
            [[fallthrough]];
        case LastActStep::Wait:
            if (s.stepTick == 0x14) actor.subState = uint8_t(LastActStep::Turn);
            [[fallthrough]];
        case LastActStep::Turn:
            turnYawTo(actor.rotation.y, 0xe00, 0x40); // exe 0x5e3df0
            sitTalkFlag(actor, f, kFlagTalkCueC);
            return;
        default: return;
        }
    default: return;
    }
}
// exe 0x447020 (variant 0x11, flag 0x591) / 0x447260 (variant 0x12, flag 0x592): states 1..4 answer the flag and follow
// the step sub (== state -> the next state); state 5 only answers
void namedSitter(AnimatedActor& actor, SittingPuppetFields& f, const ScriptState& s, uint32_t talkFlag) {
    const uint32_t st = actor.state;
    if (SittingPuppetState(st) == SittingPuppetState::Bind) {
        sittingPuppetBind(actor, f, true);
        return;
    }
    if (st > uint32_t(SittingPuppetState::Act4)) return;
    if (SitStep(actor.subState) == SitStep::Wait) sitTalkFlag(actor, f, talkFlag);
    if (st < uint32_t(SittingPuppetState::Act4) && s.stepSub == st) actor.enterState(uint8_t(st + 1));
}
// the head angle relaxing toward 0 by the rate
void headRelax(int16_t& angle, const SittingPuppetFields& f) {
    const int16_t v = angle, r = f.headRate;
    if (v < 0) {
        const int16_t n = t16(r + v);
        angle = n > 0 ? int16_t(0) : n;
    } else {
        const int16_t n = t16(v - r);
        angle = n < 0 ? int16_t(0) : n;
    }
}
// exe 0x446b90's body
void sittingPuppet(AnimatedActor& actor) {
    SittingPuppetFields& f = classState<SittingPuppetFields>(actor);
    castSavePose(actor);
    const ScriptState& s = scriptState();
    switch (actor.variant) {
    case kAppoVariant: namedSitter(actor, f, s, kFlagTalkCueA); break; // [0x88e25c] = 0x447020
    case kDahVariant: namedSitter(actor, f, s, kFlagTalkCueB); break;     // [0x88e260] = 0x447260
    default: otherSitter(actor, f, s); break;                              // [0x88e258]
    }
    if (!(actor.flags & kActorLive)) return;
    if (f.noGround == 0) actor.pos.y.whole = int16_t(actorGround(*E, actor)); // exe 0x40f190
    if (actor.anim == actor.prevAnim && !(f.castFlags & kPuppetHold)) animStep(actor); // exe 0x5e1ea0
    else animSet(actor, actor.anim, f.animKey);                                      // exe 0x5e1c50
    if (f.looking == 0) {
        headRelax(f.headYaw, f);
        headRelax(f.headPitch, f);
    } else {
        const int16_t rate = f.headRate, head = f.headYaw;
        const int d = angleDiff(uint16_t(f.lookYaw), uint32_t(uint16_t(actor.rotation.y) + uint16_t(head)));
        const int half = rate >> 1;
        if (d < -half) {
            const int16_t n = t16(rate + head);
            f.headYaw = n > kHeadYawLimit ? kHeadYawLimit : n;
        } else if (d > half) {
            const int16_t n = t16(head - rate);
            f.headYaw = n < -kHeadYawLimit ? int16_t(-kHeadYawLimit) : n;
        }
        const int16_t pitch = f.headPitch;
        const int v = ((int(actor.pos.y.whole) - int(f.lookHeight)) >> 1) + pitch;
        if (v < -half) {
            const int16_t n = t16(pitch + rate);
            f.headPitch = n > kHeadPitchLimit ? kHeadPitchLimit : n;
        } else if (v > half) {
            const int16_t n = t16(pitch - rate);
            f.headPitch = n < -kHeadPitchLimit ? int16_t(-kHeadPitchLimit) : n;
        }
    }
    LaneVec4 head = decodedFrame().entries[2]; // part 1's angles: the head
    head.x = int16_t(uint16_t(uint16_t(f.headPitch) + uint16_t(head.x)));
    head.y = int16_t(uint16_t(uint16_t(f.headYaw) + uint16_t(head.y)));
    animPose(actor); // exe 0x5e2d90
    shadow(actor);
}

constexpr uint8_t kBirdLifted = 0x04;
constexpr uint8_t kBirdAirborne = 0x08;  // in the air (bounces off the walls)
constexpr uint8_t kBirdThrown = 0x10;
constexpr uint8_t kBirdHurtHard = 0x20, kBirdHurt = 0x40;
constexpr uint8_t kBirdWatched = 0x80;   // the player watches it (its hit entry flags it)
// the bytes of the actor it follows that it sets when it arrives
struct FollowedFields {
    explicit FollowedFields(const ActorHeader& actor) : FollowedFields(const_cast<ActorHeader&>(actor)) {}
    explicit FollowedFields(ActorHeader& actor)
        : birdArrived(actor.params[1]), birdBits(actor.params[2]), birdWord(actor.params, 2) {}
    uint8_t& birdArrived;
    uint8_t& birdBits;
    BytesField<uint16_t, 4> birdWord;
};
enum class BirdState : uint8_t { Init = 0, Free = 1, Lifted = 2, Vanish = 3, Freed = 4 };
enum class BirdSub : uint8_t { Think = 0, Idle = 1, Wander = 2, Flee = 3, TurnAway = 4, Knocked = 5, KnockedHard = 6,
                               Follow = 7 };
enum class LiftedSub : uint8_t { Held = 0, Thrown = 1 };
enum class WanderStep : uint8_t { Start = 0, Walk = 1, Turned = 2 };
enum class FleeStep : uint8_t { Start = 0, Away = 1, Onward = 2, Turn = 4, TurnFast = 5, Run = 7, Slow = 0xa,
                                Settle = 0xb };
enum class TurnAwayStep : uint8_t { Start = 0, Face = 1, Startle = 2, TurnAway = 3 };
enum class KnockStep : uint8_t { Hit = 0, Fly = 1, Done = 2 };
enum class FollowStep : uint8_t { Start = 0, Walk = 1, Arrived = 2 };
enum class ThrownStep : uint8_t { Fly = 0, Land = 1 };
constexpr uint8_t kAnimBirdWalk = 0, kAnimBirdFlap = 1, kAnimBirdPeck = 2, kAnimBirdLook = 3, kAnimBirdTurnLeft = 5,
                  kAnimBirdTurnRight = 6, kAnimBirdStartle = 7, kAnimBirdTumble = 8, kAnimBirdLand = 9,
                  kAnimBirdHeld = 0xa;
constexpr uint8_t kVariantTame = 2;
constexpr uint16_t kSoundPeck = 0xd2, kSoundSquawk = 0xd1, kSoundFlap = 0xcf, kSoundHurt = 0x8d;
const DataTable<ScalarTable<int8_t, 16>> wanderTicks{TableId::CalbaniaCastWanderTicks};
const DataTable<ScalarTable<int8_t, 16>> fleeTicks{TableId::CalbaniaCastFleeTicks};
const DataTable<ScalarTable<int8_t, 16>> idleTicks{TableId::CalbaniaCastIdleTicks};

void sound(uint16_t id, const AnimatedActor& actor) { soundPlayAtPos(id, actor.pos); } // exe 0x5d5380
int iabs(int v) { return v < 0 ? -v : v; }
uint8_t coin(uint32_t r) { return uint8_t((0xaaaaaaaau >> (r & 0x1f)) & 1u); }
void stopMotion(AnimatedActor& actor) {
    actor.velocity.x = 0;
    actor.velocity.y = 0;
    actor.velocity.z = 0;
}
void birdAnim(AnimatedActor& actor, TownBirdFields& f, uint8_t anim) {
    actor.anim = anim;
    f.animKey = 0;
}
// the feather puff (pool 0xa0 type 0 variant 3) at the bird
void feathers(const AnimatedActor& actor) {
    const uint32_t address = allocActorA0(0); // exe 0x5e13b0
    if (!address) return;
    EffectActor& e = effectActorAt(address);
    const uint8_t loop = e.loopFlags;
    e.flags = kActorLive;
    e.type = 0;
    e.variant = 3;
    e.loopFlags = uint8_t(loop | kActorSweepGroup1 | kActorLateLoop);
    e.pos.x = actor.pos.x.whole;
    e.pos.y = actor.pos.y.whole;
    e.pos.z = actor.pos.z.whole;
    e.setParamsLowWord(0x40); // the puff's size
}
void birdMoveByVelocity(AnimatedActor& actor) {
    integrateVelocity(actor, actor.velocity.x, actor.velocity.y, actor.velocity.z); // exe 0x5e3690
}
uint16_t sideHalfTurn(const TownBirdFields& f) { return f.side ? 0x7ffu : 0xf801u; } // +-0x7ff by the random side
// the timer's post-decrement == 0
bool timerOut(TownBirdFields& f) {
    const int16_t t = f.timer;
    f.timer = int16_t(t - 1);
    return t == 0;
}
void birdSub(AnimatedActor& actor, BirdSub s) {
    actor.subState = uint8_t(s);
    actor.step = 0;
}
void toThink(AnimatedActor& actor) { birdSub(actor, BirdSub::Think); }

// exe 0x45ece0: the followed actor in state 1 within 0x800 -> follow it
bool birdFollowCheck(AnimatedActor& actor, TownBirdFields& f) {
    if (f.followSlot == 0) return false;
    const Actor& o = actorAt(actorBySlot(f.followSlot));
    if (o.state != 1 || uint32_t(distance3(actor.pos, o.pos)) > 0x800u) return false;
    birdSub(actor, BirdSub::Follow);
    return true;
}
// exe 0x45eca0: the player in view (+-0x580) within 0x400 -> turn away
void birdPlayerNear(AnimatedActor& actor) {
    if (facingAndRange(actor, player(), 0x580, 0x400) == 0 && actor.variant != kVariantTame)
        birdSub(actor, BirdSub::TurnAway);
}
// exe 0x45dc30: what next (follow, idle, wander, wander a while turned round)
void birdThink(AnimatedActor& actor, TownBirdFields& f) {
    const uint32_t r = rngStep() & 0xffff;
    actor.step = 0;
    actor.stepCounter = 0;
    if (birdFollowCheck(actor, f)) return;
    if ((r & 7) == 0) {
        actor.subState = uint8_t(BirdSub::Idle);
        return;
    }
    f.side = coin(r >> 3);
    actor.subState = uint8_t(BirdSub::Wander);
    if (r & 0x7f80) return;
    actor.subState = uint8_t(BirdSub::Wander);
    actor.step = uint8_t(WanderStep::Walk);
    actor.velocity.z = 0;
    actor.velocity.y = 0;
    actor.velocity.x = 0;
    birdAnim(actor, f, kAnimBirdWalk);
    f.timer = int16_t(wanderTicks[rngStep() & 0xf]);
    f.side = coin(rngStep());
    f.heading = int16_t(uint16_t(uint16_t(actor.rotation.y) + sideHalfTurn(f)));
}
// exe 0x45dd50: pecking / looking a while
void birdIdle(AnimatedActor& actor, TownBirdFields& f) {
    if (actor.step == 0) {
        actor.velocity.setXY(0, 0);
        actor.velocity.z = 0;
        birdAnim(actor, f, (rngStep() & 1) ? kAnimBirdPeck : kAnimBirdLook);
        f.timer = int16_t(int16_t(idleTicks[rngStep() & 0xf]) << 1);
        ++actor.step;
    }
    if ((uint16_t(f.timer) & 0x1f) == 0) sound(kSoundPeck, actor);
    if (timerOut(f)) toThink(actor);
    if (birdFollowCheck(actor, f)) return;
    birdPlayerNear(actor);
    birdMoveByVelocity(actor);
}
// exe 0x45de30: a walk toward a random heading (turned round off a wall)
void birdWander(AnimatedActor& actor, TownBirdFields& f) {
    switch (WanderStep(actor.step)) {
    case WanderStep::Start: {
        actor.velocity.x = 0;
        actor.velocity.y = 0;
        actor.velocity.z = int16_t(0xffe0);
        birdAnim(actor, f, kAnimBirdWalk);
        f.timer = int16_t(wanderTicks[rngStep() & 0xf]);
        f.side = coin(rngStep());
        int16_t d = int16_t(rngStep() & 0x1ff);
        if (f.side == 0) d = t16(-d);
        f.heading = int16_t(uint16_t(uint16_t(actor.rotation.y) + uint16_t(d)));
        ++actor.step;
        break;
    }
    case WanderStep::Walk:
        if (actor.wallHit & 0xf00) { // bumped into something: turn round
            f.heading = int16_t(uint16_t((uint16_t(actor.rotation.y) + sideHalfTurn(f)) & 0xfff));
            f.timer = int16_t(uint16_t(uint16_t(f.timer) + 0x40));
            ++actor.step;
        }
        break;
    default: break;
    }
    const int d = turnYawTo(actor.rotation.y, uint32_t(int32_t(f.heading)), 0x20); // exe 0x5e3df0
    if (iabs(d) > 0x20) actor.anim = d > 0 ? kAnimBirdTurnRight : kAnimBirdTurnLeft;
    else actor.anim = kAnimBirdWalk;
    f.animKey = uint8_t(actor.animKey.frame);
    if ((uint16_t(f.timer) & 0x1f) == 0 && (rngStep() & 2)) sound(kSoundPeck, actor);
    if (timerOut(f)) toThink(actor);
    birdPlayerNear(actor);
    birdMoveByVelocity(actor);
}
// a heading picked by the side byte: straight, or 0x200 one way or the other
int16_t sidestep(uint8_t k, int from) {
    if ((k & 6) == 0) return int16_t(uint16_t(from));
    if (k & 1) return int16_t(uint16_t(from - 0x200));
    return int16_t(uint16_t(from + 0x200));
}
// exe 0x45e010: flees (variant 2: back to thinking)
void birdFlee(AnimatedActor& actor, TownBirdFields& f) {
    if (actor.variant == kVariantTame) {
        toThink(actor);
        return;
    }
    switch (FleeStep(actor.step)) {
    case FleeStep::Start: {
        actor.velocity.z = int16_t(0xfc40);
        actor.velocity.x = 0;
        actor.velocity.y = 0;
        actor.acceleration.z = int16_t(0xffd8);
        birdAnim(actor, f, kAnimBirdFlap);
        f.timer = int16_t(fleeTicks[rngStep() & 0xf]);
        const uint8_t n = uint8_t(actor.stepCounter != 0 ? FleeStep::Onward : FleeStep::Away);
        actor.stepCounter = 0;
        actor.step = n;
        break;
    }
    case FleeStep::Away: { // away from the player, maybe +-0x200
        const int ang = angleOf(player().pos, actor.pos) + 0x800;
        const uint8_t k = uint8_t(rngStep() & 7);
        f.side = k;
        f.heading = sidestep(k, ang);
        actor.step = uint8_t(FleeStep::Turn);
        break;
    }
    case FleeStep::Onward: {
        const uint8_t k = uint8_t(rngStep() & 7);
        f.side = k;
        f.heading = sidestep(k, uint16_t(actor.rotation.y));
        actor.step = uint8_t(FleeStep::Turn);
        actor.stepCounter = 1;
        break;
    }
    case FleeStep::Turn:
    case FleeStep::TurnFast: {
        const int step = FleeStep(actor.step) == FleeStep::Turn ? 0x20 : 0x80;
        if (iabs(turnToward(actor.rotation.y, int(f.heading), step)) < step) {
            actor.velocity.z = int16_t(0xfd80);
            actor.step = uint8_t(FleeStep::Run);
        }
        break;
    }
    case FleeStep::Run:
        if (actor.wallHit & 0xf00) { // a wall: look for a way round (three probes 0x30 ahead)
            const int ang = angleOf(player().pos, actor.pos) + 0x800;
            f.side = uint8_t(1u << (rngStep() & 3));
            actor.step = uint8_t(FleeStep::Turn);
            uint32_t freeDirs = 0;
            if (!(probePushOutAhead(*E, actor, actor.rotation.y + 0x200, 0x30) & 0xf00)) freeDirs = 1;
            if (!(probePushOutAhead(*E, actor, actor.rotation.y, 0x30) & 0xf00)) freeDirs |= 2;
            if (!(probePushOutAhead(*E, actor, actor.rotation.y - 0x200, 0x30) & 0xf00)) freeDirs |= 4;
            if (freeDirs == 0) {
                actor.velocity.z = int16_t(0xffb0);
                actor.step = uint8_t(FleeStep::TurnFast);
                f.timer = 1;
                f.heading = int16_t(uint16_t(((f.side & 4) ? 0x700 : -0x700) + actor.rotation.y));
            } else if (freeDirs == 5) { // hop back over
                actor.rotation.y = int16_t(uint16_t(uint16_t(actor.rotation.y) + 0x800));
                actor.velocity.y = int16_t(0xfe00);
                actor.velocity.z = int16_t(0xff00);
                actor.airborne = 1;
                f.heading = actor.rotation.y;
                birdAnim(actor, f, kAnimBirdTumble);
                actor.state = uint8_t(BirdState::Free);
                actor.subState = uint8_t(BirdSub::Knocked);
                actor.step = uint8_t(KnockStep::Fly);
                f.heading = int16_t(uint16_t(ang));
            } else if (freeDirs & 1) {
                f.heading = int16_t(uint16_t(actor.rotation.y + 0x300));
            } else if (freeDirs & 4) {
                f.heading = int16_t(uint16_t(actor.rotation.y - 0x300));
            } else {
                f.heading = int16_t(uint16_t(ang));
            }
        } else {
            if (timerOut(f)) {
                f.timer = int16_t(fleeTicks[rngStep() & 0xf]);
                actor.step = uint8_t(FleeStep::Onward);
            }
            if (actor.stepCounter != 0 && planarDistance(actor.pos, player().pos) > 0xc00)
                actor.step = uint8_t(FleeStep::Slow);
        }
        break;
    case FleeStep::Slow: // slows down, picks a random heading
        actor.velocity.z = int16_t(uint16_t(uint16_t(actor.velocity.z) - uint16_t(actor.acceleration.z)));
        if (actor.velocity.z >= -0x20) {
            actor.velocity.z = int16_t(0xffe0);
            f.heading = int16_t(rngStep() & 0xfff);
            ++actor.step;
        }
        break;
    case FleeStep::Settle:
        if (iabs(turnToward(actor.rotation.y, int(f.heading), 0x80)) < 0x80) toThink(actor);
        break;
    default: break;
    }
    if ((gameState().frame & 0xf) == 0 && (rngStep() & 1)) sound(kSoundFlap, actor);
    const uint8_t n = actor.step; // the byte table 0x45e524 (step - 1 -> 0: feathers): steps 1, 2, 4, 10
    if ((n == 1 || n == 2 || n == 4 || n == 10) && (gameState().frame & 1)) feathers(actor);
    actor.rotation.y &= 0xfffu;
    f.heading &= 0xfffu;
    birdMoveByVelocity(actor);
}
// exe 0x45e530: the player came close: faces it, then turns away (-> flee)
void birdTurnAway(AnimatedActor& actor, TownBirdFields& f) {
    switch (TurnAwayStep(actor.step)) {
    case TurnAwayStep::Start:
        stopMotion(actor);
        birdAnim(actor, f, kAnimBirdWalk);
        f.heading = int16_t(uint16_t(angleOf(player().pos, actor.pos)));
        sound(kSoundSquawk, actor);
        ++actor.step;
        return;
    case TurnAwayStep::Face: {
        const int d = turnYawTo(actor.rotation.y, uint32_t(int32_t(f.heading)), 0x180);
        if (iabs(d) > 0x180) {
            actor.anim = d > 0 ? kAnimBirdTurnRight : kAnimBirdTurnLeft;
            f.animKey = uint8_t(actor.animKey.frame);
            return;
        }
        birdAnim(actor, f, kAnimBirdStartle);
        ++actor.step;
        return;
    }
    case TurnAwayStep::Startle:
        if (actor.animKey.flags != kKeyHold) return;
        birdAnim(actor, f, kAnimBirdWalk);
        f.side = coin(rngStep());
        f.heading = int16_t(uint16_t(angleOf(player().pos, actor.pos) + int((f.side ? 0x7ffu : 0x801u))));
        ++actor.step;
        return;
    case TurnAwayStep::TurnAway: {
        const int d = turnYawTo(actor.rotation.y, uint32_t(int32_t(f.heading)), 0x180);
        if (iabs(d) > 0x180) {
            actor.anim = d > 0 ? kAnimBirdTurnRight : kAnimBirdTurnLeft;
            f.animKey = uint8_t(actor.animKey.frame);
            return;
        }
        actor.subState = uint8_t(BirdSub::Flee);
        actor.enterStep(0);
        return;
    }
    default: return;
    }
}
// exe 0x45e6f0: hit -- knocked back along the hit's direction, lands, -> flee
void birdKnocked(AnimatedActor& actor, TownBirdFields& f) {
    switch (KnockStep(actor.step)) {
    case KnockStep::Hit:
        actor.velocity.x = 0;
        actor.velocity.y = int16_t(0xfe00);
        actor.velocity.z = int16_t(0xff00);
        birdAnim(actor, f, kAnimBirdTumble);
        f.heading = int16_t(uint16_t(f.hitBox.contact.x) & 0xfffu);
        ++actor.step;
        break;
    case KnockStep::Fly:
        if (actor.airborne == 0 && actor.animEnded()) {
            birdAnim(actor, f, kAnimBirdLand);
            actor.velocity.y = 0;
            actor.velocity.z = 0;
            ++actor.step;
        }
        break;
    case KnockStep::Done:
        f.hitsTaken = 0;
        f.bits &= uint8_t(~kBirdHurt);
        actor.subState = uint8_t(BirdSub::Flee);
        actor.enterStep(0);
        break;
    default: break;
    }
    moveAlongYaw(actor.pos, f.heading, 0, actor.velocity.y, actor.velocity.z); // exe 0x5e36f0
}
// exe 0x45e7a0: hurt again -- knocked higher (by the player's forward speed)
void birdKnockedHard(AnimatedActor& actor, TownBirdFields& f) {
    switch (KnockStep(actor.step)) {
    case KnockStep::Hit:
        actor.velocity.x = 0;
        actor.velocity.z = int16_t(0xff00);
        actor.anim = kAnimBirdTumble;
        actor.velocity.y = int16_t(uint16_t(uint16_t(player().velocity.z >> 2) - 0x100));
        f.heading = int16_t(uint16_t(f.hitBox.contact.x) & 0xfffu);
        f.animKey = 0;
        ++actor.step;
        break;
    case KnockStep::Fly:
        if (actor.airborne == 0 && actor.animEnded()) {
            birdAnim(actor, f, kAnimBirdLand);
            actor.velocity.z = int16_t(actor.velocity.z >> 1);
            actor.velocity.y = 0;
            ++actor.step;
        }
        break;
    case KnockStep::Done:
        if (actor.animKey.flags == kKeyHold) {
            f.hurtTicks = 8;
            f.bits &= uint8_t(~kBirdHurt);
            f.hitsTaken = 0;
            actor.subState = uint8_t(BirdSub::Flee);
            actor.step = 0;
            actor.stepCounter = 1;
        }
        break;
    default: break;
    }
    moveAlongYaw(actor.pos, f.heading, 0, actor.velocity.y, actor.velocity.z);
}
// exe 0x45e880: walks to the actor it follows; close -> it marks that actor
void birdFollow(AnimatedActor& actor, TownBirdFields& f) {
    if (f.followSlot == 0) {
        toThink(actor);
        return;
    }
    const uint32_t address = actorBySlot(f.followSlot);
    if (!address) return;
    const Actor& o = actorAt(address);
    FollowedFields followed(actorHeaderAt(address));
    switch (FollowStep(actor.step)) {
    case FollowStep::Start:
        actor.velocity.x = 0;
        actor.velocity.y = 0;
        actor.velocity.z = int16_t(0xffc0);
        birdAnim(actor, f, kAnimBirdWalk);
        ++actor.step;
        [[fallthrough]];
    case FollowStep::Walk: {
        const int ang = angleOf(o.pos, actor.pos);
        f.heading = int16_t(uint16_t(ang));
        turnYawTo(actor.rotation.y, uint32_t(int32_t(int16_t(ang))), 0x40);
        birdPlayerNear(actor);
        if (uint32_t(distance3(actor.pos, o.pos)) > 0x48u) break;
        followed.birdBits |= 2u;
        followed.birdWord = 0x100;
        stopMotion(actor);
        actor.rotation.y = int16_t(uint16_t(angleOf(o.pos, actor.pos)));
        birdAnim(actor, f, kAnimBirdLook);
        ++actor.step;
        [[fallthrough]];
    }
    case FollowStep::Arrived: followed.birdArrived = 1; break;
    default: break;
    }
    birdMoveByVelocity(actor);
}
// exe 0x45ef10: a hit taken -> -1 (sound 0x8d once, the hit points, the balance penalty, the hurt timer)
int birdHitReact(AnimatedActor& actor, TownBirdFields& f) {
    const uint8_t b = f.bits;
    if (b & (kBirdHurtHard | kBirdHurt)) {
        const int16_t t = f.hurtTicks;
        if (t != 0) {
            f.hurtTicks = int16_t(t - 1);
            return 0;
        }
        f.bits = uint8_t(b & ~kBirdHurtHard);
        if (f.hitsTaken >= 4) return 0;
    }
    const uint32_t hit = f.hitBox.result;
    const uint32_t dmg = hit & 0xfff;
    if (dmg == 0) return 0;
    if (!(hit & 0x80000) && (hit & 0x7000000)) {
        if (actor.variant == kVariantTame) return 0;
        if (f.bits & kBirdHurt) return 0;
        f.bits |= uint8_t(kBirdHurtHard | kBirdHurt);
        sound(kSoundHurt, actor);
    }
    hitPointsAdd(actor, -int(dmg)); // exe 0x5e52f0
    f.bits |= kBirdHurt;
    if (hit & 0x480000) balanceAdd(-300); // exe 0x5e51d0
    else if (hit & 0x40000) balanceAdd(-500);
    f.hurtTicks = (f.bits & kBirdHurtHard) ? 0x14 : 9;
    if (actor.variant != kVariantTame) ++f.hitsTaken;
    return -1;
}
// exe 0x45ee10: the mover queue, the wall push-out (bounces off walls while airborne), gravity / landing; -1 = left
// the ground
int birdMove(AnimatedActor& actor, TownBirdFields& f) {
    actorCollisionSubmitMover(actor); // exe 0x4140b0
    actor.wallHit = 0;
    if (int32_t(actorTestMove(*E, actor)) < 0) { // exe 0x40f570
        const uint32_t r = actorPushOut(*E, actor); // exe 0x410960
        actor.wallHit = r;
        if (int32_t(r) < 0) actor.pos = actor.prevPos;
        if ((r & 0xf00) && (f.bits & kBirdAirborne)) {
            reflectRotationOffWalls(actor.rotation, r); // exe 0x410740
            f.bits &= uint8_t(~kBirdAirborne);
            actor.velocity.z = int16_t(actor.velocity.z >> 1);
        }
        if ((actor.wallHit & 0x2000) && actor.velocity.y < 0) actor.velocity.y = int16_t(-int(actor.velocity.y >> 2));
    }
    const int g = actorGround(*E, actor); // exe 0x40f190
    if (actor.airborne != 0) {
        const int16_t vy = actor.velocity.y;
        if (vy > 0 && actor.pos.y.whole >= g) {
            actor.velocity.y = 0;
            actor.pos.y.whole = int16_t(g);
            actor.airborne = 0;
            return 0;
        }
        actor.velocity.y = int16_t(uint16_t(uint16_t(actor.acceleration.y) + uint16_t(vy)));
        return 0;
    }
    if (g - actor.pos.y.whole > 0x10) {
        f.bits |= kBirdAirborne;
        actor.airborne = 1;
        return -1;
    }
    actor.pos.y = int32_t(uint32_t(g) << 16);
    return 0;
}
// exe 0x45ed30: the hit-list entry at bone 0 (variant 2 stronger / always "watched")
void birdHitEntry(AnimatedActor& actor, TownBirdFields& f) {
    uint32_t b = 0, c = 0x40000;
    f.hitBox.previousX = actor.prevPos.x.whole;
    f.hitBox.previousZ = actor.prevPos.z.whole;
    f.hitBox.result = 0;
    if (actor.variant == kVariantTame) {
        c = 0xc0000;
        b = 0x80000;
    }
    if (f.bits & kBirdWatched) {
        if (facingAndRange(player(), actor, 0x200, 0xc00) == 0) f.bits &= uint8_t(~kBirdWatched);
        else b |= 0x80000;
    }
    const uint8_t bits = f.bits;
    if (bits & kBirdLifted) return;
    const AnimatedActorStats rec = animatedStats(actor.type);
    uint32_t p1, p2;
    if (!(bits & kBirdThrown)) {
        p1 = (((uint32_t(actor.slot) << 15) | rec.hitHeight) << 9) | b | 0x50800au;
        p2 = 0x20000;
    } else {
        p1 = (uint32_t(actor.slot) << 24) | 0x28800au;
        p2 = uint32_t(rec.bodyAttack) | c;
    }
    const ShortVec4* bp = bonePointOrNull(actor, 0); // exe 0x5e1810
    if (bp == 0) notPorted(kStoryPort, "class 0x45d970: exe 0x5e1810 returned null");
    const ShortVec4& p = *bp;
    const int16_t pos[3] = {p.x, p.y, p.z};
    hitListSubmit(pos, f.hitBox, p1, p2); // exe 0x5e4120
}
// exe 0x45daa0
void birdInit(AnimatedActor& actor, TownBirdFields& f) {
    if (!bindActorModel(actor, 0)) { // exe 0x5e0ba0
        freeActor(actor);
        return;
    }
    actor.clearClassBlock();
    actor.anim = kAnimBirdPeck;
    actor.prevAnim = kNoAnim;
    actor.rotation.z = 0;
    actor.rotation.x = 0;
    actor.acceleration.z = 0;
    actor.acceleration.x = 0;
    actor.acceleration.y = 0x40;
    const uint16_t hp = hasAnimatedStats(actor.type) ? uint16_t(animatedStats(actor.type).hitPoints) : 0;
    actor.maxHitPoints = int16_t(hp);
    actor.hitPoints = int16_t(hp);
    actor.interaction = kInteractLift;
    actor.collisionBox.pointAt(TableId::CalbaniaCastBirdBox);
    actor.interactionBox.pointAt(TableId::CalbaniaCastBirdLiftBox);
    if (actor.variant == 1) actor.interaction = kInteractTalk | kInteractLift;
    actor.airborne = 0;
    actor.onPlatform = 0;
    actor.wallHit = 0;
    actor.actorHit = 0;
    f.homeX = actor.pos.x.whole;
    f.homeZ = actor.pos.z.whole;
    f.bits |= kBirdWatched;
    actor.enterState(uint8_t(BirdState::Free));
}
// exe 0x45db70
void birdFree(AnimatedActor& actor, TownBirdFields& f) {
    if (birdHitReact(actor, f) != 0) {
        if (f.hits < 0x7fff) f.hits = int16_t(uint16_t(uint16_t(f.hits) + 1));
        sound(kSoundSquawk, actor);
        birdSub(actor, (f.bits & kBirdHurtHard) ? BirdSub::KnockedHard : BirdSub::Knocked);
    }
    switch (BirdSub(actor.subState)) { // [0x88f9b8]
    case BirdSub::Think: birdThink(actor, f); break;
    case BirdSub::Idle: birdIdle(actor, f); break;
    case BirdSub::Wander: birdWander(actor, f); break;
    case BirdSub::Flee: birdFlee(actor, f); break;
    case BirdSub::TurnAway: birdTurnAway(actor, f); break;
    case BirdSub::Knocked: birdKnocked(actor, f); break;
    case BirdSub::KnockedHard: birdKnockedHard(actor, f); break;
    case BirdSub::Follow: birdFollow(actor, f); break;
    default: notPortedAt(kStoryPort, "class 0x45d970 sub-state", actor.subState);
    }
    birdMove(actor, f);
    if (playerLiftTargetCheck(actor, 0)) { // exe 0x573a60
        actor.enterState(uint8_t(BirdState::Lifted));
        f.bits |= kBirdLifted;
    }
    if (player().talksTo(actor) && eventState().active == 0) { // exe 0x573fa0
        eventStart(&actor.pos, &actor.rotation, 0x10000, 0, 0xffff); // exe 0x543af0(actor, 0x10000, 0, -1)
        actor.enterState(uint8_t(BirdState::Vanish));
    }
}
void birdThrown(AnimatedActor& actor, TownBirdFields& f);
// exe 0x45e9b0: in the player's hands (clucking), thrown / dropped -> exe 0x45eb30
void birdHeld(AnimatedActor& actor, TownBirdFields& f) {
    if (actor.step == 0) {
        actor.anim = kAnimBirdHeld;
        sound(kSoundSquawk, actor);
        f.animKey = 0;
        f.timer = int16_t((rngStep() & 7) + 0xc);
        ++actor.step;
    }
    carryAtHands(actor, 0, 0, 0); // exe 0x573ab0
    if (timerOut(f)) {
        sound(kSoundPeck, actor);
        f.timer = int16_t((rngStep() & 7) + 0xc);
    }
    const int rel = int(carryReleased()); // exe 0x573950
    if (rel == 0) return;
    f.bits &= uint8_t(~kBirdLifted);
    if (rel == 1) {
        sound(kSoundSquawk, actor);
        int32_t v[3];
        playerAimVector(0, -0x180, -0x400, v); // exe 0x5e3e50
        actor.velocity.x = 0;
        actor.velocity.y = int16_t(v[1]);
        actor.velocity.z = int16_t(v[2]);
        f.bits |= kBirdThrown;
    } else {
        actor.velocity.y = int16_t(0xfe00);
        actor.velocity.z = int16_t(0xff00);
    }
    actor.rotation.y = player().rotation.y;
    actor.airborne = 1;
    carryDropFromPlayer(actor); // exe 0x573990
    birdAnim(actor, f, kAnimBirdTumble);
    actor.subState = uint8_t(LiftedSub::Thrown);
    actor.enterStep(0);
    birdThrown(actor, f);
}
// exe 0x45eb30: flying (bounces off walls, a hit while flying turns it round), lands
void birdThrown(AnimatedActor& actor, TownBirdFields& f) {
    const uint32_t r = actor.actorHit;
    if ((r & 0xf00) && (f.bits & kBirdAirborne)) {
        reflectRotationOffWalls(actor.rotation, r);
        f.bits &= uint8_t(~kBirdAirborne);
        actor.velocity.z = int16_t(actor.velocity.z >> 1);
    }
    switch (ThrownStep(actor.step)) {
    case ThrownStep::Fly:
        if (f.hitBox.result & 0x20000) {
            actor.rotation.y = int16_t(uint16_t(uint16_t(actor.rotation.y) + 0x800));
            spawnHitSparkWithoutSize(kActorSweepGroup2, actor.pos.x.whole, actor.pos.y.whole, actor.pos.z.whole); // exe 0x5e5400
        }
        if (actor.airborne == 0) {
            actor.velocity.y = 0;
            actor.velocity.z = int16_t(actor.velocity.z >> 1);
            if (actor.animEnded()) {
                birdAnim(actor, f, kAnimBirdLand);
                ++actor.step;
            }
        }
        break;
    case ThrownStep::Land:
        actor.velocity.y = 0;
        actor.velocity.z = int16_t(actor.velocity.z >> 1);
        if (actor.animKey.flags == kKeyHold) {
            actor.enterState(uint8_t(BirdState::Free));
            f.bits &= 0x8fu;
            f.hitsTaken = 0;
            actor.subState = uint8_t(BirdSub::Flee);
            actor.enterStep(0);
        }
        break;
    default: break;
    }
    moveAlongYaw(actor.pos, actor.rotation.y, 0, actor.velocity.y, actor.velocity.z);
    f.bits |= kBirdAirborne;
    birdMove(actor, f);
}
// exe 0x45ec40 (after its event): feathers, a squawk, freed next tick
void birdVanish(AnimatedActor& actor) {
    feathers(actor);
    sound(kSoundSquawk, actor);
    actor.state = uint8_t(BirdState::Freed);
}
// exe 0x45d970's body
void townBird(AnimatedActor& actor) {
    TownBirdFields& f = classState<TownBirdFields>(actor);
    actor.prevAnim = actor.anim;
    actor.savePreviousPose();
    actor.savePreviousContact();
    int v = 0x10;
    const int16_t hits = f.hits;
    if (hits > 0x10) {
        int k = (int(hits) - 0x10) >> 2;
        if (k >= 0xf) k = 0xe;
        v = 0x10 - k;
    }
    actor.tint = uint16_t((((uint32_t(v) << 5) | uint32_t(v)) << 5) | uint32_t(v));
    f.bits &= uint8_t(~kBirdAirborne);
    if (f.followSlot != 0 && actorHeaderAt(actorBySlot(f.followSlot)).flags == 0) f.followSlot = 0;
    switch (BirdState(actor.state)) { // [0x88f9a4]
    case BirdState::Init: birdInit(actor, f); break;
    case BirdState::Free: birdFree(actor, f); break;
    case BirdState::Lifted:
        if (LiftedSub(actor.subState) == LiftedSub::Held) birdHeld(actor, f);
        else if (LiftedSub(actor.subState) == LiftedSub::Thrown) birdThrown(actor, f);
        else notPortedAt(kStoryPort, "class 0x45d970 lifted sub-state", actor.subState);
        break;
    case BirdState::Vanish: birdVanish(actor); break;
    case BirdState::Freed: freeActor(actor); break; // exe 0x468fa0
    default: notPortedAt(kStoryPort, "class 0x45d970 state", actor.state);
    }
    if (!(actor.flags & kActorLive)) return;
    if (actor.anim == actor.prevAnim) animStepPose(actor);  // exe 0x5e1e80
    else animSetPose(actor, actor.anim, f.animKey);         // exe 0x5e1c30
    // exe 0x5f20f0(actor, 0x38, 0xa0, 0) unless held: the shadow list entry (render output)
    birdHitEntry(actor, f);
}

constexpr int16_t kInFrontOfPlayer = -0x490;
void standingCast(AnimatedActor& actor) {
    StandingCastFields& f = classState<StandingCastFields>(actor);
    castSavePose(actor);
    const uint8_t v = actor.variant;
    if (actor.state == 0 && v <= 1) {
        if (!bindActorModel(actor, 0)) { // exe 0x5e0ba0
            freeActor(actor);
            return;
        }
        actor.clearClassBlock();
        actor.collisionBox.pointAt(TableId::CalbaniaCastStandingCastBox);
        const int16_t scale = v == 0 ? 0x1a0 : 0x166;
        actor.scale.x = scale;
        actor.scale.y = scale;
        actor.scale.z = scale;
        actor.anim = v == 0 ? 3 : 0xb;
        actor.prevAnim = kNoAnim;
        if (v == 0) {
            f.animKey = f.recordKey;
        } else {
            f.animKey = 0;
            int32_t o[3];
            rotateByYaw(player().rotation.y, 0, 0, kInFrontOfPlayer, o); // exe 0x5e38d0
            actor.pos = player().pos;
            actor.pos.y.whole = int16_t(uint16_t(uint16_t(actor.pos.y.whole) - 0x73));
            actor.pos.x = int32_t(uint32_t(actor.pos.x) + uint32_t(o[0]));
            actor.pos.z = int32_t(uint32_t(actor.pos.z) + uint32_t(o[2]));
            actor.rotation.y = int16_t(uint16_t((uint16_t(player().rotation.y) - 0x800) & 0xfff));
        }
        ++actor.state;
    } else if (v > 1) {
        notPortedAt(kStoryPort, "class 0x45f010 variant", v); // [0x88f9fc] has two entries
    }
    if (!(actor.flags & kActorLive)) return;
    if (v == 0) {
        actor.pos.y.whole = int16_t(actorGround(*E, actor)); // exe 0x40f190
        shadow(actor);                                         // exe 0x5f20f0(actor, 0x30, 0xa0, 0)
    }
    if (actor.anim != actor.prevAnim) animSetPose(actor, actor.anim, f.animKey); // exe 0x5e1c30
    else animStepPose(actor);                                                   // exe 0x5e1e80
}

} // namespace

void appoDahPuppetUpdate(const PlayerEnv& env, uint32_t actor) {
    E = &env;
    walkingPuppet(animatedActorAt(actor));
}

void sittingPuppetUpdate(const PlayerEnv& env, uint32_t actor) {
    E = &env;
    sittingPuppet(animatedActorAt(actor));
}

void townBirdUpdate(const PlayerEnv& env, uint32_t actor) {
    E = &env;
    townBird(animatedActorAt(actor));
}

void townStandingCastUpdate(const PlayerEnv& env, uint32_t actor) {
    E = &env;
    standingCast(animatedActorAt(actor));
}

} // namespace od::port
