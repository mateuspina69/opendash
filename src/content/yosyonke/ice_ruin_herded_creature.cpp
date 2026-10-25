// The ice ruin's herded creature (exe 0x4a7b70, see ice_ruin_herded_creature.hpp), one routine per exe routine. Names
// are hypotheses from the observed behaviour. The shadow (exe 0x5f20f0(a, 0xc0, 0x100, 0) while drawn) is render
// output and left out.
//
// States [0x893d78 + state * 4]: 0 0x4a7be0 init, 1 0x4a7d20 (the hit test, the behaviours [0x893d84 + sub * 4], the
// hit entry, the animation, the footsteps), 2 0x468fa0 free, 3..18 = the behaviours 0..15 directly. The pen posts:
// the three points (-0x800, 0x800) / (0x800, 0x800) / (0x800, -0x800).
#include "content/yosyonke/ice_ruin_herded_creature.hpp"
#include "content/yosyonke/ice_ruin_herded_creature_state.hpp"

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
#include "game/camera.hpp"
#include "game/distances.hpp"
#include "game/enemy_motion.hpp"
#include "game/enemy_neighbours.hpp"
#include "game/explosions.hpp"
#include "game/field_alias.hpp"
#include "game/game_state.hpp"
#include "game/global_class_types.hpp"
#include "game/hit_list.hpp"
#include "game/pickups.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/projection_state.hpp"
#include "game/random.hpp"
#include "game/story.hpp"
#include "game/story_flags.hpp"

using namespace od::port::ice_ruin_herded_creature_state;

namespace od::port {

using namespace ice_ruin_herded_creature_state;

namespace {

enum class HerdState : uint8_t { Init = 0, Main = 1, Free = 2, FirstDirect = 3, LastDirect = 18 };
enum class Herd : uint8_t {
    LookAround = 0, RunOff = 1, Chase = 2, Bounced = 3, Fall = 4, Brake = 5, GetUp = 6, PenTaken = 7,
    BouncedRunAgain = 8, TurnAwayRun = 9, MakeWay = 0xa, FellInPen = 0xb, LookAroundAgain = 0xc, KnockedBack = 0xd,
    PlacedPenned = 0xe, AtGates = 0xf,
};
// its bits
constexpr uint8_t kHerdAirborne = 0x01;
constexpr uint8_t kHerdPenned = 0x02;       // no hit entry, no hit test
constexpr uint8_t kHerdRunning = 0x04;      // the chase's run (the faster turns)
constexpr uint8_t kHerdDying = 0x08;        // (the hit test clears it every tick: the dead branch never runs)
constexpr uint8_t kHerdRetreatEnded = 0x10;
constexpr uint8_t kHerdHit = 0x20;          // hit this tick / steer off the posts
constexpr uint8_t kHerdAtGates = 0x80;

// the placed-penned variant's index (its row of the pens' places)
struct HerdParams {
    explicit HerdParams(const AnimatedActor& actor) : HerdParams(const_cast<AnimatedActor&>(actor)) {}
    explicit HerdParams(AnimatedActor& actor) : penIndex(actor.params[2]) {}
    FieldAlias<int8_t, uint8_t> penIndex;
};

const DataTable<CollisionBoxRecord> kHerdBox{TableId::IceRuinHerdedCreatureHerdBox};
const DataTable<ScalarTable<int16_t, 9>> kPennedPlaces{TableId::IceRuinHerdedCreaturePennedPlaces}; // {x, z, yaw} by the pen index
const DataTable<ScalarTable<int16_t, 3>> kPostX{TableId::IceRuinHerdedCreaturePostX};
const DataTable<ScalarTable<int16_t, 3>> kPostZ{TableId::IceRuinHerdedCreaturePostZ};
constexpr uint32_t kPostCount = 3;
// the exe's globals the init writes: the model variant it binds, the index of the penned place it reads
constexpr uint8_t kHerdModelVariant = 2;
constexpr uint32_t kFlagPenA = 0x275, kFlagPenB = 0x276, kFlagPenC = 0x277;
constexpr int16_t kTurnSlow = 0x40, kTurnFast = 0x70;
constexpr int16_t kRunSpeed = int16_t(0xfe00), kChargeSpeed = int16_t(0xfc40);
constexpr uint16_t kLungeSound = 0x172, kBrakeSound = 0x173, kBounceSound = 0x246, kPenSound = 0x247,
                   kFootstepSound = 0x171;
constexpr int kChaseRange = 0xa00;
constexpr uint16_t kTintPenned = 0x7f18;

int yawDiff(const AnimatedActor& actor, int angle) { // exe 0x5e3d80(yaw, angle)
    return angleDiff(uint32_t(int32_t(actor.rotation.y)), uint32_t(angle));
}
int angleFromPlayer(const AnimatedActor& actor) { return angleOf(player().pos, actor.pos); }
void enterHerd(AnimatedActor& actor, Herd h) { // the behaviour with the step and the step counter cleared
    actor.subState = uint8_t(h);
    actor.restartStep();
}

// exe 0x4a9210(actor, mask): the ground / wall step: 0 on the ground (snapped), 1 the ground fell away (airborne), 2
// blocked (a wall: back to the previous position when the push-out fails) or airborne
int herdGround(const PlayerEnv& env, AnimatedActor& actor, HerdFields& f, uint32_t mask) {
    if (actor.airborne == 0) {
        const int16_t p[3] = {actor.pos.x.whole, actor.pos.y.whole, actor.pos.z.whole};
        const int y = actor.pos.y.whole;
        const int g = probeGroundAt(env, p, actor.collisionBox, y - int(actor.prevPos.y.whole), mask); // exe 0x40f1c0
        if (uint32_t(g - y + 0x3c) <= 0x78u) {
            actor.pos.y = int32_t(uint32_t(g) << 16);
            return 0;
        }
        if (y <= g) {
            actor.airborne = 1;
            f.bits |= kHerdAirborne;
            return 1;
        }
        if (int32_t(actorPushOutMask(env, actor, mask)) < 0) actor.pos = actor.prevPos; // exe 0x410980
        return 2;
    }
    const uint32_t r = actorPushOutMask(env, actor, mask);
    if (int32_t(r) < 0) {
        actor.pos = actor.prevPos;
        return 2;
    }
    if (r & 0x1000) {
        actor.airborne = 0;
        f.bits &= uint8_t(~kHerdAirborne);
    }
    return 2;
}
// exe 0x4a9300: the body's hit entry at (x, y - 0xa0, z) (the projection scratch's point), its damage the row's attack
void herdHitEntry(AnimatedActor& actor, HerdFields& f) {
    f.hit.previousX = actor.prevPos.x.whole;
    f.hit.previousZ = actor.prevPos.z.whole;
    f.hit.result = 0;
    if (!hasAnimatedStats(actor.type)) notPorted(kStoryPort, "FUN_004a9300: no type row (the exe reads address 1)");
    const uint32_t dmg = animatedStats(actor.type, actor.subtype).attack;
    LaneVec4& point = projectionScratch().point;
    point.x = actor.pos.x.whole;
    point.y = int16_t(uint16_t(uint16_t(actor.pos.y.whole) - 0xa0u));
    point.z = actor.pos.z.whole;
    const int16_t pos[3] = {point.x, point.y, point.z};
    hitListSubmit(pos, f.hit, uint32_t(actor.slot) << 24 | 0x408040u, dmg | 0x280000u); // exe 0x5e4120
}
// exe 0x4a9390: a hit this tick (no hit points are taken): 2 when the front hit is armed and the hit came from within
// 0x100 of the yaw (its damage kept), 1 other hits, 0 none; the dying bit cleared
int herdHit(AnimatedActor& actor, HerdFields& f) {
    const uint32_t box = f.hit.result;
    const uint32_t dmg = box & 0xfff;
    const uint8_t b = f.bits & uint8_t(~kHerdDying);
    f.bits = b;
    if (!(box & 0x1c0000) || dmg == 0) return 0;
    f.bits = uint8_t(b | kHerdHit);
    if (f.frontHitArmed != 0 && uint32_t(yawDiff(actor, f.hitYaw) + 0x100) < 0x200u) {
        f.lastDamage = uint16_t(dmg);
        return 2;
    }
    return 1;
}
// exe 0x4a9670: near a pen gate (x / z outside +-0x700 by quadrant): the yaw into the pen (0 / 0x400 / 0x800 /
// 0xc00), -1 elsewhere
int penDir(const AnimatedActor& actor) {
    const int x = actor.pos.x.whole, z = actor.pos.z.whole;
    if (x > 0x700) {
        if (x < 0x900) return uint32_t(z + 0x700) <= 0xe00u ? -1 : 0x400;
        if (z > 0x700) return 0;
        if (z < -0x700) return 0x800;
        return -1;
    }
    if (x >= -0x700) return -1;
    if (x < -0x900) return z <= 0x700 ? -1 : 0;
    return z <= 0x700 ? -1 : 0xc00;
}
// exe 0x4a96f0: inside a pen corner: the yaw toward the origin +-0x100 (CRT rand), -1 elsewhere
int gateDir(const AnimatedActor& actor) {
    const int x = actor.pos.x.whole, z = actor.pos.z.whole;
    if (!((x < -0x300 && z > 0x300) || (x > 0x300 && (z > 0x300 || z < -0x300)))) return -1;
    const int ang = angleOf(0, 0, actor.pos.x, actor.pos.z); // from the exe's zeroed stack point
    return int(((crtRand() & 0xff) + uint32_t(ang) - 0x200u) & 0xfff);
}
// exe 0x4a9410: back to the chase -- away from the gates and within 0xa00 of the player the chase, else look around
void resume(AnimatedActor& actor) {
    if (penDir(actor) < 0 && gateDir(actor) < 0 &&
        uint32_t(planarDistance(actor.pos, player().pos)) <= uint32_t(kChaseRange)) {
        actor.spin.y = kTurnFast;
        actor.velocity.z = kChargeSpeed;
        actor.anim = 0;
        enterHerd(actor, Herd::Chase);
        return;
    }
    actor.velocity.z = kRunSpeed;
    actor.anim = 0;
    enterHerd(actor, Herd::LookAround);
}
// a pen post as a 16.16 point at the creature's height (the exe's stack point; its fractions uninitialised there: 0)
void postPoint(const AnimatedActor& actor, uint32_t k, int32_t out[3]) {
    out[0] = int32_t(uint32_t(uint16_t(kPostX[k])) << 16);
    out[1] = actor.pos.y;
    out[2] = int32_t(uint32_t(uint16_t(kPostZ[k])) << 16);
}
// exe 0x4a9480(actor, tol, dist): a pen post ahead within tol of the yaw and nearer than dist (than 0x480 when the
// player, watched within 0x40 ahead and not above, is nearer than dist) -> 1
int blocked(const AnimatedActor& actor, const HerdFields& f, int tol, int dist) {
    const Player& p = player();
    for (uint32_t k = 0; k < kPostCount; ++k) {
        int32_t post[3];
        postPoint(actor, k, post);
        int playerDist = 0x7fffffff;
        if (f.watchesPlayer != 0 && uint32_t(yawDiff(actor, angleFromPlayer(actor)) + 0x40) <= 0x80u &&
            p.pos.y.whole <= actor.pos.y.whole)
            playerDist = planarDistance(actor.pos, p.pos);
        if (uint32_t(yawDiff(actor, angleOf(post[0], post[2], actor.pos.x, actor.pos.z)) + tol) > uint32_t(tol * 2))
            continue;
        const int d = planarDistance(actor.pos, post);
        if (dist > playerDist) {
            if (d < 0x480) return 1;
        } else if (d < dist) {
            return 1;
        }
    }
    return 0;
}
// exe 0x4a9570(actor, tol): away from the gates, hit this tick: a post at least 0x900 away and off the yaw while the
// player is off the yaw too -> the chase, 1; else the hit bit cleared
int avoid(AnimatedActor& actor, HerdFields& f, int tol) {
    if (penDir(actor) >= 0) return 0;
    if (gateDir(actor) >= 0) return 0;
    for (uint32_t k = 0; k < kPostCount; ++k) {
        const uint8_t b = f.bits;
        int32_t post[3];
        postPoint(actor, k, post);
        if (!(b & kHerdHit)) continue;
        if (uint32_t(planarDistance(actor.pos, post)) >= 0x900u &&
            uint32_t(yawDiff(actor, angleOf(post[0], post[2], actor.pos.x, actor.pos.z)) + tol) > uint32_t(tol * 2) &&
            uint32_t(yawDiff(actor, angleFromPlayer(actor)) + tol) > uint32_t(tol * 2)) {
            actor.spin.y = kTurnFast;
            actor.velocity.z = 0;
            enterHerd(actor, Herd::Chase);
            return 1;
        }
        f.bits &= uint8_t(~kHerdHit);
    }
    return 0;
}
bool turned(AnimatedActor& actor, int angle) { // exe 0x5e3df0 by the turn step, then the exe's unsigned window test
    const int step = actor.spin.y;
    const int d = turnYawTo(actor.rotation.y, uint32_t(angle), step);
    return uint32_t(d + step) < uint32_t(step * 2);
}
bool snapped(AnimatedActor& actor, int angle) { return turnYawTo(actor.rotation.y, uint32_t(angle), actor.spin.y) == 0; }
void stop(AnimatedActor& actor) { // the brake (animation 6)
    actor.anim = 6;
    enterHerd(actor, Herd::Brake);
}
void neighbourAhead(AnimatedActor& actor, HerdFields& f) { // exe 0x5e5e00 found one -> make way
    if (neighbourFind(actor, f.neighbour, 1) == 0) return;
    actor.spin.y = kTurnFast;
    actor.velocity.z = 0;
    enterHerd(actor, Herd::MakeWay);
}
// the ground step's tails: blocked with `bounce` -> the bounce (or, with `facing`, the run again unless it faces the
// player), the camera shaken; the ground fell away -> the fall
void groundTail(AnimatedActor& actor, int r, bool bounce, bool facing) {
    if (r == 2 && bounce) {
        actor.velocity.z = 0x100;
        actor.anim = 8;
        actor.restartStep();
        actor.subState = uint8_t(!facing ? Herd::Bounced
                                         : (facingAndRange(actor, player(), 0x180, 0xa00) == 1 ? Herd::Bounced
                                                                                                : Herd::BouncedRunAgain));
        cameraShakeAt(0x6000, 0x500, 0, 0x600, actor.pos); // exe 0x40eab0
        return;
    }
    if (r == 1) {
        actor.velocity.y = 0;
        actor.acceleration.y = 0x60;
        actor.anim = 0xf;
        enterHerd(actor, Herd::Fall);
    }
}
void forward(AnimatedActor& actor) { integrateVelocity(actor, 0, 0, actor.velocity.z); } // exe 0x5e3690(a, 0, 0, vz)
// exe 0x5e1300 (the static pool, the global table) at (x, y - 0x100, z)
StaticActor* spawnStaticAbove(const AnimatedActor& actor, GlobalStaticType type) {
    const uint32_t s = allocActor60(0);
    if (!s) return nullptr;
    StaticActor& e = staticActorAt(s);
    e.flags = kActorLive;
    e.type = typeByte(type);
    e.variant = 0;
    e.loopFlags |= kActorSweepGroup2;
    e.pos.x.whole = actor.pos.x.whole;
    e.pos.y.whole = int16_t(uint16_t(uint16_t(actor.pos.y.whole) - 0x100u));
    e.pos.z.whole = actor.pos.z.whole;
    return &e;
}

// ---- the behaviours ---------------------------------------------------------------------------------------------------
void lookAround(const PlayerEnv& env, AnimatedActor& actor, HerdFields& f) { // 0x4a7e40 (0 / 0xc)
    const uint8_t dl = f.bits & uint8_t(~kHerdRunning);
    f.bits = dl;
    switch (actor.step) {
    case 0:
        f.bits = dl & uint8_t(~kHerdAtGates);
        if (penDir(actor) >= 0 || gateDir(actor) >= 0) f.bits |= kHerdAtGates;
        actor.anim = 4;
        ++actor.step;
        f.watchesPlayer = 1;
        break;
    case 1: ++actor.step; break;
    case 2:
        if (actor.animEnded()) {
            actor.anim = 5;
            ++actor.step;
        }
        break;
    case 3:
        if (!actor.animEnded()) break;
        if (dl & kHerdAtGates) {
            actor.subState = uint8_t(Herd::AtGates);
        } else {
            bool run;
            if (actor.subState == uint8_t(Herd::LookAroundAgain) &&
                uint32_t(yawDiff(actor, angleFromPlayer(actor)) + 0x400) > 0x800u)
                run = true;
            else
                run = uint32_t(planarDistance(actor.pos, player().pos)) > uint32_t(kChaseRange);
            if (run) {
                actor.spin.y = kTurnSlow;
                actor.velocity.z = kRunSpeed;
                actor.anim = 1;
                actor.subState = uint8_t(Herd::RunOff);
            } else {
                actor.spin.y = kTurnFast;
                actor.velocity.z = 0;
                actor.subState = uint8_t(Herd::Chase);
            }
        }
        actor.restartStep();
        break;
    default: break;
    }
    if (!(f.bits & kHerdAtGates)) {
        if (actor.subState != uint8_t(Herd::LookAroundAgain)) avoid(actor, f, 0x180);
        neighbourAhead(actor, f);
    }
    groundTail(actor, herdGround(env, actor, f, 2), false, false);
}
void runOff(const PlayerEnv& env, AnimatedActor& actor, HerdFields& f) { // 0x4a7fe0: a random turn, up to 0x3c ticks
    switch (actor.step) {
    case 0: {
        actor.anim = 9;
        uint32_t v = (rngStep() & 0x3ff) + 0x200;
        if (!(v & 1)) v = uint32_t(-int32_t(v));
        f.heading = int16_t((uint16_t(f.heading) + v) & 0xfff);
        ++actor.step;
        break;
    }
    case 1:
        avoid(actor, f, 0x180);
        if (turned(actor, f.heading)) {
            f.timer = 0;
            actor.velocity.z = kRunSpeed;
            actor.anim = 1;
            ++actor.step;
        }
        break;
    case 2:
        forward(actor);
        if (blocked(actor, f, 0x180, 0x700) != 0) {
            stop(actor);
        } else {
            ++f.timer;
            if (f.timer == 0x3c || (f.bits & kHerdHit)) stop(actor);
        }
        break;
    default: break;
    }
    const int r = herdGround(env, actor, f, 2);
    neighbourAhead(actor, f);
    groundTail(actor, r, true, false);
}
void chase(const PlayerEnv& env, AnimatedActor& actor, HerdFields& f) { // 0x4a8180: face the player, lunge, run past
    const uint8_t cl = actor.step;
    switch (cl) {
    case 0:
        actor.anim = 4;
        f.bits |= kHerdRunning;
        ++actor.step;
        break;
    case 1:
        if (actor.animEnded()) {
            const int ang = angleFromPlayer(actor);
            f.heading = int16_t(ang);
            const int32_t c = int32_t(uint32_t(uint16_t(uint16_t(ang) - uint16_t(actor.rotation.y)))) + 0x40;
            if (c < 0x80) {
                actor.velocity.z = kChargeSpeed;
                actor.anim = 2;
                soundPlayAtPos(kLungeSound, actor.pos);
                actor.step += 2u;
            } else {
                turnYawTo(actor.rotation.y, uint32_t(ang), actor.spin.y);
                actor.anim = 0xa;
                ++actor.step;
            }
        }
        break;
    case 2: {
        const int ang = angleFromPlayer(actor);
        f.heading = int16_t(ang);
        if (turned(actor, ang)) {
            actor.velocity.z = kChargeSpeed;
            actor.anim = 2;
            soundPlayAtPos(kLungeSound, actor.pos);
            ++actor.step;
        }
        break;
    }
    case 3:
        if (actor.animEnded()) {
            actor.anim = 3;
            actor.step = uint8_t(cl + 1u);
        }
        break;
    case 4:
        forward(actor);
        if (blocked(actor, f, 0x180, 0x700) != 0) {
            stop(actor);
        } else if (facingAndRange(actor, player(), 0x180, 0xa00) == 1) {
            f.timer = 0;
            ++actor.step;
        }
        break;
    case 5:
        forward(actor);
        if (blocked(actor, f, 0x180, 0x700) != 0) {
            stop(actor);
        } else {
            ++f.timer;
            if (f.timer == 0x1e) stop(actor);
        }
        break;
    default: break;
    }
    const int r = herdGround(env, actor, f, 2);
    neighbourAhead(actor, f);
    groundTail(actor, r, true, true);
}
// 0x4a8440 (3) / 0x4a8d60 (8): the bounce (its sound), stand 0x1e ticks, then resume (3) / run again (8)
void bounced(const PlayerEnv& env, AnimatedActor& actor, HerdFields& f, bool runAgain) {
    forward(actor);
    const int r = herdGround(env, actor, f, 2);
    switch (actor.step) {
    case 0:
        f.bits &= uint8_t(~(kHerdHit | kHerdRunning));
        ++actor.step;
        soundPlayAtPos(kBounceSound, actor.pos);
        break;
    case 1:
        if (actor.animEnded()) {
            f.timer = 0;
            ++actor.step;
            actor.velocity.z = 0;
            actor.anim = 0;
        }
        break;
    case 2:
        ++f.timer;
        if (f.timerSigned >= 0x1e) {
            if (!runAgain) {
                resume(actor);
            } else {
                actor.spin.y = kTurnFast;
                actor.velocity.z = kChargeSpeed;
                enterHerd(actor, Herd::TurnAwayRun);
            }
        }
        break;
    default: break;
    }
    groundTail(actor, r, false, false);
}
// 0x4a8500: falling; landed low (y >= -0x80: in a pen) -> fell in a pen (its sound), else run off / chase
void fall(const PlayerEnv& env, AnimatedActor& actor, HerdFields& f) {
    actor.velocity.y += uint16_t(actor.acceleration.y); // exe 0x4a4bb0: the gravity, at most 0x2000
    if (actor.velocity.y > 0x2000) actor.velocity.y = 0x2000;
    integrateVelocity(actor, 0, actor.velocity.y, actor.velocity.z);
    herdGround(env, actor, f, 2);
    if (actor.airborne != 0) return;
    if (actor.pos.y.whole >= -0x80) {
        actor.velocity.y = 0;
        actor.acceleration.y = 0;
        enterHerd(actor, Herd::FellInPen);
        soundPlayAtPos(kPenSound, actor.pos);
        return;
    }
    if (f.bits & kHerdRunning) {
        actor.spin.y = kTurnFast;
        actor.velocity.z = 0;
        enterHerd(actor, Herd::Chase);
        return;
    }
    actor.spin.y = kTurnSlow;
    actor.velocity.z = kRunSpeed;
    actor.anim = 1;
    enterHerd(actor, Herd::RunOff);
}
void turnBack(AnimatedActor& actor, HerdFields& f) { // 0x4a8a11
    actor.step = 4;
    actor.anim = 0;
    f.timer = 0x32;
}
// 0x4a8900: the brake (its sound, the deceleration 0x18), then a pause / steer off the posts / turn (look around again)
void brake(const PlayerEnv& env, AnimatedActor& actor, HerdFields& f) {
    const uint8_t al = actor.step;
    switch (al) {
    case 0:
        actor.step = uint8_t(al + 1u);
        actor.acceleration.z = 0x18;
        f.bits &= uint8_t(~(kHerdHit | kHerdRunning));
        soundPlayAtPos(kBrakeSound, actor.pos);
        [[fallthrough]];
    case 1: {
        int v = actor.acceleration.z + actor.velocity.z;
        if (v >= 0) {
            v = 0;
            ++actor.step;
            actor.anim = 7;
        }
        actor.velocity.z = int16_t(v);
        integrateVelocity(actor, 0, 0, v);
        break;
    }
    case 2:
        if (!actor.animEnded()) break;
        if (blocked(actor, f, 0x180, 0x300) == 0) {
            const uint8_t b = f.bits;
            if (b & kHerdRetreatEnded) {
                f.bits = uint8_t(b & ~kHerdRetreatEnded);
                resume(actor);
            } else {
                f.timer = 0;
                ++actor.step;
                actor.anim = 0;
            }
            avoid(actor, f, 0x180);
        } else {
            turnBack(actor, f);
        }
        break;
    case 3:
        if (f.bits & kHerdHit) {
            if (avoid(actor, f, 0x180) == 0) turnBack(actor, f);
        } else {
            ++f.timer;
            if (f.timerSigned >= 0x1e) resume(actor);
        }
        break;
    case 4: actor.step = uint8_t(al + 1u); [[fallthrough]];
    case 5:
        if (snapped(actor, f.heading)) {
            actor.velocity.z = kRunSpeed;
            actor.anim = 0;
            enterHerd(actor, Herd::LookAroundAgain);
        }
        break;
    default: break;
    }
    groundTail(actor, herdGround(env, actor, f, 2), true, false);
}
// 0x4a8c20: the pen was taken -- it blinks, an explosion, then a blast and its drops, freed
void penTaken(AnimatedActor& actor, HerdFields& f) {
    if (gameState().frame & 1) actor.tint = kTintFlash;
    switch (actor.step) {
    case 0:
        if (StaticActor* e = spawnStaticAbove(actor, GlobalStaticType::Explosion)) {
            ExplosionParams params(actorHeaderAt(e->at));
            params.spread = 0x80;
            params.flashes = 0x3f;
        }
        ++actor.step;
        break;
    case 1:
        if (actor.animEnded()) {
            f.timer = 0;
            ++actor.step;
            actor.anim = 0xd;
            f.bits |= kHerdPenned;
        }
        break;
    case 2:
        ++f.timer;
        if (f.timer == 0x1e) {
            if (StaticActor* e = spawnStaticAbove(actor, GlobalStaticType::Blast)) {
                BlastParams params(actorHeaderAt(e->at));
                params.size = 0xc0;
                params.attack = 0;
            }
            actor.enterState(uint8_t(HerdState::Free));
            if (!hasAnimatedStats(actor.type)) notPorted(kStoryPort, "0x4a8c20: no type row (the exe reads address 5)");
            spawnDrops(actor.pos, animatedStats(actor.type, actor.subtype).dropTable); // exe 0x5f1270
        }
        break;
    default: break;
    }
}
// 0x4a8e30: turn away (0x380..0x47f either side) and run 0x14 ticks, then the brake
void turnAwayRun(const PlayerEnv& env, AnimatedActor& actor, HerdFields& f) {
    switch (actor.step) {
    case 0:
        actor.anim = 4;
        ++actor.step;
        break;
    case 1:
        if (actor.animEnded()) {
            actor.anim = 0xa;
            const uint32_t r = rngStep();
            uint32_t v = (r & 0xff) + 0x380;
            if (!(r & 0x100)) v = uint32_t(-int32_t(v));
            f.heading = int16_t((uint16_t(actor.rotation.y) + v) & 0xfff);
            ++actor.step;
        }
        break;
    case 2:
        if (turned(actor, f.heading)) {
            f.timer = 0;
            ++actor.step;
            actor.anim = 3;
            actor.velocity.z = kChargeSpeed;
        }
        break;
    case 3:
        forward(actor);
        ++f.timer;
        if (f.timer == 0x14) {
            f.bits |= kHerdRetreatEnded;
            stop(actor);
        }
        break;
    default: break;
    }
    groundTail(actor, herdGround(env, actor, f, 2), true, true);
}
// 0x4a8fe0: make way for a neighbour (exe 0x5e5f70 classifies it, the heading by its bits)
void makeWay(const PlayerEnv& env, AnimatedActor& actor, HerdFields& f) {
    const Actor& other = neighbourActor(f.neighbour); // exe 0x5e5cb0
    const uint16_t word = f.neighbour;
    bool stopped = false;
    switch (actor.step) {
    case 0:
        f.timer = 0x78;
        neighbourClassify(actor, f.neighbour);
        if (f.bits & kHerdRunning) {
            actor.anim = 0xa;
            actor.spin.y = kTurnFast;
        } else {
            actor.anim = 9;
            actor.spin.y = kTurnSlow;
        }
        if (word & kNeighbourOpposite) f.heading = int16_t(headingFromOther(actor, other, neighbourSide(f.neighbour), 0x200));
        else f.heading = actor.rotation.y;
        ++actor.step;
        break;
    case 1:
        if (snapped(actor, f.heading)) {
            if (f.bits & kHerdRunning) {
                actor.velocity.z = kChargeSpeed;
                actor.anim = 3;
            } else {
                actor.velocity.z = kRunSpeed;
                actor.anim = 1;
            }
            ++actor.step;
        }
        break;
    case 2:
        forward(actor);
        if (blocked(actor, f, 0x180, 0x700) != 0) {
            stop(actor);
            stopped = true;
        }
        break;
    default: break;
    }
    if (!stopped) {
        const bool apart = other.flags == 0 ||
                           uint32_t(other.pos.x.whole - actor.pos.x.whole + 0x400) > 0x800u ||
                           uint32_t(other.pos.z.whole - actor.pos.z.whole + 0x400) > 0x800u;
        --f.timer;
        if (f.timer == 0 || apart) {
            if (word & kNeighbourCrossingSlow) stop(actor);
            else resume(actor);
        }
    }
    groundTail(actor, herdGround(env, actor, f, 2), true, true);
}
// 0x4a8590 (fell into a pen: the quadrant's flag 0x275 (x <= 0 or z == 0) / 0x276 (z > 0) / 0x277 (z < 0); taken ->
// the pen taken) and 0x4a86b0 (placed penned): penned (animation 0x10), then a solid to stand on (exe 0x414050(a, 1,
// 0, 0))
void penned(AnimatedActor& actor, HerdFields& f, bool placed) {
    const uint8_t cl = actor.step;
    switch (cl) {
    case 0:
        if (!placed) {
            const int x = actor.pos.x.whole, z = actor.pos.z.whole;
            const uint32_t fl = x > 0 ? (z < 0 ? kFlagPenC : (z > 0 ? kFlagPenB : kFlagPenA)) : kFlagPenA;
            if (storyFlag(fl)) {
                enterHerd(actor, Herd::PenTaken);
                actor.anim = 0xc;
                actor.velocity.z = 0;
                f.timer = 0;
                ++f.pensFoundTaken;
                return;
            }
            setStoryFlag(fl);
        }
        ++actor.step;
        f.bits = uint8_t((f.bits & uint8_t(~(kHerdHit | kHerdRunning))) | kHerdPenned);
        actor.anim = 0x10;
        break;
    case 1:
        if (actor.animEnded()) actor.step = uint8_t(cl + 1u);
        break;
    case 2: actor.tint = kTintPenned; break;
    default: break;
    }
    actorCollisionSubmitSolid(actor, SolidType::StandOn, 0, 0); // exe 0x414050(a, 1, 0, 0)
}
// 0x4a8b20: knocked back (animation 0xe, braking by 0x64), 3 ticks, resume
void knockedBack(const PlayerEnv& env, AnimatedActor& actor, HerdFields& f) {
    switch (actor.step) {
    case 0:
        ++actor.step;
        actor.anim = 0xe;
        actor.velocity.z = int16_t(0xfce0);
        actor.acceleration.z = 0x64;
        break;
    case 1:
        forward(actor);
        actor.velocity.z += uint16_t(actor.acceleration.z);
        if (actor.velocity.z >= 0) {
            actor.velocity.z = 0;
            ++actor.step;
            actor.anim = 0;
            f.timer = 3;
        }
        break;
    case 2:
        --f.timer;
        if (f.timer == 0) {
            f.frontHitArmed = 0xf;
            resume(actor);
        }
        break;
    default: break;
    }
    groundTail(actor, herdGround(env, actor, f, 2), true, false);
}
// 0x4a8730: at the gates -- turn into the pen (exe 0x4a9670), walk, then away from the corner
void atGates(const PlayerEnv& env, AnimatedActor& actor, HerdFields& f) {
    switch (actor.step) {
    case 0: {
        const int r = penDir(actor);
        if (r < 0) {
            actor.step = 3;
        } else {
            actor.anim = 9;
            actor.spin.y = kTurnSlow;
            f.heading = int16_t(r);
            ++actor.step;
        }
        break;
    }
    case 1:
    case 4:
        if (snapped(actor, f.heading)) {
            const bool second = actor.step == 4;
            actor.velocity.z = kRunSpeed;
            actor.anim = 1;
            ++actor.step;
            if (second) f.timer = uint16_t((crtRand() & 0x1f) + 0x1e);
        }
        break;
    case 2:
        forward(actor);
        if (penDir(actor) < 0) ++actor.step;
        break;
    case 3: {
        const int r = gateDir(actor);
        if (r < 0) {
            stop(actor);
        } else {
            ++actor.step;
            f.heading = int16_t(r);
            actor.anim = 9;
            actor.spin.y = kTurnSlow;
        }
        break;
    }
    case 5:
        forward(actor);
        --f.timer;
        if (f.timer == 0) stop(actor);
        break;
    default: break;
    }
    groundTail(actor, herdGround(env, actor, f, 2), true, false);
}
void behaviour(const PlayerEnv& env, AnimatedActor& actor, HerdFields& f, uint32_t n) { // [0x893d84 + n * 4]
    switch (Herd(n)) {
    case Herd::LookAround:
    case Herd::LookAroundAgain: lookAround(env, actor, f); return; // 0x4a7e40
    case Herd::RunOff: runOff(env, actor, f); return;              // 0x4a7fe0
    case Herd::Chase: chase(env, actor, f); return;                // 0x4a8180
    case Herd::Bounced: bounced(env, actor, f, false); return;     // 0x4a8440
    case Herd::Fall: fall(env, actor, f); return;                  // 0x4a8500
    case Herd::Brake: brake(env, actor, f); return;                // 0x4a8900
    case Herd::GetUp:                                              // 0x4a8b00
        if (actor.animEnded()) resume(actor);
        return;
    case Herd::PenTaken: penTaken(actor, f); return;                   // 0x4a8c20
    case Herd::BouncedRunAgain: bounced(env, actor, f, true); return;  // 0x4a8d60
    case Herd::TurnAwayRun: turnAwayRun(env, actor, f); return;        // 0x4a8e30
    case Herd::MakeWay: makeWay(env, actor, f); return;                // 0x4a8fe0
    case Herd::FellInPen: penned(actor, f, false); return;             // 0x4a8590
    case Herd::KnockedBack: knockedBack(env, actor, f); return;        // 0x4a8b20
    case Herd::PlacedPenned: penned(actor, f, true); return;           // 0x4a86b0
    case Herd::AtGates: atGates(env, actor, f); return;                // 0x4a8730
    default: notPortedAt(kStoryPort, "0x4a7b70 behaviour (table 0x893d84)", n);
    }
}
// 0x4a7be0: model variant 2, hit points (the row's) * 4; variant 1 placed penned already (its pen's place)
void herdInit(const PlayerEnv& env, AnimatedActor& actor) {
    iceRuinHerdedCreatureGlobals().initModelVariant = kHerdModelVariant;
    if (!bindActorModel(actor, iceRuinHerdedCreatureGlobals().initModelVariant)) { // exe 0x5e0bc0
        freeActor(actor);                         // exe 0x5e1490
        return;
    }
    actor.anim = 0;
    actor.prevAnim = 0xff;
    const int16_t hp = hasAnimatedStats(actor.type)
                           ? int16_t(uint16_t(uint32_t(animatedStats(actor.type, actor.subtype).hitPoints) << 2))
                           : int16_t(0);
    actor.maxHitPoints = hp;
    actor.hitPoints = hp;
    actor.rotation.z = 0;
    actor.rotation.x = 0;
    actor.velocity.setZSpare(0, 0);
    actor.velocity.setXY(0, 0);
    actor.spin.x = 0;
    actor.spin.y = kTurnFast;
    actor.collisionBox.pointAt(kHerdBox.stateAddress());
    actor.actorHit = 0;
    actor.platform = nullptr;
    actor.clearClassBlock(); // exe 0x40dc40
    switch (actor.variant) {
    case 0:
        actor.enterState(uint8_t(HerdState::Main));
        actor.anim = 0;
        break;
    case 1: { // the init's global index walks the pen's place {x, z, yaw}
        const uint32_t i = uint32_t(int32_t(HerdParams(actor).penIndex.get())) * 3;
        actor.enterState(uint8_t(HerdState::Main), uint8_t(Herd::PlacedPenned));
        actor.anim = 0xc;
        iceRuinHerdedCreatureGlobals().initPlaceIndex = i;
        actor.pos.x.whole = kPennedPlaces[i];
        iceRuinHerdedCreatureGlobals().initPlaceIndex = i + 1;
        actor.pos.z.whole = kPennedPlaces[i + 1];
        iceRuinHerdedCreatureGlobals().initPlaceIndex = i + 2;
        actor.rotation.y = kPennedPlaces[i + 2];
        break;
    }
    default: break;
    }
    actor.pos.y = int32_t(uint32_t(actorGround(env, actor)) << 16); // exe 0x40f190
    registerEnemy(actor.slot, 1);                                     // exe 0x5e5c10
    actor.flags |= kActorDrawn;
}
void herdMain(const PlayerEnv& env, AnimatedActor& actor, HerdFields& f) { // 0x4a7d20
    if (!(f.bits & kHerdPenned)) {
        const int r = herdHit(actor, f); // exe 0x4a9390 (never -1: the exe's "-1" branch is dead)
        if (r == 2 && !(f.bits & kHerdAirborne)) {
            enterHerd(actor, Herd::KnockedBack);
        } else if ((f.bits & (kHerdAirborne | kHerdDying)) == kHerdDying) { // dead too: exe 0x4a9390 clears the bit
            actor.subState = uint8_t(Herd::GetUp);
            actor.velocity.z = 0;
            actor.anim = 0xb;
        }
    }
    behaviour(env, actor, f, actor.subState);
    if (!(f.bits & kHerdPenned)) herdHitEntry(actor, f); // exe 0x4a9300(a, +0x162)
    if (actor.anim == actor.prevAnim) animStepPose(actor); // exe 0x5e1e80
    else animSetPose(actor, actor.anim, 0);               // exe 0x5e1c30
    const uint8_t an = actor.anim, fr = actor.keyIndex;
    if ((an == 3 || an == 1) && (fr == 3 || fr == 9)) soundPlayAtPos(kFootstepSound, actor.pos); // the footsteps
}
void herdedCreature(const PlayerEnv& env, AnimatedActor& actor) { // 0x4a7b70
    HerdFields& f = classState<HerdFields>(actor);
    actor.prevAnim = actor.anim;
    actor.savePreviousPose();
    actor.tint = kTintNeutral;
    actor.savePreviousContact();
    const uint8_t st = actor.state;
    switch (HerdState(st)) {
    case HerdState::Init: herdInit(env, actor); break;
    case HerdState::Main: herdMain(env, actor, f); break;
    case HerdState::Free: freeActor(actor); break; // 0x468fa0 -> exe 0x5e1490
    default:
        if (st > uint8_t(HerdState::LastDirect)) notPortedAt(kStoryPort, "0x4a7b70 state (table 0x893d78)", st);
        behaviour(env, actor, f, st - uint32_t(HerdState::FirstDirect));
        break;
    }
    const int8_t c = f.cooldown;
    if (c < 0) f.cooldown = int8_t(c + 1);
}

} // namespace

void herdedCreatureUpdate(const PlayerEnv& env, uint32_t actor) { herdedCreature(env, animatedActorAt(actor)); }

} // namespace od::port
