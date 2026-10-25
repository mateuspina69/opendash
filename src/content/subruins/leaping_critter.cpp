// The leaping critter (pool 0x20 class 0x4dff50, see leaping_critter.hpp; hypothesis names from the observed
// behaviour). States [0x895a08] (6 entries; index 6.. runs into the sub table 0x895a20): 0 init 0x4e00d0, 1 active
// 0x4e01c0, 2 carried 0x4e0c10 (sub-states [0x895a50]: 0x4e0c30 at the hands, 0x4e0d10 thrown / dropped), 3 dying
// 0x4e09a0, 4 timed out 0x4e0af0, 5 freed 0x468fa0. Active sub-states [0x895a20]: 0 wait 0x4e02f0, 1 wander 0x4e0370,
// 2 fall 0x4e0940, 3 nothing 0x52ef00, 4 leap 0x4e0510, 5 path 0x4e0610, 6 hurt 0x4e0820, 7 turn 0x4e07b0.
// Its variant: its model, collision boxes and stat row; variant 0 times out when the player stays away.
// Render output skipped: the shadow exe 0x5f20f0(a, 0x30 / 0x80 (variant 0 / other), 0x160, 0), not drawn while +8 /
// +9 == 2 / 0 (in the hands).
#include "content/subruins/leaping_critter.hpp"
#include "content/subruins/leaping_critter_state.hpp"

#include "engine/data_source.hpp"
#include "game/loaded_data.hpp"
#include "game/actor_animation.hpp"
#include "game/actor_model.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actor_stats.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/carry.hpp"
#include "game/distances.hpp"
#include "game/explosions.hpp"
#include "game/field_alias.hpp"
#include "game/global_class_types.hpp"
#include "game/hit_list.hpp"
#include "game/hit_points.hpp"
#include "game/pickups.hpp"
#include "game/player/player_motion.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/script_state.hpp"
#include "game/stage_smoke.hpp"
#include "game/story.hpp"
#include "game/visibility.hpp"

namespace od::port {

using namespace leaping_critter_state;

namespace {

// Its spawn parameters: the path walked first (else it wanders), its path (an index of the path table), the player's
// distance it waits for (<< 8; 0: 1000)
struct CritterParams {
    explicit CritterParams(const AnimatedActor& actor) : CritterParams(const_cast<AnimatedActor&>(actor)) {}
    explicit CritterParams(AnimatedActor& actor) : walksPath(actor.params[0]), path(actor.params[1]), wakeDistance(actor.params[2]) {}
    uint8_t& walksPath;
    FieldAlias<int8_t, uint8_t> path;
    FieldAlias<int8_t, uint8_t> wakeDistance;
};
// +0x160
constexpr uint8_t kCritterNoHitEntry = 0x01, kCritterNoDamage = 0x02, kCritterNoWallTurn = 0x04;
constexpr uint8_t kCritterNoFall = 0x08, kCritterThrownEntry = 0x10, kCritterLeaping = 0x20;
constexpr uint8_t kCritterBounces = 0x40, kCritterNotTarget = 0x80;
// +0x161
constexpr uint8_t kCritterWallX = 0x01, kCritterWallZ = 0x02, kCritterLanded = 0x04, kCritterGroundLost = 0x08;
constexpr uint8_t kCritterWalls = kCritterWallX | kCritterWallZ;

enum class CritterState : uint8_t { Init = 0, Active = 1, Carried = 2, Dying = 3, TimedOut = 4, Freed = 5 };
enum class CritterSub : uint8_t {
    Wait = 0,   // exe 0x4e02f0
    Wander = 1, // exe 0x4e0370
    Fall = 2,   // exe 0x4e0940
    Rest = 3,   // exe 0x52ef00
    Leap = 4,   // exe 0x4e0510
    Path = 5,   // exe 0x4e0610
    Hurt = 6,   // exe 0x4e0820
    Turn = 7,   // exe 0x4e07b0
};
enum class CritterCarriedSub : uint8_t { InHands = 0, Flying = 1 }; // exe 0x4e0c30 / 0x4e0d10

// one word of a path: a leg's yaw (1: the path ends, -1: the critter is freed) or its limit
struct PathWord : DataRecord {
    static constexpr uint32_t kBytes = 0x2;
    uint16_t value = 0; // +0x0
    template <class R> static PathWord load(const R& r) {
        PathWord v;
        v.value = r.template read<uint16_t>(0x0);
        return v;
    }
};
constexpr int16_t kPathEnds = 1, kPathFrees = -1;
// by the path parameter (exe data 0x8959f8: the paths' addresses, as the critter's cursor stores them: the
// address is the record's identity)
const DataTable<ScalarTable<uint32_t, 0x40>> paths{TableId::LeapingCritterPaths};
const DataTable<ScalarTable<uint16_t, 3>> puffOffsetsX{TableId::LeapingCritterPuffOffsetsX}; // the time-out's puffs
const DataTable<ScalarTable<uint16_t, 3>> puffOffsetsZ{TableId::LeapingCritterPuffOffsetsZ};
constexpr uint8_t kPuffVariant = 0xa;
constexpr uint16_t kSoundLeap = 0x243, kSoundHurt = 0x8d, kSoundTimeout = 0x244;
constexpr uint16_t kTintDyingBlink = 0x4214;

AnimatedActorStats statsOf(const AnimatedActor& a) { return animatedStats(a.type, a.variant); }
void toSub(AnimatedActor& a, CritterSub s) {
    a.subState = uint8_t(s);
    a.enterStep(0);
}

// the waddle of the wander / the path: the phase steps on; sideways the difference of the two scaled cosines at the
// new and the old phase; the move forward (exe 0x5e3690(a, x, 0, -0x280))
void waddle(AnimatedActor& a, CritterFields& f, uint32_t step) {
    const uint16_t old = uint16_t(f.waddlePhase);
    const uint32_t oi = uint32_t(int32_t(int16_t(old))) & 0xfff;
    const uint32_t nw = uint32_t(old) + step;
    f.waddlePhase = int16_t(uint16_t(nw));
    const uint32_t ni = nw & 0xfff;
    const int32_t x = ((int32_t(fixedCos(int(ni))) * 24) >> 8) & int32_t(0xfffffff0);
    const int32_t y = ((int32_t(fixedCos(int(oi))) * 24) >> 12) << 4;
    integrateVelocity(a, x - y, 0, -0x280);
}
// exe 0x4ac3b0: the arc along the yaw; landed / falling onto an actor -> 1 (a bounce halves it)
int arcStep(AnimatedActor& a, CritterFields& f, int yaw) {
    if (!(f.contact & kCritterLanded) && (a.velocity.y < 1 || a.onPlatform == 0)) {
        moveAlongYaw(a.pos, yaw, 0, a.velocity.y, a.velocity.z); // exe 0x5e36f0
        a.velocity.y = int16_t(uint16_t(a.velocity.y) + uint16_t(a.acceleration.y));
        return 0;
    }
    a.velocity.z = int16_t(a.velocity.z >> 1);
    a.velocity.y = int16_t(uint32_t(-int32_t(a.velocity.y)) >> 1);
    a.rotation.x = 0;
    a.airborne = 1;
    return 1;
}
// exe 0x4e0df0: a hit -- the flash, one hit more; the sixth / a strong one -> hurt (the hit's direction); -1 dead
int hitCheck(AnimatedActor& a, CritterFields& f) {
    const uint32_t r = f.hitBox.result;
    const uint32_t dmg = r & kHitDamageMask;
    if (dmg == 0) return 0;
    a.tint = kTintFlash;
    const uint8_t n = uint8_t(f.hits + 1u);
    f.hits = n;
    if (n > 5 || (r & (kHitLevelBelow | kHitLevelWithin | kHitLevelAbove | kHitAttackStrongPush | kHitAttackThrown))) {
        toSub(a, CritterSub::Hurt);
        f.targetYaw = f.hitBox.contact.x;
    }
    return hitPointsAdd(a, -int(dmg)) != 0 ? -1 : 0; // exe 0x5e52f0
}
// exe 0x4e0e60: the walls -> the contact bits, the ground; 1 = the ground was lost (> 0x10 below) while standing
int collide(const PlayerEnv& env, AnimatedActor& a, CritterFields& f) {
    uint32_t r = 0;
    f.contact = 0;
    if (int32_t(actorTestMove(env, a)) < 0) { // exe 0x40f570
        r = actorPushOut(env, a);              // exe 0x410960
        a.wallHit = r;
        if (int32_t(r) < 0) a.pos = a.prevPos;
        if (r & 0x300) f.contact |= kCritterWallX;
        if (r & 0xc00) f.contact |= kCritterWallZ;
        if ((f.bits & kCritterBounces) && reflectRotationOffWalls(a.rotation, r) != 0) // exe 0x410740
            f.contact |= kCritterLanded;
    }
    const int g = actorGround(env, a); // exe 0x40f190
    if (a.airborne == 0) {
        if (g - a.pos.y.whole > 0x10) {
            f.contact |= kCritterGroundLost;
            return 1;
        }
        a.pos.y.whole = int16_t(g);
        return 0;
    }
    if (!(f.bits & kCritterBounces) && (r & 0x3000)) {
        a.airborne = 0;
        a.pos.y.whole = int16_t(g);
        f.contact |= kCritterLanded;
    }
    return 0;
}

// ---- the active sub-states ------------------------------------------------------------------------------------------
// exe 0x4e02f0: waits for the player within its wake distance (variant 0 out of patience: timed out)
void waitForPlayer(AnimatedActor& a, CritterFields& f) {
    CritterParams params(a);
    if (a.step == 0) {
        f.bits = 0;
        a.enterStep(1);
        a.anim = 0;
    }
    const int32_t e = params.wakeDistance;
    const int32_t d = e != 0 ? e << 8 : 0x3e8;
    if (f.nearPatience < 0 && a.variant == 0) a.enterState(uint8_t(CritterState::TimedOut));
    if (planarDistance(a.pos, player().pos) >= d) return; // exe 0x5e3b00
    toSub(a, params.walksPath == 0 ? CritterSub::Wander : CritterSub::Path);
}
// exe 0x4e0370: a random turn every 20 ticks; the player near and ahead -> the leap
void wander(AnimatedActor& a, CritterFields& f) {
    if (a.step == 0) {
        f.bits = 0;
        a.enterStep(1);
        a.anim = 2;
        f.timer = 0x14;
        f.targetYaw = a.rotation.y;
        f.waddlePhase = 0;
    }
    if (f.nearPatience < 0 || f.visiblePatience < 0) {
        if (a.variant == 0) a.enterState(uint8_t(CritterState::TimedOut));
        else toSub(a, CritterSub::Wait);
        return;
    }
    --f.timer;
    if (f.timer == 0) {
        f.timer = 0x14;
        const uint32_t k = crtRand() & 3;
        if (k == 2) {
            f.targetYaw = int16_t(uint16_t((crtRand() & 0x7f) + uint16_t(a.rotation.y) + 0x200u));
        } else if (k == 3) {
            const uint32_t r = crtRand() & 0x7f;
            f.targetYaw = int16_t(uint16_t(uint32_t(uint16_t(a.rotation.y)) - r - 0x200u));
        }
    }
    if (planarDistance(a.pos, player().pos) < 0x258 && (a.variant == 0 || f.cooldown == 0)) {
        const int ang = angleOf(player().pos, a.pos);                       // exe 0x5e3bc0
        const int d = angleDiff(uint32_t(int32_t(a.rotation.y)), uint32_t(ang)); // exe 0x5e3d80
        if (d > -0x200 && d < 0x200) {
            toSub(a, CritterSub::Leap);
            f.targetYaw = int16_t(ang);
        }
    }
    turnYawTo(a.rotation.y, uint32_t(int32_t(f.targetYaw)), 0x100); // exe 0x5e3df0
    waddle(a, f, 0x100);
}
// exe 0x4e0940: falls along its yaw until it lands -> the wait
void fall(AnimatedActor& a, CritterFields& f) {
    switch (a.step) {
    case 0:
        f.bits = kCritterBounces | kCritterNoFall | kCritterNoWallTurn | kCritterNoHitEntry;
        a.step = 1;
        a.anim = 0;
        a.velocity.x = 0;
        a.velocity.y = 0;
        a.velocity.z = 0;
        a.acceleration.y = 0x40;
        [[fallthrough]];
    case 1:
        if (arcStep(a, f, a.rotation.y)) {
            a.airborne = 0;
            a.velocity.y = 0;
            toSub(a, CritterSub::Wait);
        }
        return;
    default: return;
    }
}
// exe 0x4e0510 (jump table 0x4e0600): a crouch, the leap at the player (its sound), the landing -> the wander
void leap(AnimatedActor& a, CritterFields& f) {
    switch (a.step) {
    case 0:
        f.bits = kCritterLeaping;
        a.enterStep(1);
        a.anim = 6;
        return;
    case 1:
        if (a.animEnded()) {
            a.enterStep(2);
            a.airborne = 1;
            a.anim = 7;
            a.velocity.x = 0;
            a.velocity.y = -0x180;
            a.velocity.z = -0x200;
            a.acceleration.y = 0x40;
            f.bits |= kCritterBounces;
            soundPlayAtPos(kSoundLeap, a.pos); // exe 0x5d5380
        }
        turnYawTo(a.rotation.y, uint32_t(int32_t(f.targetYaw)), 0x40);
        return;
    case 2:
        if (arcStep(a, f, a.rotation.y)) {
            a.enterStep(3);
            a.airborne = 0;
            a.anim = 0xc;
            a.velocity.y = 0;
            a.velocity.z = 0;
            f.bits &= uint8_t(~kCritterBounces);
        }
        return;
    case 3:
        if (a.animEnded()) {
            a.enterState(uint8_t(CritterState::Active), uint8_t(CritterSub::Wander));
            f.cooldown = 0x96;
        }
        return;
    default: return;
    }
}
// exe 0x4e0610: walks its path's (yaw, limit) legs; the end word -> the wait, the free word -> freed
void walkPath(const PlayerEnv& env, AnimatedActor& a, CritterFields& f) {
    switch (a.step) {
    case 0: {
        if (scriptState().flags != 0) return;
        f.bits = kCritterBounces | kCritterNoFall | kCritterNoDamage;
        a.enterStep(1);
        a.anim = 2;
        f.waddlePhase = 0;
        uint32_t p = f.pathCursor;
        const uint16_t v = dataRecord<PathWord>(p, PathWord::kBytes).value;
        p += 2;
        f.targetYaw = int16_t(v);
        if (int16_t(v) == kPathFrees) {
            a.enterState(uint8_t(CritterState::Freed));
            return;
        }
        if (int16_t(v) == kPathEnds) {
            a.subState = uint8_t(CritterSub::Wait);
            f.nearPatience = 0x5a;
            a.restartStep();
            CritterParams(a).walksPath = 0;
            return;
        }
        f.pathLimit = int16_t(dataRecord<PathWord>(p, PathWord::kBytes).value);
        p += 2;
        f.pathCursor = p;
        break;
    }
    case 1:
        if (turnYawTo(a.rotation.y, uint32_t(int32_t(f.targetYaw)), 0x100) == 0) a.enterStep(2);
        break;
    case 2: {
        const int32_t t = f.targetYaw;
        if (t > 0x800) {
            if (t == 0xc00 && a.pos.x.whole >= f.pathLimit) a.step = 0;
        } else if (t == 0x800) {
            if (a.pos.z.whole >= f.pathLimit) a.step = 0;
        } else if (t == 0) {
            if (a.pos.z.whole <= f.pathLimit) a.step = 0;
        } else if (t == 0x400) {
            if (a.pos.x.whole <= f.pathLimit) a.step = 0;
        }
        break;
    }
    default: break;
    }
    a.pos.y.whole = int16_t(actorGround(env, a)); // exe 0x40f190
    waddle(a, f, 0x80);
}
// exe 0x4e0820 (jump table 0x4e0924): knocked back (its sound); no hit points left -> dying
void hurt(AnimatedActor& a, CritterFields& f) {
    switch (a.step) {
    case 0:
        f.bits = kCritterBounces | kCritterNoDamage;
        a.enterStep(1);
        a.anim = 3;
        a.rotation.y = f.targetYaw;
        a.airborne = 1;
        a.velocity.x = 0;
        a.velocity.z = -0x4c;
        a.velocity.y = -0x6d5;
        a.acceleration.y = 0x247;
        soundPlayAtPos(kSoundHurt, a.pos);
        if (a.hitPoints < 0) f.bits |= kCritterNoFall;
        else f.bits &= uint8_t(~kCritterNoFall);
        return;
    case 1:
        if (arcStep(a, f, a.rotation.y)) {
            a.enterStep(2);
            a.airborne = 0;
            a.anim = 4;
            f.timer = 5;
        }
        return;
    case 2:
        if (!a.animEnded()) return;
        if (a.hitPoints < 0) {
            a.enterState(uint8_t(CritterState::Dying));
            return;
        }
        --f.timer;
        if (f.timer == 0) {
            a.enterStep(3);
            a.anim = 5;
        }
        return;
    case 3:
        if (a.animEnded()) toSub(a, CritterSub::Wander);
        return;
    default: return;
    }
}
// exe 0x4e07b0: a spin of +-0x100 a tick for 4..11 ticks -> the wander
void turn(AnimatedActor& a, CritterFields& f) {
    if (a.step == 0) {
        f.bits = kCritterNoWallTurn;
        a.enterStep(1);
        a.anim = 1;
        f.timer = int16_t((crtRand() & 7) + 4u);
        f.turnRate = 0x100;
        if (crtRand() & 1) f.turnRate = -0x100;
    }
    --f.timer;
    if (f.timer == 0) toSub(a, CritterSub::Wander);
    a.rotation.y = int16_t(uint16_t(uint16_t(a.rotation.y) + uint16_t(f.turnRate)));
}

// ---- states ---------------------------------------------------------------------------------------------------------
void init(AnimatedActor& a, CritterFields& f) { // exe 0x4e00d0
    if (!bindActorModel(a, a.variant)) { // exe 0x5e0bc0
        freeActor(a);                    // exe 0x5e1490
        return;
    }
    a.anim = 0;
    a.prevAnim = 0xff;
    a.runFlags |= kActorRunMarked;
    const uint16_t hp = hasAnimatedStats(a.type) ? uint16_t(statsOf(a).hitPoints) : 0;
    a.maxHitPoints = int16_t(hp);
    a.hitPoints = int16_t(hp);
    a.interaction = kInteractLift;
    a.airborne = 0;
    a.onPlatform = 0;
    f.waddlePhase = 0;
    a.collisionBox.pointAt(stateAddress(TableId::LeapingCritterBoxes) + uint32_t(a.variant) * 12);
    a.interactionBox.pointAt(stateAddress(TableId::LeapingCritterLiftBoxes) + uint32_t(a.variant) * 12);
    a.clearClassBlock(); // exe 0x40dc40
    f.hitBox.previousX = a.pos.x.whole;
    f.hitBox.previousZ = a.pos.z.whole;
    f.hitBox.result = 0;
    f.nearPatience = 0x12c;
    f.cooldown = 0x96;
    f.pathCursor = paths[size_t(int32_t(CritterParams(a).path.get()))];
    a.enterState(uint8_t(CritterState::Active));
}
void active(const PlayerEnv& env, AnimatedActor& a, CritterFields& f) { // exe 0x4e01c0
    a.tint = kTintNeutral;
    if (a.variant == 0) {
        if (a.subState != 0 && CritterParams(a).walksPath == 0) --f.nearPatience;
    } else {
        if (f.cooldown != 0) --f.cooldown;
        if (a.subState != 0) {
            if (planarDistance(a.pos, player().pos) < 0x5dc) --f.nearPatience;
            if (pointVisibleOrNear(a.pos, 0x200) != 0) --f.visiblePatience; // exe 0x5e59a0
        } else {
            f.nearPatience = 0x78;
            f.visiblePatience = 0x96;
        }
    }
    switch (CritterSub(a.subState)) { // table 0x895a20
    case CritterSub::Wait: waitForPlayer(a, f); break;
    case CritterSub::Wander: wander(a, f); break;
    case CritterSub::Fall: fall(a, f); break;
    case CritterSub::Rest: break; // exe 0x52ef00: ret
    case CritterSub::Leap: leap(a, f); break;
    case CritterSub::Path: walkPath(env, a, f); break;
    case CritterSub::Hurt: hurt(a, f); break;
    case CritterSub::Turn: turn(a, f); break;
    default: notPortedAt(kStoryPort, "0x4e01c0 sub-state (table 0x895a20 runs into the data 0x895a40)", a.subState);
    }
    if (!(f.bits & kCritterNoDamage)) hitCheck(a, f); // exe 0x4e0df0 (its result unused here)
    if (collide(env, a, f) != 0) {
        if (!(f.bits & kCritterNoFall)) {
            a.subState = uint8_t(CritterSub::Fall);
            a.airborne = 1;
            a.restartStep();
        }
    } else if (!(f.bits & kCritterNoWallTurn) && a.airborne == 0 && (f.contact & kCritterWalls)) {
        a.subState = uint8_t(CritterSub::Turn);
        a.restartStep();
    }
    if (playerLiftTargetCheck(a, 0)) { // exe 0x573a60: lifted -> carried
        a.enterState(uint8_t(CritterState::Carried));
        f.bits |= kCritterNoHitEntry;
        a.runFlags |= kActorRunMarked;
    }
}
// exe 0x4e0d10: thrown / dropped -- tumbles (animations 0xa / 0xb), lands (0xc) -> the wander
void flying(const PlayerEnv& env, AnimatedActor& a, CritterFields& f) {
    if (f.hitBox.result & kHitAttackSolid) {
        a.rotation.y += 0x800u;
        if (a.hitPoints >= 0) {
            a.hitPoints = -1;
            f.bits |= kCritterNoHitEntry;
        }
    }
    switch (a.stepCounter) {
    case 0:
        ++a.stepCounter;
        f.timer = 9;
        a.anim = 0xa;
        [[fallthrough]];
    case 1:
        --f.timer;
        if (f.timer == 0) {
            a.anim = 0xb;
            ++a.stepCounter;
        }
        break;
    case 3:
        if (a.animEnded()) a.enterState(uint8_t(CritterState::Active), uint8_t(CritterSub::Wander));
        break;
    default: break;
    }
    if (arcStep(a, f, a.rotation.y + 0x800)) {
        a.stepCounter = 3;
        a.airborne = 0;
        a.velocity.y = 0;
        a.velocity.z = 0;
        a.anim = 0xc;
        f.bits &= uint8_t(~kCritterBounces);
    }
    collide(env, a, f); // exe 0x4e0e60
}
// exe 0x4e0c30: at the player's hands until thrown / dropped
void inHands(const PlayerEnv& env, AnimatedActor& a, CritterFields& f) {
    if (a.step == 0) {
        f.bits = kCritterBounces | kCritterNoHitEntry;
        a.anim = 9;
        a.enterStep(1);
    }
    carryAtHands(a, 0, 0, 0);               // exe 0x573ab0
    const CarryRelease r = carryReleased(); // exe 0x573950
    if (r == CarryRelease::Held) return;
    if (r == CarryRelease::Thrown) {
        int32_t v[3];
        playerAimVector(0, -0x100, -0x400, v); // exe 0x5e3e50
        a.anim = 3;
        a.velocity.x = 0;
        a.velocity.y = int16_t(v[1]);
        a.velocity.z = int16_t(v[2]);
        f.bits = kCritterBounces | kCritterThrownEntry;
    } else {
        a.velocity.y = -0x200;
        a.velocity.z = -0x100;
        f.bits = kCritterBounces;
    }
    a.acceleration.y = 0x40;
    a.rotation.y = int16_t(uint16_t(uint16_t(player().rotation.y) + 0x800u));
    a.airborne = 1;
    a.hitPoints = 1;
    carryDropFromPlayer(a); // exe 0x573990
    a.subState = uint8_t(CritterCarriedSub::Flying);
    a.enterStep(0);
    flying(env, a, f);
}
void carried(const PlayerEnv& env, AnimatedActor& a, CritterFields& f) { // exe 0x4e0c10
    switch (CritterCarriedSub(a.subState)) { // table 0x895a50
    case CritterCarriedSub::InHands: inHands(env, a, f); break;
    case CritterCarriedSub::Flying: flying(env, a, f); break;
    default: notPortedAt(kStoryPort, "0x4e0c10 sub-state (table 0x895a50 runs into the data 0x895a58)", a.subState);
    }
}
// exe 0x4e09a0: the explosion (static global type 8), the blink, the fall; the drops, freed
void dying(const PlayerEnv& env, AnimatedActor& a, CritterFields& f) {
    a.tint = kTintFlash;
    if (a.subState == 0) {
        f.bits = kCritterNoFall | kCritterNoWallTurn | kCritterNoDamage;
        a.subState = 1;
        f.timer = 0x14;
        if (a.airborne == 0) {
            a.velocity.x = 0;
            a.velocity.y = 0;
            a.velocity.z = 0;
        } else {
            f.timer = 8;
        }
        a.acceleration.y = 0x40;
        const uint32_t address = allocActor60(0); // exe 0x5e1300
        if (address) {
            StaticActor& x = staticActorAt(address);
            ExplosionParams e(x);
            x.flags = kActorLive;
            x.type = typeByte(GlobalStaticType::Explosion);
            x.variant = 0;
            x.loopFlags |= kActorSweepGroup2;
            x.pos.x.whole = a.pos.x.whole;
            x.pos.y.whole = int16_t(uint16_t(uint16_t(a.pos.y.whole) - 0x60u));
            x.pos.z.whole = a.pos.z.whole;
            e.spread = 0x30;
            e.flashes = uint16_t(f.timer);
        }
    }
    if (uint8_t(f.timer) & 1) a.tint = kTintDyingBlink;
    --f.timer;
    const uint16_t t = uint16_t(f.timer);
    if (t == 0) {
        spawnDrops(a.pos, statsOf(a).dropTable); // exe 0x5f1270
        freeActor(a);                            // exe 0x5e1490
        return;
    }
    if (t == 0xa) f.bits |= kCritterNotTarget;
    integrateVelocity(a, a.velocity.x, a.velocity.y, a.velocity.z); // exe 0x5e3690
    if (a.airborne != 0) a.velocity.y = int16_t(uint16_t(a.velocity.y) + uint16_t(a.acceleration.y));
    collide(env, a, f); // exe 0x4e0e60
    if (a.wasAirborne != 0 && a.airborne == 0) {
        a.velocity.z = 0;
        a.velocity.x = 0;
    }
}
// exe 0x4e0af0: 15 ticks, three smoke puffs (static stage type 3), the sound, a fade out, freed
void timedOut(AnimatedActor& a, CritterFields& f) {
    switch (a.step) {
    case 0:
        f.bits = kCritterNoFall | kCritterNoHitEntry | kCritterNoDamage;
        a.step = 1;
        a.anim = 0;
        f.timer = 0xf;
        [[fallthrough]];
    case 1:
        --f.timer;
        if (f.timer != 0) return;
        f.bits |= kCritterNotTarget;
        for (size_t k = 0; k < puffOffsetsX.size(); ++k) {
            const uint32_t address = allocActor60(kActorStageClass); // exe 0x5e12f0
            if (!address) continue;
            StaticActor& puff = staticActorAt(address);
            SmokeParams smoke(puff);
            puff.flags = kActorLive;
            puff.type = kSmokeType;
            puff.variant = kPuffVariant;
            puff.loopFlags |= kActorSweepGroup2;
            puff.pos.x.whole = int16_t(uint16_t(puffOffsetsX[k] + uint16_t(a.pos.x.whole)));
            puff.pos.y.whole = int16_t(uint16_t(uint16_t(a.pos.y.whole) - 0x32u));
            smoke.driftX = 0;
            smoke.driftZ = 0;
            smoke.life = 0xa;
            puff.pos.z.whole = int16_t(uint16_t(puffOffsetsZ[k] + uint16_t(a.pos.z.whole)));
        }
        a.step = 2;
        f.timer = 0x10;
        a.semiTransparency = semiTransparencyBits(1, 0); // exe 0x5e1a00(a, 1, 0)
        soundPlayAtPos(kSoundTimeout, a.pos);
        return;
    case 2: {
        --f.timer;
        const int32_t t = f.timer;
        if (t == 0) freeActor(a); // exe 0x5e1490
        a.tint = uint16_t(((t << 5 | t) << 5) | t);
        return;
    }
    default: return;
    }
}

void leapingCritter(const PlayerEnv& env, AnimatedActor& a) { // exe 0x4dff50
    CritterFields& f = classState<CritterFields>(a);
    a.prevAnim = a.anim;
    a.savePreviousPose();
    a.savePreviousContact();
    switch (CritterState(a.state)) { // table 0x895a08
    case CritterState::Init: init(a, f); break;
    case CritterState::Active: active(env, a, f); break;
    case CritterState::Carried: carried(env, a, f); break;
    case CritterState::Dying: dying(env, a, f); break;
    case CritterState::TimedOut: timedOut(a, f); break;
    case CritterState::Freed: freeActor(a); break; // exe 0x468fa0 -> 0x5e1490
    default: notPortedAt(kStoryPort, "0x4dff50 state (table 0x895a08 runs into the sub table 0x895a20)", a.state);
    }
    if (!(a.flags & kActorLive)) return;
    if (a.anim == a.prevAnim) animStepPose(a); // exe 0x5e1e80
    else animSetPose(a, a.anim, 0);            // exe 0x5e1c30
    const AnimatedActorStats row = statsOf(a);
    const uint32_t mode = f.bits;
    f.hitBox.result = 0;
    f.hitBox.previousX = a.prevPos.x.whole;
    f.hitBox.previousZ = a.prevPos.z.whole;
    if (!(mode & kCritterNoHitEntry)) {
        const ShortVec4& bp = bonePoint(a, 0, kStoryPort); // exe 0x5e1810
        const int16_t pos[3] = {bp.x, bp.y, bp.z};
        uint32_t param, param2;
        if (!(mode & kCritterThrownEntry)) {
            param = ((((uint32_t(a.slot) << 12 | (mode & kCritterNotTarget) | 0x508u) << 3) | row.hitHeight) << 9) |
                    (uint32_t(row.hitSize) >> 2);
            param2 = uint32_t(row.attack) | kHitAttackSolid;
        } else {
            param = (uint32_t(row.hitSize) >> 2) | (uint32_t(a.slot) << 24) | 0x288000u;
            param2 = uint32_t(row.shotAttack) | kHitAttackByPlayer;
        }
        hitListSubmit(pos, f.hitBox, param, param2); // exe 0x5e4120
    }
    // u16 +8 != 2 (not in the hands): exe 0x5f20f0(a, variant ? 0x80 : 0x30, 0x160, 0): the shadow (render output)
}

} // namespace

void leapingCritterUpdate(const PlayerEnv& env, uint32_t actor) { leapingCritter(env, animatedActorAt(actor)); }

} // namespace od::port
