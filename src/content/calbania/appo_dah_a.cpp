// Story 8 (Calbania): Shu's brothers Appo and Dah outside the town.
//   The guide, class exe 0x4c4980 (pool 0x20 type 0x43 sub 0; ST21 / ST22 records 0x88ad54 / 0x88ad68 / 0x88adb8 /
//   0x88adcc from the e0 spawners 0x41f850 / 0x41f940, variants 0 / 1 = Appo / Dah): they run ahead along a node path
//   of the field (path 0: 0x894d60, 5 nodes; path 1: 0x894db8, 9 nodes; per node s16 x[2] / z[2] by variant, a choice
//   mask and the next nodes), wait jumping and shouting (sounds 0x1ab / 0x1af) when the player falls behind, come back
//   when the player is too far, and at the last node step to the exit point. Hits knock them back, they can be lifted,
//   carried (0x1ae / 0x1b2) and thrown.
//   The gate puppets, class exe 0x4c6650 (pool 0x20 type 0x43 sub 2, ST1F records 0x8a3640 / 0x8a3654 of scripts 0x1e /
//   0x1f): the script puppets of the square (the variant routine [0x894ea0 + +0xc * 4], run when the step tick moves).
// Transcribed from dash2.exe one function per exe function (conventions as game/story.cpp). The shadow (exe 0x5f20f0)
// is render output.
#include <cstdint>

#include "content/calbania/appo_dah_face.hpp"
#include "content/calbania/appo_dah_a_state.hpp"
#include "content/calbania/ch3_story.hpp"
#include "game/actor_animation.hpp"
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
#include "game/encounter.hpp"
#include "game/enemy_neighbours.hpp"
#include "game/game_state.hpp"
#include "game/hit_effects.hpp"
#include "game/hit_list.hpp"
#include "game/player/player_motion.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/rewards.hpp"
#include "game/script_state.hpp"
#include "game/story.hpp"
#include "game/story_flags.hpp"
#include "game/territory.hpp"
#include "game/vectors.hpp"

namespace od::port {

using namespace appo_dah_a_state;

namespace {

using address_forms::E;

constexpr int8_t kShortPathLast = 5, kLongPathLast = 9;

// collision bits (+0x165)
constexpr uint8_t kGuideWallX = 0x01, kGuideWallZ = 0x02, kGuideFloor = 0x04, kGuideGroundLost = 0x08;
constexpr uint8_t kGuideWalls = kGuideWallX | kGuideWallZ;
// flags (+0x166)
constexpr uint8_t kGuideNoHitEntry = 0x01;
constexpr uint8_t kGuideNoHits = 0x02;
constexpr uint8_t kGuideNoHop = 0x04;     // a wall does not make it hop
constexpr uint8_t kGuideNoFall = 0x08;    // the ground lost does not make it fall
constexpr uint8_t kGuideThrown = 0x10;    // its hit entry is a thrown body's
constexpr uint8_t kGuideKeepGround = 0x20; // the ground is not lost (it follows it)
constexpr uint8_t kGuideReflect = 0x40;   // bounces off the walls
constexpr uint8_t kGuideJumping = kGuideNoFall;
constexpr uint8_t kGuideFalling = kGuideReflect | kGuideNoFall | kGuideNoHop | kGuideNoHitEntry;
constexpr uint8_t kGuideKnocked = kGuideReflect | kGuideNoFall | kGuideNoHop | kGuideNoHits;
constexpr uint8_t kGuideHopping = kGuideNoFall | kGuideNoHop;
constexpr uint8_t kGuideCarried = kGuideReflect | kGuideNoHitEntry;
constexpr uint8_t kGuideRunning = kGuideNoHop | kGuideNoHitEntry;
constexpr uint8_t kGuideKnockedAtExit = kGuideNoHop | kGuideNoHits;

enum class GuideState : uint8_t { Init = 0, Guide = 1, AtExit = 2, Carried = 3 }; // [0x894e3c]
enum class GuideSub : uint8_t { Start = 0, Run = 1, WaitForPlayer = 2, AtLastNode = 3, Pause = 4, Fall = 5,
                                KnockedBack = 6, HopWall = 7 };      // [0x894e4c]
enum class ExitSub : uint8_t { Start = 0, Stand = 1, RunToExit = 2, Knocked = 3 }; // [0x894e6c]
enum class CarriedSub : uint8_t { Held = 0, Flying = 1 };            // [0x894e7c]
enum class RunStep : uint8_t { Pick = 0, Run = 1, Return = 2 };
enum class JumpStep : uint8_t { Crouch = 0, Jump = 1, Air = 2, Landed = 3 };
enum class KnockStep : uint8_t { Hit = 0, Fly = 1, Slide = 2, Land = 3, GetUp = 4, Done = 5 };
enum class HopStep : uint8_t { Crouch = 0, Jump = 1, Air = 2, Done = 3 };
enum class ExitRunStep : uint8_t { Start = 0, Run = 1, Arrived = 2 };
enum class ExitKnockStep : uint8_t { Hit = 0, Land = 1, GetUp = 2, Done = 3 };
enum class FlyStep : uint8_t { Fly = 0, Slide = 1, Land = 2, GetUp = 3 };

constexpr uint8_t kAnimIdle = 0, kAnimStand = 1, kAnimRun = 2, kAnimCarried = 3, kAnimCarriedHurt = 4;
constexpr uint8_t kAnimKnockedFront = 5, kAnimKnockedBack = 6, kAnimDownFront = 7, kAnimDownBack = 8,
                  kAnimUpFront = 9, kAnimUpBack = 0xa, kAnimPause = 0xd;
constexpr uint8_t kAnimCrouch = 0x20, kAnimJump = 0x21, kAnimLand = 0x22;
constexpr uint8_t kNoAnim = 0xff;
constexpr uint16_t kSoundBounce = 0x147, kSoundLand = 0x146, kSoundWall = 0x148;
constexpr uint16_t kShoutAppo = 0x1ab, kShoutDah = 0x1af, kHurtAppo = 0x1ad, kHurtDah = 0x1b1, kCarriedAppo = 0x1ae,
                   kCarriedDah = 0x1b2;
constexpr int16_t kGuideGravity = 0x40;
constexpr int kBalanceLoss = -500;
constexpr uint32_t kHitDamageMask = 0xfff;
constexpr uint32_t kHitKnocks = 0x1e0000, kHitCostsBalance = 0x440000, kHitBump = 0x10000, kHitThrownHit = 0x20000;

uint16_t voiceOf(const AnimatedActor& actor, uint16_t appo, uint16_t dah) { return actor.variant == 0 ? appo : dah; }
void sound(uint16_t id, const AnimatedActor& actor) { soundPlayAtPos(id, actor.pos); } // exe 0x5d5380
// the sub-state s at its first step
void enterSub(AnimatedActor& actor, uint8_t s) {
    actor.subState = s;
    actor.enterStep(0);
}
template <class Sub> void enterSub(AnimatedActor& actor, Sub s) { enterSub(actor, uint8_t(s)); }
// a node's point by the variant, as the exe's stack vector (the whole parts only)
struct NodePoint {
    int16_t x, z;
};
NodePoint nodePoint(const AnimatedActor& actor, const GuideFields& f, int n) {
    const GuideNode node = f.node(n);
    return {node.x[actor.variant], node.z[actor.variant]};
}
int angleFromPoint(const NodePoint& p, const AnimatedActor& actor) {
    return angleOf(int32_t(uint32_t(uint16_t(p.x)) << 16), int32_t(uint32_t(uint16_t(p.z)) << 16), actor.pos.x,
                   actor.pos.z);
}
int planarDistanceTo(const Fixed16Vec3& from, const NodePoint& p) {
    const int32_t to[3] = {int32_t(uint32_t(uint16_t(p.x)) << 16), 0, int32_t(uint32_t(uint16_t(p.z)) << 16)};
    return planarDistance(from, to);
}
// the "within 0x40" test on the integer x / z of the point
bool near40(const AnimatedActor& actor, const NodePoint& p) {
    return uint16_t(uint16_t(actor.pos.x.whole) - uint16_t(p.x) + 0x40u) < 0x80 &&
           uint16_t(uint16_t(actor.pos.z.whole) - uint16_t(p.z) + 0x40u) < 0x80;
}
void facePlayer(AnimatedActor& actor, int step) {
    turnYawTo(actor.rotation.y, uint32_t(angleOf(player().pos, actor.pos)), step);
}

// the dust puff (pool 0xa0 type 0 variant 3) on the odd frames
void dust(const AnimatedActor& actor) {
    if (!(gameState().frame & 1)) return;
    const uint32_t address = allocActorA0(0); // exe 0x5e13b0
    if (!address) return;
    EffectActor& e = effectActorAt(address);
    e.flags = kActorLive;
    e.type = 0;
    e.variant = 3;
    e.loopFlags |= uint8_t(kActorSweepGroup2 | kActorLateLoop);
    e.pos.x = actor.pos.x.whole;
    e.pos.y = actor.pos.y.whole;
    e.pos.z = actor.pos.z.whole;
    e.setParamsLowWord(0x40); // the puff's size
}

// exe 0x4c5db0(actor, yaw): the arc along yaw; the floor hit while rising -> a bounce (sound 0x147), 1
int arc(AnimatedActor& actor, GuideFields& f, int yaw) {
    if (f.collision & kGuideFloor) {
        const int v = -int(actor.velocity.y);
        actor.velocity.z = int16_t(actor.velocity.z >> 1);
        actor.velocity.y = int16_t(v >> 1);
        actor.rotation.x = 0;
        actor.airborne = 1;
        sound(kSoundBounce, actor);
        return 1;
    }
    moveAlongYaw(actor.pos, yaw, 0, actor.velocity.y, actor.velocity.z); // exe 0x5e36f0
    actor.velocity.y += uint16_t(actor.acceleration.y);
    return 0;
}
// exe 0x4c5e20: a hit (the hit list's result) -> its direction, the flash (the balance -500 on 0x440000); 1
int hitTaken(AnimatedActor& actor, GuideFields& f) {
    const uint32_t r = f.hitBox.result;
    if ((r & kHitDamageMask) && (r & kHitKnocks)) {
        f.knockYaw = uint16_t(f.hitBox.contact.x);
        actor.tint = kTintFlash;
        if (r & kHitCostsBalance) balanceAdd(kBalanceLoss); // exe 0x5e51d0
        return 1;
    }
    if (r & kHitBump) {
        f.knockYaw = uint16_t(f.hitBox.contact.x);
        return 1;
    }
    return 0;
}
// exe 0x4c5e90: the walls / the territory (the collision bits), the ground; 1 = the ground lost (> 0x10 below)
int collide(AnimatedActor& actor, GuideFields& f) {
    f.collision = 0;
    if (int32_t(actorTestMove(*E, actor)) < 0) {          // exe 0x40f570
        const uint32_t r = actorPushOut(*E, actor);       // exe 0x410960
        if (int32_t(r) < 0) actor.pos = actor.prevPos;
        if (r & 0x300) f.collision |= kGuideWallX;
        if (r & 0xc00) f.collision |= kGuideWallZ;
        if (actor.velocity.y > 0 && (r & 0x3000)) f.collision |= kGuideFloor;
        if (f.flags & kGuideReflect) reflectRotationOffWalls(actor.rotation, r); // exe 0x410740
    } else if (const int z = territoryViolated(actor.pos)) {                      // exe 0x5e6ab0
        const uint32_t r = territoryPushBack(actor, z);                           // exe 0x5e6b40
        if (r & 0x300) f.collision |= kGuideWallX;
        if (r & 0xc00) f.collision |= kGuideWallZ;
    }
    const int g = actorGround(*E, actor); // exe 0x40f190
    if (actor.airborne != 0) return 0;
    if (!(f.flags & kGuideKeepGround) && g - actor.pos.y.whole > 0x10) {
        f.collision |= kGuideGroundLost;
        return 1;
    }
    actor.pos.y.whole = int16_t(g);
    return 0;
}
// exe 0x4c4cd0: standing (animation 1), the speeds 0, the scale 1.0
void stand(AnimatedActor& actor) {
    actor.anim = kAnimStand;
    actor.velocity.setXY(0, 0);
    actor.velocity.z = 0;
    actor.acceleration.z = 0;
    resetModelScale(actor); // exe 0x4e5cd0
}
// the turn-away of the knock-backs: the facing kept when the hit came from the front half (animation `front`), else
// turned round (+0x800, animation `back`); true when kept
bool knockFacing(AnimatedActor& actor, GuideFields& f, uint8_t front, uint8_t back, bool writeDir) {
    const uint16_t c = f.knockYaw;
    int32_t d = int32_t(uint16_t(actor.rotation.y) & 0xfff) - int32_t(c & 0xfff);
    if (uint32_t(d + 0x800) > 0x1000) d = (-d ^ 0xfff) + 1;
    if (uint32_t(d + 0x400) < 0x800) {
        actor.rotation.y = int16_t(c);
        if (writeDir) f.knockYaw = 0;
        actor.anim = front;
        return true;
    }
    actor.rotation.y = int16_t(uint16_t(c - 0x800));
    if (writeDir) f.knockYaw = 0x800;
    actor.anim = back;
    return false;
}
// exe 0x4c4cb0 (state 1 sub 0): stands, then runs to a node
void guideStart(AnimatedActor& actor, GuideFields& f) {
    stand(actor);
    f.flags = 0;
    enterSub(actor, GuideSub::Run);
}
// the player's lead over it on the way to the last node
int playerBehind(const AnimatedActor& actor, const GuideFields& f) {
    const NodePoint g = nodePoint(actor, f, f.lastNode);
    const int d1 = planarDistanceTo(actor.pos, g); // exe 0x5e3b00
    const int d2 = planarDistanceTo(player().pos, g);
    return d2 - d1;
}
// exe 0x4c4cf0 (state 1 sub 1): runs (animation 2) node to node ahead of the player
void guideRun(AnimatedActor& actor, GuideFields& f) {
    switch (RunStep(actor.step)) {
    case RunStep::Pick: {
        f.flags = 0;
        actor.anim = kAnimRun;
        actor.velocity.z = int16_t(0xfe00);
        const uint32_t r = rngStep();
        const GuideNode reached = f.node(f.reachedNode);
        const uint32_t k = uint32_t(int32_t(reached.choiceMask)) & r;
        f.targetNode = int8_t(reached.next[k]);
        actor.step = uint8_t(RunStep::Run);
    }
        [[fallthrough]];
    case RunStep::Run: {
        const NodePoint p = nodePoint(actor, f, f.targetNode);
        turnYawTo(actor.rotation.y, uint32_t(angleFromPoint(p, actor)), 0x80); // exe 0x5e3bc0 / 0x5e3df0
        integrateVelocity(actor, 0, 0, actor.velocity.z);                        // exe 0x5e3690
        if (near40(actor, p)) {
            f.reachedNode = f.targetNode;
            if (f.targetNode == f.lastNode) {
                enterSub(actor, GuideSub::AtLastNode);
                return;
            }
            if ((rngStep() & 7) == 0) enterSub(actor, GuideSub::Pause);
            else actor.step = uint8_t(RunStep::Pick);
        }
        if (playerBehind(actor, f) > 0x1800) actor.step = uint8_t(RunStep::Return);
        return;
    }
    case RunStep::Return: { // back toward the player until within 0x1200
        const NodePoint p{player().pos.x.whole, player().pos.z.whole};
        turnYawTo(actor.rotation.y, uint32_t(angleFromPoint(p, actor)), 0x40);
        integrateVelocity(actor, 0, 0, actor.velocity.z);
        if (planarDistance(player().pos, actor.pos) < 0x1200) enterSub(actor, GuideSub::WaitForPlayer);
        return;
    }
    default: return;
    }
}
// the jump of the waits: crouch (0x20), jump (0x21, a shout), land (0x22); true once landed
bool jumpSteps(AnimatedActor& actor, GuideFields& f) {
    switch (JumpStep(actor.step)) {
    case JumpStep::Crouch:
        f.flags = kGuideJumping;
        actor.velocity.z = 0;
        actor.anim = kAnimCrouch;
        actor.enterStep(uint8_t(JumpStep::Jump));
        return false;
    case JumpStep::Jump:
        if (!actor.animEnded()) return false;
        actor.airborne = 1;
        actor.velocity.y = int16_t(0xfe00);
        actor.acceleration.y = kGuideGravity;
        actor.anim = kAnimJump;
        actor.step = uint8_t(JumpStep::Air);
        sound(voiceOf(actor, kShoutAppo, kShoutDah), actor);
        return false;
    case JumpStep::Air:
        if (arc(actor, f, actor.rotation.y)) {
            actor.anim = kAnimLand;
            actor.airborne = 0;
            actor.velocity.y = 0;
            actor.step = uint8_t(JumpStep::Landed);
        }
        return false;
    case JumpStep::Landed: return true;
    default: return false;
    }
}
// exe 0x4c4f00 (state 1 sub 2): waits for the player, jumping and shouting ("over here")
void guideWait(AnimatedActor& actor, GuideFields& f) {
    if (jumpSteps(actor, f) && actor.animEnded()) {
        actor.anim = kAnimCrouch;
        actor.step = uint8_t(JumpStep::Jump);
        const int d = playerBehind(actor, f);
        if (d < 0xc00 || d > 0x1800) enterSub(actor, GuideSub::Run);
    }
    facePlayer(actor, 0x100);
}
// exe 0x4c5080 (state 1 sub 3): at the last node, jumping until the player comes (-> state 2 sub 2)
void guideAtLastNode(AnimatedActor& actor, GuideFields& f) {
    if (jumpSteps(actor, f) && actor.animEnded()) {
        actor.anim = kAnimCrouch;
        actor.step = uint8_t(JumpStep::Jump);
        const int d = planarDistance(player().pos, actor.pos);
        if (d < 0xc00) actor.enterState(uint8_t(GuideState::AtExit), uint8_t(ExitSub::RunToExit));
        else if (d > 0x1800) enterSub(actor, GuideSub::Run);
    }
    facePlayer(actor, 0x100);
}
// exe 0x4c51b0 (state 1 sub 4): a pause (animation 0xd)
void guidePause(AnimatedActor& actor, GuideFields& f) {
    if (actor.step == 0) {
        f.flags = 0;
        actor.anim = kAnimPause;
        actor.velocity.z = 0;
        actor.enterStep(1);
    }
    if (actor.animEnded()) enterSub(actor, GuideSub::Run);
}
enum class FallStep : uint8_t { Start = 0, Fall = 1, Landed = 2 };
// exe 0x4c51f0 (state 1 sub 5): the ground lost -- falls
void guideFall(AnimatedActor& actor, GuideFields& f) {
    switch (FallStep(actor.step)) {
    case FallStep::Start:
        f.flags = kGuideFalling;
        actor.step = uint8_t(FallStep::Fall);
        actor.anim = kAnimRun;
        actor.acceleration.y = kGuideGravity;
        resetModelScale(actor);
        [[fallthrough]];
    case FallStep::Fall:
        if (arc(actor, f, actor.rotation.y)) {
            actor.airborne = 0;
            actor.velocity.y = 0;
            actor.step = uint8_t(FallStep::Landed);
        }
        return;
    case FallStep::Landed: enterSub(actor, GuideSub::Run); return;
    default: return;
    }
}
// the getting-up animations after a knock-back: down by the side it fell on, then up
uint8_t downAnim(const AnimatedActor& actor) { return actor.anim != kAnimKnockedFront ? kAnimDownBack : kAnimDownFront; }
uint8_t upAnim(const AnimatedActor& actor) { return actor.anim != kAnimDownFront ? kAnimUpBack : kAnimUpFront; }
// exe 0x4c5260 (state 1 sub 6): hit -- knocked back, lands, gets up, picks a node behind the player
void guideKnockedBack(AnimatedActor& actor, GuideFields& f) {
    switch (KnockStep(actor.step)) {
    case KnockStep::Hit: {
        actor.enterStep(uint8_t(KnockStep::Fly));
        f.flags = kGuideKnocked;
        actor.anim = kAnimDownFront;
        knockFacing(actor, f, kAnimKnockedFront, kAnimKnockedBack, true);
        int power = int(f.hitBox.result & kHitDamageMask);
        if (power > 0x30) power = 0x30;
        else if (power < 0x10) power = 0x10;
        power <<= 4;
        actor.acceleration.z = 0x20;
        actor.velocity.x = 0;
        actor.velocity.z = int16_t(-power);
        if (actor.airborne != 0) actor.velocity.y -= uint32_t(power);
        else actor.velocity.y = int16_t(-power);
        actor.airborne = 1;
        actor.acceleration.y = kGuideGravity;
        sound(voiceOf(actor, kHurtAppo, kHurtDah), actor);
        resetModelScale(actor);
    }
        [[fallthrough]];
    case KnockStep::Fly:
        if (arc(actor, f, actor.rotation.y - int16_t(f.knockYaw))) {
            actor.airborne = 0;
            actor.velocity.z = int16_t(uint16_t(actor.velocity.z) << 1);
            actor.velocity.y = 0;
            ++actor.step;
            f.flags &= uint8_t(~(kGuideReflect | kGuideNoFall));
            sound(kSoundLand, actor);
        }
        return;
    case KnockStep::Slide: // sliding to a stop
        moveAlongYaw(actor.pos, actor.rotation.y - int16_t(f.knockYaw), 0, 0, actor.velocity.z);
        actor.velocity.z += uint16_t(actor.acceleration.z);
        dust(actor);
        if (actor.velocity.z >= 0) ++actor.step;
        return;
    case KnockStep::Land:
        if (actor.animEnded()) {
            actor.anim = downAnim(actor);
            ++actor.step;
        }
        return;
    case KnockStep::GetUp:
        if (actor.animEnded()) {
            actor.anim = upAnim(actor);
            ++actor.step;
        }
        return;
    case KnockStep::Done: {
        if (!actor.animEnded()) return;
        f.blinkTicks = 0x40;
        if ((rngStep() & 3) != 0) {
            int n = f.lastNode;
            if (n == 0) {
                f.targetNode = 1;
            } else {
                for (;;) {
                    if (f.node(n).x[actor.variant] < actor.pos.x.whole) {
                        f.targetNode = int8_t(n);
                        break;
                    }
                    --n;
                    if (n == 0) {
                        f.targetNode = 1;
                        break;
                    }
                }
            }
        }
        enterSub(actor, GuideSub::Run);
        return;
    }
    default: return;
    }
}
// exe 0x4c5560 (state 1 sub 7): a wall in the way -- hops over it
void guideHopWall(AnimatedActor& actor, GuideFields& f) {
    switch (HopStep(actor.step)) {
    case HopStep::Crouch:
        f.flags = kGuideHopping;
        actor.enterStep(uint8_t(HopStep::Jump));
        actor.anim = kAnimCrouch;
        actor.velocity.z = 0;
        actor.velocity.x = 0;
        resetModelScale(actor);
        [[fallthrough]];
    case HopStep::Jump:
        if (actor.animEnded()) {
            actor.anim = kAnimJump;
            actor.step = uint8_t(HopStep::Air);
            actor.velocity.z = int16_t(0xff00);
            actor.airborne = 1;
            actor.velocity.y = int16_t(0xfe00);
            actor.acceleration.y = kGuideGravity;
        }
        return;
    case HopStep::Air:
        if (arc(actor, f, actor.rotation.y)) {
            actor.velocity.z = int16_t(uint16_t(actor.velocity.z) << 1);
            actor.anim = kAnimJump;
            actor.airborne = 0;
            actor.velocity.y = 0;
            actor.step = uint8_t(HopStep::Done);
            f.flags |= kGuideReflect;
        }
        return;
    case HopStep::Done:
        integrateVelocity(actor, 0, 0, actor.velocity.z);
        enterSub(actor, GuideSub::Run);
        return;
    default: return;
    }
}
// exe 0x4c4bb0 (state 1): the guide (subs [0x894e4c]); the blink, the hits, the falls and walls, the lift
void guideState(AnimatedActor& actor, GuideFields& f) {
    actor.tint = kTintNeutral;
    uint8_t b = f.blinkTicks;
    if (b != 0) {
        --b;
        f.blinkTicks = b;
        if (b != 0) {
            if (b & 3) actor.flags |= kActorDrawn;
            else actor.flags &= uint8_t(~kActorDrawn);
        } else {
            actor.flags |= kActorDrawn;
            f.flags &= uint8_t(~kGuideNoHitEntry);
        }
    } else if (!(f.flags & kGuideNoHits) && hitTaken(actor, f)) {
        enterSub(actor, GuideSub::KnockedBack);
    }
    switch (GuideSub(actor.subState)) {
    case GuideSub::Start: guideStart(actor, f); break;
    case GuideSub::Run: guideRun(actor, f); break;
    case GuideSub::WaitForPlayer: guideWait(actor, f); break;
    case GuideSub::AtLastNode: guideAtLastNode(actor, f); break;
    case GuideSub::Pause: guidePause(actor, f); break;
    case GuideSub::Fall: guideFall(actor, f); break;
    case GuideSub::KnockedBack: guideKnockedBack(actor, f); break;
    case GuideSub::HopWall: guideHopWall(actor, f); break;
    default: notPortedAt(kStoryPort, "class 0x4c4980 state 1 sub", actor.subState);
    }
    if (f.blinkTicks != 0) f.flags |= kGuideNoHitEntry;
    if (collide(actor, f)) {
        actor.airborne = 1;
        if (!(f.flags & kGuideNoFall)) enterSub(actor, GuideSub::Fall);
    } else if ((f.collision & kGuideWalls) && !(f.flags & kGuideNoHop)) {
        enterSub(actor, GuideSub::HopWall);
    }
    if (playerLiftTargetCheck(actor, 0)) { // exe 0x573a60
        actor.enterState(uint8_t(GuideState::Carried));
        actor.flags |= kActorDrawn;
        f.flags |= kGuideNoHitEntry;
    }
}
// exe 0x4c5690 (state 2 sub 0)
void exitStart(AnimatedActor& actor, GuideFields& f) {
    stand(actor);
    f.flags = 0;
    actor.anim = kAnimIdle;
    enterSub(actor, ExitSub::Stand);
}
// the exit point: the node after the last
NodePoint exitPoint(const AnimatedActor& actor, const GuideFields& f) { return nodePoint(actor, f, f.lastNode + 1); }
// exe 0x4c56c0 (state 2 sub 1): standing at the exit point, facing the player
void exitStand(AnimatedActor& actor, GuideFields& f) {
    if (actor.step == 0) {
        f.flags = kGuideKeepGround;
        actor.enterStep(1);
        actor.airborne = 0;
        actor.anim = kAnimIdle;
        const NodePoint p = exitPoint(actor, f);
        actor.pos.x.whole = p.x;
        actor.pos.z.whole = p.z;
    }
    actor.pos.y.whole = int16_t(f.path == 0 ? 0xfec0 : 0xfde0);
    facePlayer(actor, 0x100);
}
// exe 0x4c5760 (state 2 sub 2): runs off to the exit point
void exitRun(AnimatedActor& actor, GuideFields& f) {
    switch (ExitRunStep(actor.step)) {
    case ExitRunStep::Start:
        f.flags = kGuideRunning;
        actor.flags |= kActorDrawn;
        actor.rotation.x = 0;
        actor.rotation.z = 0;
        actor.velocity.z = int16_t(0xfe00);
        actor.anim = kAnimRun;
        actor.step = uint8_t(ExitRunStep::Run);
        [[fallthrough]];
    case ExitRunStep::Run: {
        const NodePoint p = exitPoint(actor, f);
        turnYawTo(actor.rotation.y, uint32_t(angleFromPoint(p, actor)), 0x800);
        integrateVelocity(actor, 0, 0, actor.velocity.z);
        if (near40(actor, p)) actor.step = uint8_t(ExitRunStep::Arrived);
        if (f.path != 0) actor.pos.y.whole += 2u;
        return;
    }
    case ExitRunStep::Arrived: enterSub(actor, ExitSub::Stand); return;
    default: return;
    }
}
// exe 0x4c5850 (state 2 sub 3): hit at the exit -- knocked, gets up
void exitKnocked(AnimatedActor& actor, GuideFields& f) {
    switch (ExitKnockStep(actor.step)) {
    case ExitKnockStep::Hit: {
        f.flags = kGuideKnockedAtExit;
        actor.enterStep(uint8_t(ExitKnockStep::Land));
        knockFacing(actor, f, kAnimKnockedFront, kAnimKnockedBack, false);
        const uint8_t air = actor.airborne;
        actor.velocity.clearWords();
        actor.acceleration.clearWords();
        if (air != 0) actor.acceleration.y = kGuideGravity;
        else actor.velocity.y = 0;
        sound(voiceOf(actor, kHurtAppo, kHurtDah), actor);
        resetModelScale(actor);
    }
        [[fallthrough]];
    case ExitKnockStep::Land:
        if (actor.animEnded()) {
            actor.anim = downAnim(actor);
            ++actor.step;
        }
        return;
    case ExitKnockStep::GetUp:
        if (actor.animEnded()) {
            actor.anim = upAnim(actor);
            ++actor.step;
        }
        return;
    case ExitKnockStep::Done:
        if (actor.animEnded()) enterSub(actor, ExitSub::Stand);
        return;
    default: return;
    }
}
// exe 0x4c5640 (state 2): at the exit (subs [0x894e6c])
void exitState(AnimatedActor& actor, GuideFields& f) {
    actor.tint = kTintNeutral;
    if (!(f.flags & kGuideNoHits) && hitTaken(actor, f)) enterSub(actor, ExitSub::Knocked);
    switch (ExitSub(actor.subState)) {
    case ExitSub::Start: exitStart(actor, f); return;
    case ExitSub::Stand: exitStand(actor, f); return;
    case ExitSub::RunToExit: exitRun(actor, f); return;
    case ExitSub::Knocked: exitKnocked(actor, f); return;
    default: notPortedAt(kStoryPort, "class 0x4c4980 state 2 sub", actor.subState);
    }
}
void guideFlying(AnimatedActor& actor, GuideFields& f);
// exe 0x4c59c0 (state 3 sub 0): in the player's hands (shouting), thrown / dropped
void guideHeld(AnimatedActor& actor, GuideFields& f) {
    if (actor.step == 0) {
        f.flags = kGuideCarried;
        actor.step = 1;
        actor.anim = actor.hitPoints < 0 ? kAnimCarriedHurt : kAnimCarried;
        resetModelScale(actor);
        encounterUntrack(actor); // exe 0x5e6540
        actor.tint = kTintNeutral;
        f.shoutTicks = 0;
    }
    const uint16_t t = f.shoutTicks;
    if (t == 0) {
        if ((rngStep() & 0xf) == 0) {
            sound(voiceOf(actor, kCarriedAppo, kCarriedDah), actor);
            f.shoutTicks = 0x28;
        }
    } else {
        f.shoutTicks = uint16_t(t - 1u);
    }
    carryAtHands(actor, 0, -0x18, 0); // exe 0x573ab0
    const int r = int(carryReleased()); // exe 0x573950
    if (r == 0) return;
    f.flags &= uint8_t(~kGuideNoHitEntry);
    f.knockYaw = uint16_t(player().rotation.y);
    if (r == 1) {
        int32_t v[3];
        playerAimVector(0, -0x100, -0x380, v); // exe 0x5e3e50
        actor.velocity.y = int16_t(v[1]);
        actor.velocity.z = int16_t(v[2]);
        f.flags |= kGuideThrown;
    } else {
        actor.velocity.y = int16_t(0xfd00);
        actor.velocity.z = int16_t(0xffc0);
    }
    actor.velocity.x = 0;
    actor.acceleration.x = 0;
    actor.acceleration.y = kGuideGravity;
    actor.rotation.y = int16_t(f.knockYaw);
    actor.airborne = 1;
    carryDropFromPlayer(actor); // exe 0x573990
    enterSub(actor, CarriedSub::Flying);
    guideFlying(actor, f);
}
// exe 0x4c5b40 (state 3 sub 1): thrown / dropped -- flies, lands, slides, gets up -> state 1
void guideFlying(AnimatedActor& actor, GuideFields& f) {
    if (f.hitBox.result & kHitThrownHit) {
        actor.rotation.y += 0x800u;
        f.flags |= kGuideNoHitEntry;
        spawnHitSparkWithoutSize(kActorSweepGroup2, actor.pos.x.whole, int16_t(uint16_t(actor.pos.y.whole - 0x40)),
                                 actor.pos.z.whole); // exe 0x5e5400
        balanceAdd(kBalanceLoss);                    // exe 0x5e51d0
    }
    switch (FlyStep(actor.step)) {
    case FlyStep::Fly:
        if (arc(actor, f, actor.rotation.y)) {
            actor.velocity.z = int16_t(uint16_t(actor.velocity.z) << 1);
            actor.airborne = 0;
            actor.acceleration.z = 0x20;
            actor.velocity.y = 0;
            f.flags = uint8_t((f.flags & uint8_t(~kGuideReflect)) | kGuideKeepGround);
            ++actor.step;
            actor.anim = kAnimDownFront;
            sound(kSoundLand, actor);
        }
        break;
    case FlyStep::Slide:
        moveAlongYaw(actor.pos, actor.rotation.y, 0, actor.velocity.y, actor.velocity.z); // exe 0x5e36f0
        actor.velocity.z += uint16_t(actor.acceleration.z);
        dust(actor);
        if (actor.velocity.z >= 0) ++actor.step;
        break;
    case FlyStep::Land:
        if (actor.animEnded()) {
            actor.anim = kAnimUpFront;
            ++actor.step;
        }
        break;
    case FlyStep::GetUp: {
        if (!actor.animEnded()) break;
        const int n = f.lastNode;
        int i = 0;
        if (n > 0) {
            for (; i < n; ++i) {
                if (f.node(i).x[actor.variant] > actor.pos.x.whole) {
                    f.reachedNode = int8_t(i);
                    break;
                }
            }
        }
        if (i == n) f.reachedNode = 8;
        actor.enterState(uint8_t(GuideState::Guide));
        f.flags &= uint8_t(~kGuideKeepGround);
        break;
    }
    default: break;
    }
    collide(actor, f);
    if ((f.collision & kGuideWalls) && actor.airborne != 0) {
        const uint32_t r = rngStep();
        const uint32_t r2 = rngStep();
        if (r & 1) actor.rotation.y += r2 & 0x1ff;
        else actor.rotation.y -= r2 & 0x1ff;
        actor.velocity.z = int16_t(actor.velocity.z >> 1);
        actor.velocity.y = int16_t(0xfe00);
        sound(kSoundWall, actor);
    }
}
// exe 0x4c4ab0 (state 0): the model, the hit points, the boxes, the path, on the ground, registered
void guideInit(AnimatedActor& actor, GuideFields& f) {
    actor.flags |= kActorDrawn;
    if (!bindActorModel(actor, actor.variant)) { // exe 0x5e0bc0
        freeActor(actor);                         // exe 0x5e1490
        return;
    }
    actor.runFlags |= kActorRunMarked;
    actor.rotation.x = 0;
    actor.rotation.z = 0;
    actor.anim = kAnimStand;
    actor.prevAnim = kNoAnim;
    const uint32_t hp = hasAnimatedStats(actor.type) ? uint32_t(animatedStats(actor.type).hitPoints) : 0;
    actor.maxHitPoints = int16_t(hp);
    actor.hitPoints = int16_t(hp << 4);
    actor.collisionBox.pointAt(TableId::AppoDahAGuideBox);
    actor.interactionBox.pointAt(TableId::AppoDahAGuideLiftBox);
    actor.interaction = kInteractLift;
    actor.airborne = 0;
    actor.onPlatform = 0;
    actor.clearClassBlock(); // exe 0x40dc40
    f.hitBox.previousX = actor.pos.x.whole;
    f.hitBox.previousZ = actor.pos.z.whole;
    f.hitBox.result = 0;
    f.reachedNode = 0;
    f.targetNode = 0;
    f.blinkTicks = 0;
    f.animKey = 0;
    if (f.path == 0) {
        f.nodes = stateAddress(TableId::AppoDahAShortPath);
        f.lastNode = kShortPathLast;
    } else {
        f.nodes = stateAddress(TableId::AppoDahALongPath);
        f.lastNode = kLongPathLast;
    }
    actor.enterState(uint8_t(GuideState::Guide));
    actor.pos.y.whole = int16_t(actorGround(*E, actor)); // exe 0x40f190
    registerEnemy(actor.slot, 0);                         // exe 0x5e5c10
}
// exe 0x4c4980's body
void guide(AnimatedActor& actor) {
    GuideFields& f = classState<GuideFields>(actor);
    actor.prevAnim = actor.anim;
    actor.savePreviousPose();
    actor.savePreviousContact();
    f.animKey = 0;
    switch (GuideState(actor.state)) { // [0x894e3c]
    case GuideState::Init: guideInit(actor, f); break;
    case GuideState::Guide: guideState(actor, f); break;
    case GuideState::AtExit: exitState(actor, f); break;
    case GuideState::Carried: // exe 0x4c59a0 ([0x894e7c])
        if (CarriedSub(actor.subState) == CarriedSub::Held) guideHeld(actor, f);
        else if (CarriedSub(actor.subState) == CarriedSub::Flying) guideFlying(actor, f);
        else notPortedAt(kStoryPort, "class 0x4c4980 state 3 sub", actor.subState);
        break;
    default: notPortedAt(kStoryPort, "class 0x4c4980 state", actor.state);
    }
    if (!(actor.flags & kActorLive)) return;
    if (actor.anim == actor.prevAnim) animStepPose(actor);      // exe 0x5e1e80
    else animSetPose(actor, actor.anim, f.animKey);             // exe 0x5e1c30
    const AnimatedActorStats row = animatedStats(actor.type);
    const uint8_t flags = f.flags;
    const ShortVec4* bp = bonePointOrNull(actor, 0); // exe 0x5e1810
    f.hitBox.result = 0;
    f.hitBox.previousX = actor.prevPos.x.whole;
    f.hitBox.previousZ = actor.prevPos.z.whole;
    if (!(flags & kGuideNoHitEntry)) {
        if (bp == 0) notPorted(kStoryPort, "class 0x4c4980: exe 0x5e1810 returned null");
        const ShortVec4& p = *bp;
        const int16_t pos[3] = {p.x, p.y, p.z};
        const uint32_t slot = uint32_t(actor.slot) << 24;
        if (!(flags & kGuideThrown))
            hitListSubmit(pos, f.hitBox, slot | 0xa8800au, 0x830000u); // exe 0x5e4120
        else
            hitListSubmit(pos, f.hitBox, slot | 0x20800au, uint32_t(row.bodyAttack) | 0x840000u);
    }
    // exe 0x5f20f0(actor, 0x30, 0x160, 0) unless held (state 3 sub 0): the shadow list entry (render output)
}

constexpr uint8_t kPuppetRestart = 0x01; // the animation restarted at its key every tick (the pose held)
enum class GatePuppetState : uint8_t { Bind = 0, WaitAct1 = 1, Act1 = 2, Act2 = 3 };
enum class Act1Step : uint8_t { Wait = 0, Walk = 1, Stopped = 2 };
enum class Act2Step : uint8_t { Place = 0, Walk = 1, WalkOn = 2, Wait = 3, Talk = 4, TalkMore = 5, WaitLine = 6,
                                LastLine = 7, Done = 8 };
constexpr uint32_t kFlagAppoDahHurt = 0x680; // + the variant: hurt in the fight (grey)
constexpr uint16_t kHurtTint = 0x2108;
constexpr size_t kTalkPartShown = 10, kTalkPartHidden = 3; // the head parts of the second line

// exe 0x4c6730(actor, the script block): by the step sub and the step tick
void gatePuppetRoutine(AnimatedActor& actor, GatePuppetFields& f, ScriptState& s) {
    const int32_t tick = s.stepTick;
    switch (GatePuppetState(actor.state)) {
    case GatePuppetState::Bind:
        if (!bindActorModel(actor, actor.variant)) { // exe 0x5e0bc0
            freeActor(actor);                         // exe 0x5e1490
            return;
        }
        actor.collisionBox.pointAt(TableId::AppoDahAGatePuppetBox);
        actor.clearClassBlock();
        actor.enterState(uint8_t(GatePuppetState::WaitAct1));
        actor.anim = 0;
        actor.prevAnim = kNoAnim;
        f.castFlags |= kPuppetRestart;
        appoDahFaceReset(actor, f.mouth);
        if (storyFlag(kFlagAppoDahHurt + uint32_t(actor.variant))) actor.tint = kHurtTint;
        return;
    case GatePuppetState::WaitAct1:
        if (s.stepSub == 1) actor.enterState(uint8_t(GatePuppetState::Act1));
        return;
    case GatePuppetState::Act1:
        switch (Act1Step(actor.step)) {
        case Act1Step::Wait:
            if (tick == 0x1e) {
                actor.anim = 2;
                f.castFlags &= uint8_t(~kPuppetRestart);
                actor.step = uint8_t(Act1Step::Walk);
            }
            break;
        case Act1Step::Walk:
            integrateVelocity(actor, 0, 0, -0x100); // exe 0x5e3690
            if (s.stepTick == 0x5a) actor.step = uint8_t(Act1Step::Stopped);
            break;
        default: break;
        }
        if (s.stepSub == 2) actor.enterState(uint8_t(GatePuppetState::Act2));
        return;
    case GatePuppetState::Act2:
        switch (Act2Step(actor.step)) {
        case Act2Step::Place:
            actor.pos.x.whole = 0x1526;
            actor.pos.y.whole = int16_t(0xfe1a);
            actor.pos.z.whole = 0x2c9;
            actor.anim = 2;
            actor.prevAnim = kNoAnim;
            f.animKey = 0x1b;
            f.castFlags &= uint8_t(~kPuppetRestart);
            actor.step = uint8_t(Act2Step::Walk);
            [[fallthrough]];
        case Act2Step::Walk:
            integrateVelocity(actor, 0, 0, -0x100);
            if (s.stepTick == 5) {
                actor.anim = 0x17;
                f.animKey = 0;
                actor.step = uint8_t(Act2Step::WalkOn);
            }
            return;
        case Act2Step::WalkOn:
            integrateVelocity(actor, 0, 0, -0x100);
            if (s.stepTick == 0xa) actor.step = uint8_t(Act2Step::Wait);
            return;
        case Act2Step::Wait:
            if (tick == 0x14) {
                appoDahFaceReset(actor, f.mouth);
                actor.step = uint8_t(Act2Step::Talk);
            }
            return;
        case Act2Step::Talk:
            appoDahTalk(actor, f.mouth);
            if (s.stepTick == 0x21) {
                actor.parts[kTalkPartHidden].flags |= kPartHidden;
                actor.parts[kTalkPartShown].flags &= uint8_t(~kPartHidden);
                actor.step = uint8_t(Act2Step::TalkMore);
            }
            return;
        case Act2Step::TalkMore:
            appoDahTalk(actor, f.mouth);
            if (s.stepTick == 0x4b) {
                appoDahFaceReset(actor, f.mouth);
                actor.step = uint8_t(Act2Step::WaitLine);
            }
            return;
        case Act2Step::WaitLine:
            if (tick == 0x5a) {
                actor.anim = 0x18;
                actor.step = uint8_t(Act2Step::LastLine);
            }
            return;
        case Act2Step::LastLine:
            appoDahTalk(actor, f.mouth);
            if (s.stepTick == 0xeb) {
                appoDahFaceReset(actor, f.mouth);
                f.castFlags &= uint8_t(~kPuppetRestart);
                actor.step = uint8_t(Act2Step::Done);
            }
            return;
        default: return;
        }
    default: return;
    }
}
// exe 0x4c6650's body: the routine when the step tick moves
void gatePuppet(AnimatedActor& actor) {
    GatePuppetFields& f = classState<GatePuppetFields>(actor);
    castSavePose(actor);
    ScriptState& s = scriptState();
    const int32_t t = s.stepTick;
    if (t == 1 || t != int32_t(f.lastTick)) {
        f.lastTick = int8_t(t);
        if (f.routine != 0)
            notPortedAt(kStoryPort, "class 0x4c6650 variant (+0xc)", uint8_t(f.routine)); // [0x894ea0] has one entry
        gatePuppetRoutine(actor, f, s);
    }
    if (!(actor.flags & kActorLive)) return;
    if (f.noGround == 0) actor.pos.y.whole = int16_t(actorGround(*E, actor)); // exe 0x40f190
    if (actor.anim == actor.prevAnim && !(f.castFlags & kPuppetRestart)) animStepPose(actor); // exe 0x5e1e80
    else animSetPose(actor, actor.anim, f.animKey);                                         // exe 0x5e1c30
    // exe 0x5f20f0(actor, 0x30, 0x160, 0): the shadow list entry (render output)
}

} // namespace

void appoDahGuideUpdate(const PlayerEnv& env, uint32_t actor) {
    E = &env;
    guide(animatedActorAt(actor));
}

void appoDahGatePuppetUpdate(const PlayerEnv& env, uint32_t actor) {
    E = &env;
    gatePuppet(animatedActorAt(actor));
}

} // namespace od::port
