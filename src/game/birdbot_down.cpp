// The knocked-down Birdbot, class 0x492330 (see birdbot_down.hpp). States [0x892ab4]: 0 init 0x492520, 1 0x492660
// (sub-states [0x892ac4]: 0 0x492710 bounced, 1 0x492880 runs around the player, 2 0x4929d0 knocked back, 3 0x493580 a
// quarter turn, 4 0x492b10 fall, 5 0x492b80 hop), 2 0x492c20 carried (sub-states [0x892aec]: 0x492c40 at the hands,
// 0x492db0 thrown / dropped). The shadow's entry (exe 0x5f20f0) is render output, its ground probe is kept.
#include "game/actor_state.hpp"
#include "game/birdbot_down_state.hpp"
#include "game/birdbot_down.hpp"

#include "engine/data_source.hpp"
#include "game/actor_animation.hpp"
#include "game/actor_collision.hpp"
#include "game/actor_model.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_stats.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/birdbot.hpp"
#include "game/carry.hpp"
#include "game/distances.hpp"
#include "game/enemy_motion.hpp"
#include "game/fixed_point.hpp"
#include "game/hit_list.hpp"
#include "game/player/player_motion.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/shadows.hpp"
#include "game/spark_emitter.hpp"
#include "game/stage_state.hpp"
#include "game/visibility.hpp"

namespace od::port {

using namespace birdbot_down_state;

namespace {

constexpr const char* kDownPort = "birdbot down port"; // the area of its port-gap errors

// +0x15e (besides kBirdbotBounces)
constexpr uint8_t kDownNoHitEntry = 0x04, kDownNoFall = 0x08, kDownThrown = 0x10, kDownNoDamage = 0x20;
// the variant: & 0xf how it went down; kDownStaysDown never gets up, kDownHops hops at the walls
constexpr uint8_t kDownVariantMask = 0x0f, kDownStaysDown = 0x40, kDownHops = 0x80;
constexpr uint8_t kVariantBlownOff = 0, kVariantBounced = 1, kVariantRuns = 2;
constexpr uint8_t kOffScreenTicks = 0x1e;

enum class DownState : uint8_t { Init = 0, Main = 1, Carried = 2 };
enum class DownSub : uint8_t { Bounced = 0, Runs = 1, KnockBack = 2, Turn = 3, Fall = 4, Hop = 5 };
enum class DownCarriedSub : uint8_t { InHands = 0, Flying = 1 }; // exe 0x492c40 / 0x492db0

const DataTable<CollisionBoxRecord> downBox{TableId::BirdbotDownBox};
const DataTable<InteractionBoxRecord> downLiftBox{TableId::BirdbotDownLiftBox};
const DataTable<StridedScalarTable<uint8_t, 0x10, 4>> carryCries{TableId::BirdbotDownCarryCries};
constexpr uint8_t kSmokeVariant = 6; // the spark emitter's smoke
constexpr uint16_t kSoundKnock = 400, kSoundKnockHard = 0x192, kSoundCry = 0x194, kSoundThrown = 0x196;
constexpr uint16_t kSoundDown = 0x191;
constexpr int16_t kRunSpeed = -0x140;

void toSub(AnimatedActor& a, DownSub s) {
    a.subState = uint8_t(s);
    a.enterStep(0);
}
// exe 0x493a00: 1 = hit (the player's attack kinds or damage, on the ground), 2 = by a strong push; the hit kind 2 from
// far below
int damage(AnimatedActor& a, DownFields& f) {
    const uint32_t v = f.hitBox.result;
    if (f.bits & kDownNoDamage) return 0;
    f.hitKind = 0;
    int r = 0;
    if ((v & (kHitDamageMask | kHitAttackSolid | kHitAttackByPlayer | kHitAttackThrown)) != 0 && a.airborne == 0) {
        r = 1;
        f.hitKind = 1;
        if (v & kHitLevelBelow) f.hitKind = 2;
        if (v & kHitAttackStrongPush) r = 2;
    }
    return r;
}
// exe 0x493910: the walls and (not on an actor) the ground -- a variant of game/birdbot birdbotCollide: the ground's
// height is not kept and a falling one lands once the ground is above it; 1 = the ground lost
int collide(const PlayerEnv& env, AnimatedActor& a, DownFields& f) {
    f.contact = 0;
    const uint32_t t = actorTestMove(env, a); // exe 0x40f570
    if (t & 0x20000000u) f.contact |= kBirdbotInWater;
    if (int32_t(t) < 0) {
        const uint32_t r = actorPushOut(env, a); // exe 0x410960
        a.wallHit = r;
        if (int32_t(r) < 0) a.pos = a.prevPos;
        if (r & 0xf00) f.contact = kBirdbotWall;
        if ((f.bits & kBirdbotBounces) && reflectRotationOffWalls(a.rotation, r) != 0) f.contact |= kBirdbotLanded;
    }
    if (a.onPlatform == 0) {
        const int g = actorGround(env, a); // exe 0x40f190
        if (a.airborne == 0) {
            if (0x20 < g - a.pos.y.whole) {
                f.contact |= kBirdbotGroundLost;
                return 1;
            }
            a.pos.y.whole = int16_t(g);
            return 0;
        }
        if (!(f.bits & kBirdbotBounces) && g < a.pos.y.whole) {
            a.airborne = 0;
            a.pos.y.whole = int16_t(g);
            f.contact |= kBirdbotLanded;
        }
    }
    return 0;
}
// exe 0x5e3c00(contact, position): the angle from the hit's contact point to the actor
int fromContact(const AnimatedActor& a, const DownFields& f) {
    return atanAngle(float(a.pos.x.whole - f.hitBox.contact.x), float(a.pos.z.whole - f.hitBox.contact.z)) &
           kAngleMask;
}

// ---- sub-states -------------------------------------------------------------------------------------------------
void bounced(AnimatedActor& a, DownFields& f) { // exe 0x492710: bounced along its heading spinning, smoking
    switch (a.step) {
    case 0:
        f.bits = uint8_t(kDownNoDamage | kDownNoFall | kBirdbotBounces);
        a.anim = 0x11;
        a.airborne = 1;
        a.acceleration.y = 0x40;
        a.spin.y = -0x1c7;
        f.heading = a.rotation.y;
        f.timer = 0;
        a.enterStep(1);
        [[fallthrough]];
    case 1:
        a.rotation.y = int16_t(uint16_t(a.rotation.y) + uint16_t(a.spin.y));
        if (birdbotArc(a, f, f.heading) != 0) {
            a.anim = 0x12;
            a.enterStep(2);
            a.airborne = 0;
        }
        if (f.smoking != 0 && !(f.timer & 1)) {
            if (const uint32_t address = allocActor60(kActorStageClass)) { // exe 0x5e12f0
                StaticActor& s = staticActorAt(address);
                s.flags = kActorLive;
                s.type = kSparkEmitterStageType;
                s.variant = kSmokeVariant;
                s.loopFlags |= kActorSweepGroup2;
                s.pos.x.whole = a.pos.x.whole;
                s.pos.y.whole = a.pos.y.whole;
                s.pos.z.whole = a.pos.z.whole;
                SparkEmitterParams& sp = classState<SparkEmitterParams>(s);
                sp.driftX = 0;
                sp.driftZ = 0;
                sp.lifetime = 4;
            }
        }
        f.timer = uint8_t(f.timer + 1u);
        return;
    case 2:
        moveAlongYaw(a.pos, f.heading, 0, 0, a.velocity.z); // exe 0x5e36f0
        if (a.animEnded()) {
            f.timer = 0x1e;
            a.enterStep(3);
        }
        return;
    case 3:
        if (!(a.variant & kDownStaysDown)) {
            --f.timer;
            if (f.timer == 0) {
                a.anim = 0x13;
                a.enterStep(4);
            }
        }
        return;
    case 4:
        if (a.animEnded()) {
            f.smoking = 0;
            toSub(a, DownSub::Runs);
        }
        return;
    default: return;
    }
}
// exe 0x492880: runs around the player (0x600 off the line to it, either side), turns back (or hops) at the walls
void runs(AnimatedActor& a, DownFields& f) {
    const uint8_t s = a.step;
    if (s == 0) {
        a.anim = 0x14;
        a.velocity.setXY(0, 0);
        a.velocity.z = kRunSpeed;
        f.bits = 0;
        const uint32_t r = rngStep();
        a.enterStep(1);
        a.spin.y = int16_t(((0xaaaaaaaau >> (r & 0x1f)) & 1u) != 0 ? 0x600 : -0x600);
    } else if (s != 1) {
        if (s == 2) {
            --f.turnTimer;
            if (f.turnTimer == 0) {
                a.enterStep(1);
                a.spin.y = int16_t(-int(a.spin.y));
            }
        }
        integrateVelocity(a, 0, 0, a.velocity.z);
        return;
    }
    if (f.contact == 0 && (a.actorHit & 0xf00) == 0) {
        const Player& p = player();
        const int ang = angleOf(p.pos.x, p.pos.z, a.pos.x, a.pos.z); // exe 0x5e3bc0(player, actor)
        turnToward(a.rotation.y, int(uint32_t(int(a.spin.y) + ang)), 0x20); // exe 0x5e3db0
    } else if (!(a.variant & kDownHops)) {
        a.rotation.y = int16_t(uint16_t(a.rotation.y) + uint16_t(kHalfTurn));
        f.turnTimer = 0x1e;
        a.enterStep(2);
    } else {
        toSub(a, DownSub::Hop);
    }
    integrateVelocity(a, 0, 0, a.velocity.z); // exe 0x5e3690
}
void knockBack(AnimatedActor& a, DownFields& f) { // exe 0x4929d0: knocked back (anim 0x18 from the front / 0x17)
    const uint8_t s = a.step;
    if (s == 0) {
        f.bits = kDownNoDamage;
        soundPlayAtPos(kSoundKnock, a.pos);
        a.velocity.z = 0x300;
        a.acceleration.z = -0x30;
        if (f.hitKind == 2) a.velocity.z = 0x500;
        const int d = angleDiff(uint32_t(int32_t(a.rotation.y)), uint32_t(fromContact(a, f)));
        if (uint32_t(d + kQuarterTurn) < uint32_t(kHalfTurn)) {
            a.anim = 0x18;
            f.timer = 8;
            a.rotation.y = int16_t(uint16_t(a.rotation.y) + 0x20u);
            a.enterStep(1);
            return;
        }
        a.anim = 0x17;
        f.timer = 9;
        a.rotation.y = int16_t(uint16_t(a.rotation.y) + 0x20u);
        a.velocity.z = int16_t(-int(a.velocity.z));
        a.acceleration.z = int16_t(-int(a.acceleration.z));
        a.enterStep(1);
        return;
    }
    if (s == 1) {
        integrateVelocity(a, 0, 0, a.velocity.z);
        a.velocity.z = int16_t(uint16_t(a.velocity.z) + uint16_t(a.acceleration.z));
        --f.timer;
        if (f.timer == 0 || int16_t(uint16_t(a.velocity.z) ^ uint16_t(a.acceleration.z)) >= 0) a.enterStep(2);
    } else if (s != 2) {
        return;
    }
    if (a.animEnded()) toSub(a, DownSub::Runs);
}
void turn(AnimatedActor& a, DownFields& f) { // exe 0x493580: a quarter turn (anims 0x12 from frame 9, 0x13)
    const uint8_t s = a.step;
    if (s == 0) {
        a.anim = 0x12;
        f.animKeyStart = 9;
        f.bits = 0;
        a.rotation.y = int16_t(uint16_t(a.rotation.y) + uint16_t(kQuarterTurn));
        a.enterStep(1);
    } else if (s == 1) {
        if (a.animEnded()) {
            a.anim = 0x13;
            a.enterStep(2);
        }
    } else if (s == 2 && a.animEnded()) {
        toSub(a, DownSub::Runs);
    }
}
// exe 0x492b10 the fall (anim 0xf) / 0x492b80 the hop (anim 0xe): anim 0x10 on landing, then the run
void fallOrHop(AnimatedActor& a, DownFields& f, bool hop) {
    const uint8_t s = a.step;
    if (s == 0) {
        a.anim = hop ? 0xe : 0xf;
        a.acceleration.y = 0x18;
        if (hop) {
            a.velocity.y = -0x140;
            a.velocity.z = 0;
            a.airborne = 1;
        } else {
            a.velocity.y = 0;
        }
        f.bits = uint8_t(kDownNoFall | kBirdbotBounces);
        a.enterStep(1);
    } else if (s != 1) {
        if (s == 2 && a.animEnded()) toSub(a, DownSub::Runs);
        return;
    }
    if (birdbotArc(a, f, a.rotation.y) != 0) {
        a.anim = 0x10;
        a.enterStep(2);
        a.airborne = 0;
    }
    if (hop && a.velocity.y >= 0) {
        a.velocity.z = int16_t(uint16_t(a.velocity.z) - 0x20u);
        if (a.velocity.z < kRunSpeed) a.velocity.z = kRunSpeed;
    }
}

// ---- states -------------------------------------------------------------------------------------------------------
void mainState(const PlayerEnv& env, AnimatedActor& a, DownFields& f) { // exe 0x492660
    if (a.actorHit != 0) birdbotContactHits(a, f);
    if (a.wasOnPlatform != 0 && a.onPlatform == 0 && !(f.bits & kDownNoFall)) toSub(a, DownSub::Fall);
    const int d = damage(a, f);
    if (d == 1) {
        toSub(a, DownSub::KnockBack);
    } else if (d == 2) {
        a.velocity.y = -0x300;
        a.velocity.z = -0x80;
        toSub(a, DownSub::Bounced);
    }
    switch (DownSub(a.subState)) { // [0x892ac4]
    case DownSub::Bounced: bounced(a, f); break;
    case DownSub::Runs: runs(a, f); break;
    case DownSub::KnockBack: knockBack(a, f); break;
    case DownSub::Turn: turn(a, f); break;
    case DownSub::Fall: fallOrHop(a, f, false); break;
    case DownSub::Hop: fallOrHop(a, f, true); break;
    default: notPortedAt(kDownPort, "class 0x492330 sub-state", a.subState);
    }
    if (collide(env, a, f) != 0 && !(f.bits & kDownNoFall)) toSub(a, DownSub::Fall);
    if (playerLiftTargetCheck(a, 0)) { // exe 0x573a60
        a.enterState(uint8_t(DownState::Carried));
        f.bits |= kDownNoHitEntry;
        a.onPlatform = 0;
    }
}
// exe 0x492db0: thrown / dropped -- a hit turns its heading back; lands -> the main state, a quarter turn
void flying(const PlayerEnv& env, AnimatedActor& a, DownFields& f) {
    if (!(f.hitBox.result & kHitAttackSolid)) {
        if (a.actorHit != 0) {
            birdbotContactHits(a, f);
            a.hitPoints = -1;
        }
    } else if (a.hitPoints >= 0) {
        f.heading = int16_t(uint16_t(f.heading) + uint16_t(kHalfTurn));
        a.hitPoints = -1;
        f.bits |= kDownNoHitEntry;
    }
    a.rotation.x = int16_t(uint16_t(a.rotation.x) + 0x100u);
    if (birdbotArc(a, f, a.rotation.y) != 0) {
        if (a.step == 0 && !(f.contact & kBirdbotOnPlatform)) {
            integrateVelocity(a, 0, a.velocity.y, a.velocity.z);
            a.velocity.y = int16_t(uint16_t(a.velocity.y) + uint16_t(a.acceleration.y));
            a.airborne = 1;
            a.enterStep(1);
            collide(env, a, f);
            return;
        }
        a.airborne = 0;
        a.enterState(uint8_t(DownState::Main), uint8_t(DownSub::Turn));
    }
    collide(env, a, f);
}
void inHands(const PlayerEnv& env, AnimatedActor& a, DownFields& f) { // exe 0x492c40: its cries until let go
    if (a.step == 0) {
        f.timer = carryCries[rngStep() & 0xf];
        a.anim = 0x16;
        a.enterStep(1);
    }
    --f.timer;
    if (f.timer == 0) {
        soundPlayAtPos(kSoundCry, kSoundAtListener);
        f.timer = carryCries[rngStep() & 0xf];
    }
    carryAtHands(a, 0, 0, 0); // exe 0x573ab0
    const CarryRelease r = carryReleased(); // exe 0x573950
    if (r == CarryRelease::Held) return;
    if (r == CarryRelease::Thrown) { // thrown; else dropped
        int32_t v[3];
        playerAimVector(0, -0x100, -0x400, v); // exe 0x5e3e50
        a.velocity.y = int16_t(v[1]);
        a.velocity.x = 0;
        a.velocity.z = int16_t(v[2]);
        f.bits = uint8_t(kDownThrown | kBirdbotBounces);
        soundPlayAtPos(kSoundThrown, a.pos);
    } else {
        a.velocity.y = -0x200;
        a.velocity.z = -0x100;
        f.bits = kBirdbotBounces;
        soundPlayAtPos(kSoundKnockHard, a.pos);
    }
    a.acceleration.y = 0x40;
    a.rotation.y = player().rotation.y;
    f.heading = player().rotation.y;
    a.airborne = 1;
    a.hitPoints = 1;
    carryDropFromPlayer(a); // exe 0x573990
    a.subState = uint8_t(DownCarriedSub::Flying);
    a.enterStep(0);
    flying(env, a, f);
}
void init(const PlayerEnv& env, AnimatedActor& a, DownFields& f) { // exe 0x492520
    if (!bindActorModel(a, a.params[0])) {
        freeActor(a);
        return;
    }
    a.flags |= kActorDrawn;
    a.anim = 0;
    a.prevAnim = 0xff;
    const uint16_t hp = hasAnimatedStats(a.type) ? animatedStats(a.type).hitPoints : 0;
    a.maxHitPoints = int16_t(hp);
    a.hitPoints = int16_t(hp);
    a.rotation.z = 0;
    a.rotation.x = 0;
    a.collisionBox.pointAt(downBox.stateAddress());
    a.interactionBox.pointAt(downLiftBox.stateAddress());
    a.interaction = kInteractLift;
    a.airborne = 0;
    a.onPlatform = 0;
    a.clearClassBlock();
    f.offScreen = kOffScreenTicks;
    // the part set of the downed body: part 2 shown, the parts 6 / 8 / 7 hidden
    a.parts[2].flags = uint8_t(a.parts[2].flags & ~kPartHidden);
    a.parts[6].flags = uint8_t(a.parts[6].flags | kPartHidden);
    a.parts[8].flags = uint8_t(a.parts[8].flags | kPartHidden);
    a.parts[7].flags = uint8_t(a.parts[7].flags | kPartHidden);
    const uint8_t v = a.variant & kDownVariantMask;
    a.enterState(uint8_t(DownState::Main));
    if (v == kVariantBlownOff) {
        a.velocity.z = -0x80;
        a.velocity.y = -0x300;
        f.smoking = 1;
        soundPlayAtPos(kSoundDown, a.pos);
    } else if (v == kVariantBounced) {
        f.smoking = 1;
        soundPlayAtPos(kSoundDown, a.pos);
    } else if (v == kVariantRuns) {
        a.subState = uint8_t(DownSub::Runs);
    }
    mainState(env, a, f);
}

void down(const PlayerEnv& env, AnimatedActor& a) { // exe 0x492330
    DownFields& f = classState<DownFields>(a);
    a.prevAnim = a.anim;
    a.tint = kTintSpent;
    a.savePreviousPose();
    a.savePreviousContact();
    switch (DownState(a.state)) { // [0x892ab4]
    case DownState::Init: init(env, a, f); break;
    case DownState::Main: mainState(env, a, f); break;
    case DownState::Carried: // exe 0x492c20: [0x892aec]
        if (a.subState == uint8_t(DownCarriedSub::InHands)) inHands(env, a, f);
        else if (a.subState == uint8_t(DownCarriedSub::Flying)) flying(env, a, f);
        else notPortedAt(kDownPort, "class 0x492330 state 2 sub-state", a.subState);
        break;
    default: notPortedAt(kDownPort, "class 0x492330 state", a.state);
    }
    const Player& p = player();
    const int far = int(uint32_t(stage().depthRange) << 2);
    if ((f.contact & kBirdbotInWater) && distance3(a.pos, p.pos) > far) {
        freeActor(a);
        return;
    }
    if (int(a.pos.y.whole) > far) {
        freeActor(a);
        return;
    }
    if (pointVisibleOrNear(a.pos, 0x200) == 0) {
        f.offScreen = kOffScreenTicks;
    } else {
        --f.offScreen;
        if (f.offScreen == 0) {
            freeActor(a);
            return;
        }
    }
    if (!(a.flags & kActorLive)) return;
    if (a.anim == a.prevAnim) {
        animStepPose(a); // exe 0x5e1e80
    } else {
        animSetPose(a, a.anim, f.animKeyStart); // exe 0x5e1c30
        f.animKeyStart = 0;
    }
    f.hitBox.result = 0;
    f.hitBox.previousX = a.prevPos.x.whole;
    f.hitBox.previousZ = a.prevPos.z.whole;
    const AnimatedActorStats row = animatedStats(a.type);
    const uint8_t b = f.bits;
    if (!(b & kDownNoHitEntry)) {
        const int16_t pos[3] = {a.pos.x.whole, wrap16(a.pos.y.whole - 0x40), a.pos.z.whole};
        if (!(b & kDownThrown))
            hitListSubmit(pos, f.hitBox, ((uint32_t(a.slot) << 15 | row.hitHeight) << 9) | 0xb84007u, kHitAttackSolid);
        else
            hitListSubmit(pos, f.hitBox, hitOwner(a.slot) | 0x20400cu,
                          uint32_t(row.bodyAttack) | kHitAttackThrown | kHitAttackByPlayer);
    }
    // exe 0x5f20f0(a, 0x30, 0x100, 0) unless state 2 sub 0: render output; its ground probe is kept
    if (!(a.state == uint8_t(DownState::Carried) && a.subState == 0) && (a.flags & kActorDrawn))
        shadowGroundProbe(env, a);
    actorCollisionSubmitMover(a); // exe 0x4140b0
}

} // namespace

void birdbotDownUpdate(const PlayerEnv& env, uint32_t actor) { down(env, animatedActorAt(actor)); }

} // namespace od::port
