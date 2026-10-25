// The shy critter of ST0D, class 0x509830 (pool 0x20 type 0x7e; see st0d_critter.hpp). It wanders (the RNG: idle /
// hop / short walks), runs off when the player comes near its front, flees until he is far, is knocked away by a hit
// (sound 0x8d, the balance -500), falls; the player can lift it (state 2: the carry routines 0x573950.. are not
// ported). States (the exe's table 0x89823c): 0 exe 0x5099a0, 1 exe 0x509a60 (the sub-states 0x89824c), 2 exe
// 0x50a170 lifted, 3 exe 0x50a3c0 defeated (the drops, freed). Its shadow (exe 0x5f20f0, unless lifted) is render
// output and left out.
#include "content/yosyonke/st0d_critter.hpp"
#include "content/yosyonke/st0d_critter_state.hpp"

#include "engine/data_source.hpp"
#include "game/actor_state.hpp"
#include "game/cutscene_actor.hpp"
#include "game/actor_animation.hpp"
#include "game/actor_model.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_stats.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/carry.hpp"
#include "game/distances.hpp"
#include "game/event_state.hpp"
#include "game/game_state.hpp"
#include "game/hit_effects.hpp"
#include "game/hit_list.hpp"
#include "game/pickups.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/rewards.hpp"
#include "game/walker_collision.hpp"

namespace od::port {

using namespace st0d_critter_state;

namespace {

constexpr const char* kCritterPort = "town story port"; // the area of its port-gap errors

constexpr uint8_t kCritterNoHitEntry = 0x01;
constexpr uint8_t kCritterNoHurt = 0x02;     // the hits are not taken
constexpr uint8_t kCritterKnockedBit = 0x04; // set while knocked away (the Roll classes' talk bit; this class does
                                             // not read it)
constexpr uint8_t kCritterNoFall = 0x08;
constexpr uint8_t kCritterBodyAttack = 0x10; // its entry as the player's attacks, with the body's damage (thrown)
constexpr uint8_t kCritterStrong = 0x20;     // its entry's attack strong, without damage

enum class CritterState : uint8_t { Init = 0, Main = 1, Lifted = 2, Defeated = 3 };
enum class CritterSub : uint8_t {
    Settle = 0,  // exe 0x509af0 (exe 0x509b10: animation 8, the motion cleared)
    Wander = 1,  // exe 0x509b30
    Flee = 2,    // exe 0x509e00
    Fall = 3,    // exe 0x509f60
    Knocked = 4, // exe 0x509fc0
};
enum class WanderStep : uint8_t { Start = 0, Wait = 1, Choose = 2, Walk = 3 };
// the wander's walk: the wall's turn picked (the step counter)
constexpr uint8_t kNoTurnPicked = 0, kTurnPicked = 1;
enum class KnockedStep : uint8_t { Launch = 0, Rise = 1, Fall = 2, Slide = 3 };
const DataTable<CollisionBoxRecord> critterCollisionBox{TableId::St0dCritterCollisionBox};
const DataTable<InteractionBoxRecord> critterLiftBox{TableId::St0dCritterLiftBox};
constexpr int kCritterMaxDrop = 0x10;
constexpr uint16_t kKnockedSound = 0x8d;
constexpr uint32_t kHurtingHits = 0x10fff;      // any damage or a bump (& the result)
constexpr uint8_t kDustVariant = 3;             // the hit spark's dust variant of its slide
constexpr uint16_t kDustSize = 0x40;
constexpr uint32_t kHarmlessBump = kHitAttackHarmless | kHitAttackBump;

void setSub(AnimatedActor& a, CritterSub s) {
    a.subState = uint8_t(s);
    a.restartStep();
}
void moveForward(AnimatedActor& a, int z) { moveAlongYaw(a.pos, a.rotation.y, 0, 0, z); } // exe 0x5e36f0
int distanceFromPlayer(const AnimatedActor& a) { return planarDistance(player().pos, a.pos); }
// a random heading within +-0x200 of the yaw
int16_t randomHeading(const AnimatedActor& a) {
    return int16_t(uint16_t((rngStep() & 0x3ff) + uint16_t(a.rotation.y) - 0x200));
}

void critterSub(AnimatedActor& a, CritterFields& f) {
    switch (CritterSub(a.subState)) {
    case CritterSub::Settle:
        a.anim = 8;
        a.velocity.setXY(0, 0);
        a.velocity.z = 0;
        a.acceleration.z = 0;
        resetModelScale(a);
        f.behaviour = 0;
        setSub(a, CritterSub::Wander);
        return;
    case CritterSub::Wander: {
        switch (WanderStep(a.step)) {
        case WanderStep::Start:
            f.behaviour = 0;
            a.velocity.z = 0;
            a.velocity.y = 0;
            a.acceleration.y = 0;
            a.anim = 0;
            a.enterStep(uint8_t(WanderStep::Wait));
            break;
        case WanderStep::Wait:
            if (a.animEnded()) ++a.step;
            break;
        case WanderStep::Choose:
            if ((rngStep() & 3) == 0) { // a hop
                a.anim = 1;
                a.step = uint8_t(WanderStep::Wait);
            } else if ((rngStep() & 0xf) == 0) { // idle
                a.anim = 0;
                a.step = uint8_t(WanderStep::Wait);
            } else if ((rngStep() & 3) == 0) { // a quick walk
                f.heading = randomHeading(a);
                a.anim = 9;
                a.velocity.z = -0x100;
                a.step = uint8_t(WanderStep::Walk);
            } else { // a slow walk
                f.heading = randomHeading(a);
                a.anim = 8;
                a.velocity.z = -0x40;
                a.step = uint8_t(WanderStep::Walk);
            }
            break;
        case WanderStep::Walk:
            turnYawTo(a.rotation.y, uint32_t(int32_t(f.heading)), 0x20);
            moveForward(a, a.velocity.z);
            if (f.hits & kWalkHitSides) {
                if (a.stepCounter == kNoTurnPicked) {
                    f.turn = (rngStep() & 1) ? int16_t(0x20) : int16_t(-0x20);
                    a.stepCounter = kTurnPicked;
                    f.heading = int16_t(uint32_t(uint16_t(f.heading)) + uint16_t(f.turn));
                } else if (a.stepCounter == kTurnPicked) {
                    f.heading = int16_t(uint32_t(uint16_t(f.heading)) + uint16_t(f.turn));
                }
            } else if (a.animEnded() && (rngStep() & 1)) {
                a.anim = 0;
                a.step = uint8_t(WanderStep::Wait);
                a.stepCounter = kNoTurnPicked;
            }
            break;
        default: break;
        }
        // it runs off when the player is near and in front (within +-0x400, +-0x600 while hopping)
        int d = int(uint16_t(a.rotation.y) & kAngleMask) - (angleOf(player().pos, a.pos) & kAngleMask);
        if (uint32_t(d + kHalfTurn) > 0x1000u) d = int((uint32_t(-d) ^ 0xfffu) + 1u);
        const int k = a.anim == 1 ? 0x600 : 0x400;
        if (uint32_t(k + d) < uint32_t(k * 2) && distanceFromPlayer(a) < 0x400) {
            f.heading = int16_t(uint32_t(uint16_t(a.rotation.y)) + (d < 0 ? 0x400u : 0xfc00u));
            setSub(a, CritterSub::Flee);
        }
        return;
    }
    case CritterSub::Flee: {
        const uint8_t st = a.step;
        if (st == 0) {
            f.behaviour = kWalkerBounces | kCritterStrong;
            a.velocity.z = -0x1e0;
            a.velocity.y = 0;
            a.acceleration.y = 0;
            a.anim = 9;
            a.step = 1;
        } else if (st == 1) {
            turnYawTo(a.rotation.y, uint32_t(int32_t(f.heading)), 0x40);
            moveForward(a, a.velocity.z);
            if (f.hits & kWalkHitSides) f.heading = a.rotation.y;
            if (f.timer != 0) {
                --f.timer;
            } else if (distanceFromPlayer(a) < 0x200) {
                f.timer = 0x40;
                f.heading = int16_t(uint32_t(uint16_t(f.heading)) + ((rngStep() & 1) ? 0x400u : 0xfc00u));
            } else {
                f.timer = 0x10;
                f.heading = int16_t(uint32_t(uint16_t(f.heading)) + ((rngStep() & 1) ? 0x200u : 0xfe00u));
            }
        }
        if (distanceFromPlayer(a) > 0x1000 && a.animEnded()) setSub(a, CritterSub::Wander);
        return;
    }
    case CritterSub::Fall: {
        const uint8_t st = a.step;
        if (st == 0) {
            f.behaviour = kWalkerBounces | kCritterNoFall;
            a.step = 1;
            a.anim = 7;
            a.acceleration.y = 0x40;
            resetModelScale(a);
        } else if (st != 1) {
            return;
        }
        if (walkerFallStep(a, f.hits, a.rotation.y)) { // exe 0x50a360
            a.airborne = 0;
            a.velocity.y = 0;
            setSub(a, CritterSub::Flee);
        }
        return;
    }
    case CritterSub::Knocked:
        switch (KnockedStep(a.step)) {
        case KnockedStep::Launch: {
            a.enterStep(uint8_t(KnockedStep::Rise));
            f.behaviour = kWalkerBounces | kCritterNoFall | kCritterKnockedBit | kCritterNoHurt;
            a.anim = 2;
            a.rotation.y = int16_t(uint32_t(uint16_t(f.turn)) + uint32_t(kHalfTurn));
            a.rotation.x = 0;
            a.rotation.z = 0;
            a.airborne = kAirborneFalling;
            a.velocity.z = 0x200;
            a.acceleration.z = -0x14;
            a.velocity.x = 0;
            a.acceleration.y = 0x40;
            int dmg = int(f.box.result & kHitDamageMask);
            if (dmg > 0x30) dmg = 0x30;
            else if (dmg < 0x20) dmg = 0x20;
            a.velocity.y = int16_t(uint32_t(uint16_t(a.velocity.y)) - uint32_t(dmg << 4));
            soundPlayAtPos(kKnockedSound, a.pos);
            resetModelScale(a);
            return;
        }
        case KnockedStep::Rise:
            if (walkerFallStep(a, f.hits, a.rotation.y)) {
                a.anim = 3;
                ++a.step;
            }
            return;
        case KnockedStep::Fall:
            if (walkerFallStep(a, f.hits, a.rotation.y)) {
                a.velocity.z = int16_t(uint32_t(uint16_t(a.velocity.z)) << 1);
                a.airborne = 0;
                a.velocity.y = 0;
                ++a.step;
                a.anim = 4;
                f.behaviour &= uint8_t(~(kWalkerBounces | kCritterNoFall | kCritterNoHurt));
            }
            return;
        case KnockedStep::Slide: {
            moveForward(a, a.velocity.z);
            a.velocity.z += a.acceleration.z;
            if (gameState().frame & 1) { // a dust puff (pool 0xa0 global type 0 variant 3)
                if (const uint32_t at = allocActorA0(0)) {
                    EffectActor& e = effectActorAt(at);
                    e.flags = kActorLive;
                    e.loopFlags |= uint8_t(kActorSweepGroup2 | kActorLateLoop);
                    e.type = 0;
                    e.variant = kDustVariant;
                    e.pos.x = a.pos.x.whole;
                    e.pos.y = a.pos.y.whole;
                    e.pos.z = a.pos.z.whole;
                    classState<HitSparkFields>(e).size = kDustSize;
                }
            }
            if (a.velocity.z <= 0) {
                balanceAdd(-500);
                a.rotation.y += uint32_t(kHalfTurn);
                setSub(a, CritterSub::Flee);
            }
            return;
        }
        default: return;
        }
    default: notPortedAt(kCritterPort, "0x509830 sub-state", a.subState);
    }
}

// exe 0x509830
void critter(const PlayerEnv& env, AnimatedActor& a) {
    CritterFields& f = classState<CritterFields>(a);
    a.prevAnim = a.anim;
    a.savePreviousPose();
    a.savePreviousContact();
    switch (CritterState(a.state)) {
    case CritterState::Init: { // exe 0x5099a0
        a.flags |= kActorDrawn;
        if (!bindActorModel(a, 0)) {
            freeActor(a);
            return;
        }
        a.runFlags |= kActorRunMarked;
        a.rotation.x = 0;
        a.rotation.z = 0;
        a.anim = 0;
        a.prevAnim = kNoAnimShown;
        const uint16_t hp = hasAnimatedStats(a.type) ? animatedStats(a.type).hitPoints : 0;
        a.maxHitPoints = int16_t(hp);
        a.hitPoints = int16_t(hp);
        a.collisionBox.pointAt(critterCollisionBox.stateAddress());
        a.interactionBox.pointAt(critterLiftBox.stateAddress());
        a.interaction = kInteractLift;
        a.airborne = 0;
        a.onPlatform = 0;
        a.clearClassBlock();
        f.box.previousX = a.pos.x.whole;
        f.box.result = 0;
        f.box.previousZ = a.pos.z.whole;
        a.enterState(uint8_t(CritterState::Main));
        break;
    }
    case CritterState::Main: { // exe 0x509a60
        a.tint = kTintNeutral;
        if (!(f.behaviour & kCritterNoHurt) && (f.box.result & kHurtingHits)) { // exe 0x50a440
            f.turn = f.box.contact.x;
            a.tint = kTintFlash;
            setSub(a, CritterSub::Knocked);
        }
        critterSub(a, f);
        if (walkerCollide(env, a, f.hits, f.behaviour, false, kCritterMaxDrop)) { // exe 0x50a470
            a.airborne = kAirborneFalling;
            if (!(f.behaviour & kCritterNoFall)) setSub(a, CritterSub::Fall);
        }
        if (a.hitPoints >= 0 && playerLiftTargetCheck(a, 0)) { // exe 0x573a60
            a.enterState(uint8_t(CritterState::Lifted));
            f.behaviour |= kCritterNoHitEntry;
        }
        break;
    }
    case CritterState::Defeated: { // exe 0x50a3c0
        a.tint = kTintFlash;
        if (a.step == 0) {
            a.step = 1;
            if (a.airborne == 0) {
                a.velocity.x = 0;
                a.velocity.y = 0;
                a.velocity.z = 0;
            }
            a.acceleration.y = 0x40;
            f.timer = 1;
        }
        --f.timer;
        if (f.timer == 0) {
            spawnDrops(a.pos, animatedStats(a.type).dropTable);
            freeActor(a);
        }
        break;
    }
    default: notPortedAt(kCritterPort, "0x509830 state (2 = lifted by the player)", a.state);
    }
    if (!(a.flags & kActorLive)) return;
    if (a.anim == a.prevAnim) animStep(a); // exe 0x5e1e80 / 0x5e1c30: the animation and the pose
    else animSet(a, a.anim, 0);
    animPose(a);
    const int8_t b = int8_t(f.behaviour);
    f.box.result = 0;
    f.box.previousX = a.prevPos.x.whole;
    f.box.previousZ = a.prevPos.z.whole;
    if (!(b & kCritterNoHitEntry)) {
        const ShortVec4& bone = bonePoint(a, 0, kCritterPort);
        const int16_t pos[3] = {bone.x, bone.y, bone.z};
        const uint32_t owner = hitOwner(a.slot) | kHitNoLockOn;
        if (CritterState(a.state) == CritterState::Defeated) {
            hitListSubmit(pos, f.box, owner | hitGroup(kHitGroupBodies) | hitRadius(0x58), kHarmlessBump);
        } else if (b & kCritterBodyAttack) {
            hitListSubmit(pos, f.box, owner | hitGroup(kHitGroupPlayerAttacks) | kHitContactMotion | hitRadius(0x48),
                          animatedStats(a.type).bodyAttack | kHitAttackHarmless | kHitAttackByPlayer);
        } else {
            uint32_t kind = kHitAttackHarmless | kHitAttackSolid, damage = animatedStats(a.type).attack;
            if (b & kCritterStrong) {
                kind = kHitAttackHarmless | kHitAttackStrong | kHitAttackBump;
                damage = 0;
            }
            if (eventState().active != 0) damage = 0;
            hitListSubmit(pos, f.box, owner | hitGroup(kHitGroupBodies) | kHitContactMotion | hitRadius(0x50),
                          kind | damage);
        }
    }
    // exe 0x5f20f0 (the shadow unless lifted) is render output
}

} // namespace

void shyCritterUpdate(const PlayerEnv& env, uint32_t actor) { critter(env, animatedActorAt(actor)); }

} // namespace od::port
