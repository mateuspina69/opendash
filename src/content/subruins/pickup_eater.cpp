// The pickup eater (pool 0x20 class 0x4ab6f0, see pickup_eater.hpp; hypothesis names from the observed behaviour).
// States [0x894024]: 0 init 0x4ab850, 1 main 0x4ab920 (sub-states [0x894038]: 0 0x4aba00 loops, 1 0x4aba80 shies, 2
// 0x4abde0 runs to a pickup, 3 0x4ac070 off a wall, 4 0x4ac140 hop, 5 0x4ac230 fall, 6..8 0x4ac290 knocked), 2
// 0x4ac550 carried (sub-states [0x8940bc]: 0x4ac570 at the hands, 0x4ac650 thrown), 3 0x4ac420 dying, 4 0x468fa0 freed.
// Render output skipped: the shadow exe 0x5f20f0(a, 0x30, 0x160, 0) (not drawn while u16 +8 == 2).
#include "content/subruins/pickup_eater.hpp"
#include "content/subruins/pickup_eater_state.hpp"

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
#include "game/carry.hpp"
#include "game/distances.hpp"
#include "game/enemy_motion.hpp"
#include "game/explosions.hpp"
#include "game/fixed_point.hpp"
#include "game/global_class_types.hpp"
#include "game/hit_list.hpp"
#include "game/hit_points.hpp"
#include "game/pickups.hpp"
#include "game/player/player_motion.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/story.hpp"
#include "game/territory.hpp"

namespace od::port {

using namespace pickup_eater_state;

namespace {

// +0x160
constexpr uint8_t kEaterNoHitEntry = 0x01, kEaterNoHitReact = 0x02, kEaterNoWallTurn = 0x04, kEaterNoFall = 0x08;
constexpr uint8_t kEaterThrownEntry = 0x10, kEaterBounces = 0x40, kEaterNotTarget = 0x80;
// +0x161
constexpr uint8_t kEaterWallX = 0x01, kEaterWallZ = 0x02, kEaterLanded = 0x04, kEaterGroundLost = 0x08;
constexpr uint8_t kEaterWalls = kEaterWallX | kEaterWallZ;

enum class EaterState : uint8_t { Init = 0, Main = 1, Carried = 2, Dying = 3, Freed = 4 };
enum class EaterSub : uint8_t {
    Loops = 0,      // exe 0x4aba00
    Shies = 1,      // exe 0x4aba80
    ToPickup = 2,   // exe 0x4abde0
    OffWall = 3,    // exe 0x4ac070
    Hop = 4,        // exe 0x4ac140
    Fall = 5,       // exe 0x4ac230
    KnockBack = 6,  // exe 0x4ac290 (the three knock-backs share it)
    KnockAir = 7,
    KnockAside = 8,
};
enum class EaterCarriedSub : uint8_t { InHands = 0, Thrown = 1 }; // exe 0x4ac570 / 0x4ac650

// a probe point of its steering (s16 x / z offsets)
struct ProbePoint : DataRecord {
    static constexpr uint32_t kBytes = 4;
    int16_t x = 0;
    int16_t z = 0;
    template <class R> static ProbePoint load(const R& r) { return {{}, r.s16(0), r.s16(2)}; }
};
const DataTable<ScalarTable<int16_t, 0x10>> blockedTurns{TableId::PickupEaterBlockedTurns}; // by the blocked probes' bits (< 0: no turn)
const DataTable<CollisionBoxRecord> eaterBox{TableId::PickupEaterBox};
const DataTable<InteractionBoxRecord> eaterLiftBox{TableId::PickupEaterLiftBox};
constexpr int16_t kEaterScale = 0x280;
constexpr uint8_t kEatDistance = 0x3c;
constexpr uint16_t kSoundSpotsPickup = 0x24a;
constexpr uint16_t kTintDyingBlink = 0x4214;

AnimatedActorStats statsOf(const AnimatedActor& a) { return animatedStats(a.type); }
void toSub(AnimatedActor& a, EaterSub s) {
    a.subState = uint8_t(s);
    a.enterStep(0);
}

// a pickup it can eat: in use, the pickup class (global static type 0)
bool edible(const StaticActor& e) {
    return e.flags != 0 && e.type == typeByte(GlobalStaticType::Pickup) && !(e.loopFlags & kActorStageClass);
}

// exe 0x4abb30: the resting pickup with the largest size whose height is within [y - 0x20, y] of its; 1-based static
// index (scanned from the last slot down), 0 = none
int findPickup(const AnimatedActor& a) {
    int best = -1, idx = 0;
    for (size_t i = StaticPool::size(); i != 0; --i) {
        const StaticActor& e = staticPool()[i - 1];
        if (!edible(e) || e.airborne != 0) continue;
        const int pr = classState<PickupFields>(e).size;
        if (best >= pr) continue;
        const int16_t ey = e.pos.y.whole, ay = a.pos.y.whole;
        if (ey > ay || int(ey) < int(ay) - 0x20) continue;
        best = pr;
        idx = int(i);
    }
    return idx;
}

// exe 0x4abba0(actor, angle): the loop's centre pushed along the angle by (0x400 - the player's planar distance) * 16
// (clamped to +-0x140); when one of its four probe points (by its variant, turned by its yaw) is blocked, by -0xa0
// along its yaw + the blocked bits' turn instead (when that turn is >= 0)
void steer(const PlayerEnv& env, AnimatedActor& a, EaterFields& f, int ang) {
    const uint32_t probes = stateAddress(TableId::PickupEaterProbePoints) + uint32_t(a.variant) * 8 * 2;
    uint32_t blocked = 0;
    int push = (0x400 - planarDistance(a.pos, player().pos)) << 4; // exe 0x5e3b00
    if (push > 0x140) push = 0x140;
    else if (push < -0x140) push = -0x140;
    const uint32_t mask = collisionLayerMask(a.slot); // exe 0x413240
    for (uint32_t k = 0; k < 4; ++k) {
        int32_t d[3];
        const ProbePoint probe = dataRecord<ProbePoint>(probes + uint32_t(k) * ProbePoint::kBytes, ProbePoint::kBytes);
        rotateByYaw(a.rotation.y, probe.x, 0, probe.z, d); // exe 0x5e36f0 on a copy of the position
        const int32_t q[3] = {int32_t(uint32_t(a.pos.x) + uint32_t(d[0])), int32_t(uint32_t(a.pos.y) + uint32_t(d[1])),
                              int32_t(uint32_t(a.pos.z) + uint32_t(d[2]))};
        const int16_t pa[3] = {wholePart(q[0]), wholePart(q[1]), wholePart(q[2])};
        const int16_t pb[3] = {a.pos.x.whole, a.pos.y.whole, a.pos.z.whole};
        if (int32_t(testMoveBlocked(env, pa, pb, a.collisionBox, mask)) < 0) blocked |= 1u << k; // exe 0x40f5a0
    }
    int yaw = ang;
    if (blocked != 0) {
        const int16_t t = blockedTurns[blocked];
        if (t >= 0) {
            yaw = a.rotation.y + t;
            push = -0xa0;
        }
    }
    int32_t o[3];
    rotateByYaw(yaw, 0, 0, push, o); // exe 0x5e38d0
    f.centreX = int16_t(uint16_t(uint16_t(f.centreX) + uint16_t(uint32_t(o[0]) >> 16)));
    f.centreZ = int16_t(uint16_t(uint16_t(f.centreZ) + uint16_t(uint32_t(o[2]) >> 16)));
}

// exe 0x4ac910: the loop walk -- x / z = the centre moved along two yaws (exe 0x5e3750 on an s16 point of the stack;
// its y is never read back). Variant 0: a figure eight of radius 0xfa0 (0x40 ticks a half, the angle sweeping from 0x400
// up / from 0xc00 down); else a circle of radius 0x7d0 (the angle + 0x80 a tick)
void loopWalk(AnimatedActor& a, EaterFields& f) {
    int16_t p[3] = {0, 0, 0};
    if (a.variant == 0) {
        switch (f.loopPhase) {
        case 0:
            a.anim = 0;
            f.loopAngle = 0xc00;
            f.loopYaw = int16_t(uint16_t(uint16_t(a.rotation.y) + 0x400u));
            f.centreX = a.pos.x.whole;
            f.centreZ = a.pos.z.whole;
            f.timer = 0x40;
            ++f.loopPhase;
            [[fallthrough]];
        case 1:
            p[0] = f.centreX;
            p[2] = f.centreZ;
            moveAlongYaw16(p, int(uint16_t(f.loopYaw)), 0, 0, -0xfa0);
            moveAlongYaw16(p, int(uint16_t(f.loopYaw)) + int(uint16_t(f.loopAngle)) - 0x400, 0, 0, -0xfa0);
            f.loopAngle += 0x40u;
            --f.timer;
            if (f.timer == 0) {
                f.timer = 0x40;
                f.loopYaw += 0x800u;
                ++f.loopPhase;
                f.loopAngle = 0x400;
            }
            break;
        case 2:
            p[0] = f.centreX;
            p[2] = f.centreZ;
            moveAlongYaw16(p, int(uint16_t(f.loopYaw)), 0, 0, -0xfa0);
            moveAlongYaw16(p, int(uint16_t(f.loopYaw)) + int(uint16_t(f.loopAngle)) - 0xc00, 0, 0, -0xfa0);
            f.loopAngle += 0xffc0u;
            --f.timer;
            if (f.timer == 0) {
                f.timer = 0x40;
                f.loopYaw += 0x800u;
                --f.loopPhase;
                f.loopAngle = 0xc00;
            }
            break;
        default:
            notPortedAt(kStoryPort, "exe 0x4ac910 phase (the exe writes uninitialised stack to x / z)", f.loopPhase);
        }
    } else {
        switch (f.loopPhase) {
        case 0:
            p[0] = a.pos.x.whole;
            p[2] = a.pos.z.whole;
            moveAlongYaw16(p, a.rotation.y, 0, 0, -0x7d0);
            f.centreX = p[0];
            f.centreZ = p[2];
            f.loopAngle = 0x800;
            f.loopYaw = a.rotation.y;
            a.anim = 0;
            ++f.loopPhase;
            [[fallthrough]];
        case 1:
            p[0] = f.centreX;
            p[2] = f.centreZ;
            moveAlongYaw16(p, int(uint16_t(f.loopYaw)) + int(uint16_t(f.loopAngle)), 0, 0, -0x7d0);
            f.loopAngle += 0x80u;
            break;
        default:
            notPortedAt(kStoryPort, "exe 0x4ac910 phase (the exe writes uninitialised stack to x / z)", f.loopPhase);
        }
    }
    a.pos.x.whole = p[0];
    a.pos.z.whole = p[2];
}

// exe 0x4ac3b0(actor, yaw): the flight step -- a bounce (landed, or falling onto an actor) halves / reverses the
// velocity, the pitch 0, airborne: 1; else the move along the yaw and the gravity: 0
int flyStep(AnimatedActor& a, EaterFields& f, int yaw) {
    if (!(f.contact & kEaterLanded) && !(a.velocity.y > 0 && a.onPlatform != 0)) {
        moveAlongYaw(a.pos, yaw, 0, a.velocity.y, a.velocity.z); // exe 0x5e36f0
        a.velocity.y = int16_t(uint16_t(a.velocity.y) + uint16_t(a.acceleration.y));
        return 0;
    }
    a.velocity.z = int16_t(a.velocity.z >> 1);
    a.velocity.y = int16_t(uint32_t(-int(a.velocity.y)) >> 1);
    a.rotation.x = 0;
    a.airborne = 1;
    return 1;
}

// exe 0x4ac7d0: the walls (the push-out, or the territory), the ground -> the contact bits; 1 = the ground was lost
// (> 0x10 below, standing)
int collide(const PlayerEnv& env, AnimatedActor& a, EaterFields& f) {
    uint32_t r = 0;
    f.contact = 0;
    if (int32_t(actorTestMove(env, a)) < 0) { // exe 0x40f570
        r = actorPushOut(env, a);              // exe 0x410960
        a.wallHit = r;
        r |= a.actorHit;
        if (int32_t(r) < 0) a.pos = a.prevPos;
        if (r & 0x300) f.contact |= kEaterWallX;
        if (r & 0xc00) f.contact |= kEaterWallZ;
        if ((f.bits & kEaterBounces) && reflectRotationOffWalls(a.rotation, r) != 0) // exe 0x410740
            f.contact |= kEaterLanded;
    } else {
        const int z = territoryViolated(a.pos); // exe 0x5e6ab0
        r = uint32_t(z);
        if (z != 0) {
            r = territoryPushBack(a, z); // exe 0x5e6b40
            if (r & 0x300) f.contact |= kEaterWallX;
            if (r & 0xc00) f.contact |= kEaterWallZ;
        }
    }
    const int g = actorGround(env, a); // exe 0x40f190
    if (a.airborne == 0 && a.onPlatform == 0) {
        if (g - a.pos.y.whole > 0x10) {
            f.contact |= kEaterGroundLost;
            return 1;
        }
        a.pos.y.whole = int16_t(g);
        return 0;
    }
    if (!(f.bits & kEaterBounces) && (r & 0x3000)) {
        a.airborne = 0;
        a.pos.y.whole = int16_t(g);
        f.contact |= kEaterLanded;
    }
    return 0;
}

// exe 0x4ac710: a hit (the damage of its hit box) -- thrown / pushing attacks knock it back (in the air: up), the
// special weapons' levels back / aside (the hit's direction kept); the damage taken (exe 0x5e52f0); killed -> the
// knock-back, -1
int hitReact(AnimatedActor& a, EaterFields& f) {
    const uint32_t r = f.hitBox.result;
    const uint32_t dmg = r & kHitDamageMask;
    if (dmg == 0) return 0;
    uint32_t n = 0;
    if (r & (kHitAttackStrongPush | kHitAttackThrown)) {
        f.kept = f.hitBox.contact.x;
        a.tint = kTintFlash;
        toSub(a, a.airborne != 0 ? EaterSub::KnockAir : EaterSub::KnockBack);
        n = dmg;
    }
    if (r & (kHitLevelBelow | kHitLevelWithin | kHitLevelAbove)) {
        f.kept = f.hitBox.contact.x;
        if (r & kHitLevelBelow) {
            toSub(a, EaterSub::KnockAir);
            a.tint = kTintFlash;
        } else if (r & kHitLevelWithin) {
            toSub(a, EaterSub::KnockAside);
            a.tint = kTintFlash;
        }
        n = dmg;
    }
    if (hitPointsAdd(a, -int(n)) != 0) { // exe 0x5e52f0
        toSub(a, EaterSub::KnockBack);
        return -1;
    }
    return 0;
}

// ---- the main state's sub-states ------------------------------------------------------------------------------------
// exe 0x4aba00: the loops; a pickup, or the player near (planar < 0x400, its height within +-0x20) -> shies
void loops(AnimatedActor& a, EaterFields& f) {
    if (a.step == 0) {
        f.bits = 0;
        a.enterStep(1);
        a.tint = kTintNeutral;
        f.loopPhase = 0;
    }
    if (findPickup(a) != 0) toSub(a, EaterSub::Shies); // exe 0x4abb30
    const Player& p = player();
    if (planarDistance(a.pos, p.pos) < 0x400 && // exe 0x5e3b00
        uint32_t(int(a.pos.y.whole) - int(p.pos.y.whole) + 0x20) < 0x40u)
        toSub(a, EaterSub::Shies);
    loopWalk(a, f); // exe 0x4ac910
}
// exe 0x4aba80: turns away from the player, its loop pushed away; a pickup -> runs to it
void shies(const PlayerEnv& env, AnimatedActor& a, EaterFields& f) {
    if (a.step == 0) {
        f.bits = 0;
        a.enterStep(1);
        a.tint = kTintNeutral;
    }
    const uint16_t yaw0 = uint16_t(a.rotation.y);
    const int ang = angleOf(player().pos, a.pos); // exe 0x5e3bc0
    turnYawTo(a.rotation.y, uint32_t(ang), 0x10); // exe 0x5e3df0
    f.loopYaw = int16_t(uint16_t(uint16_t(f.loopYaw) + uint16_t(uint16_t(a.rotation.y) - yaw0)));
    steer(env, a, f, ang); // exe 0x4abba0
    loopWalk(a, f);        // exe 0x4ac910
    const int found = findPickup(a); // exe 0x4abb30
    if (found == 0) return;
    f.target = uint8_t(found - 1);
    toSub(a, EaterSub::ToPickup);
    a.velocity.x = int16_t(uint32_t(uint16_t(uint16_t(a.pos.x.whole) - uint16_t(f.hitBox.previousX))) << 4);
    a.velocity.z = int16_t(uint32_t(uint16_t(uint16_t(a.pos.z.whole) - uint16_t(f.hitBox.previousZ))) << 4);
}
// the position moved by the planar velocity (<< 12)
void slide(AnimatedActor& a) {
    a.pos.x += uint32_t(int32_t(a.velocity.x)) << 12;
    a.pos.z += uint32_t(int32_t(a.velocity.z)) << 12;
}
// exe 0x4abde0: brakes, turns to the pickup, runs at it; eaten / gone -> on, another one or a stop back into the loops
void toPickup(AnimatedActor& a, EaterFields& f) {
    const StaticActor& e = staticPool()[f.target];
    switch (a.step) { // table 0x4ac054
    case 0:           // exe 0x4abe18
        f.bits = 0;
        ++a.step;
        a.tint = kTintNeutral;
        a.anim = 2;
        a.acceleration.x = int16_t(a.velocity.x >> 3);
        a.acceleration.z = int16_t(a.velocity.z >> 3);
        soundPlayAtPos(kSoundSpotsPickup, a.pos); // exe 0x5d5380
        [[fallthrough]];
    case 1: // exe 0x4abe5f: braked to a stop, then turned to the pickup
        if (decelerateToward(a, 0)) { // exe 0x5e54a0
            a.acceleration.x = 0;
            a.acceleration.z = -0x50;
            ++a.step;
        }
        slide(a);
        f.timer = 0x20;
        turnYawTo(a.rotation.y, uint32_t(angleOf(e.pos, a.pos)), 0x80); // exe 0x5e3bc0 / 0x5e3df0
        return;
    case 2: { // exe 0x4abec6: at it; off course after 0x20 ticks -> 0; the pickup gone -> 3
        accelerateToward(a, 0x320); // exe 0x5e5450
        const int ang = angleOf(e.pos, a.pos);
        turnYawTo(a.rotation.y, uint32_t(ang), 0x80);
        integrateVelocity(a, 0, 0, a.velocity.z); // exe 0x5e3690
        --f.timer;
        if (f.timer == 0) {
            f.timer = 0x20;
            const int d = (ang - int(uint16_t(a.rotation.y))) & 0xfff;
            if (d > 0x20 && d < 0xfe0) a.step = 0;
        }
        if (edible(e) && e.airborne == 0) return;
        ++a.step;
        f.timer = 0xa;
        return;
    }
    case 3: { // exe 0x4abf62: on for 0xa ticks; another pickup -> 2, else the brake
        accelerateToward(a, 0x320);
        integrateVelocity(a, 0, 0, a.velocity.z);
        --f.timer;
        if (f.timer != 0) return;
        const int found = findPickup(a); // exe 0x4abb30
        if (found != 0) {
            f.target = uint8_t(found - 1);
            a.enterStep(2);
            f.timer = 0x20;
            return;
        }
        ++a.step;
        const uint16_t vx = uint16_t(uint32_t(uint16_t(uint16_t(a.pos.x.whole) - uint16_t(f.hitBox.previousX))) << 4);
        const uint16_t vz = uint16_t(uint32_t(uint16_t(uint16_t(a.pos.z.whole) - uint16_t(f.hitBox.previousZ))) << 4);
        a.velocity.x = int16_t(vx);
        a.velocity.z = int16_t(vz);
        a.acceleration.x = int16_t(int16_t(vx) >> 3);
        a.acceleration.z = int16_t(int16_t(vz) >> 3);
        return;
    }
    case 4: // exe 0x4ac003: the brake; stopped -> the loops
        if (decelerateToward(a, 0)) {
            toSub(a, EaterSub::Loops);
            a.velocity.x = 0;
            a.velocity.z = 0;
            a.acceleration.x = 0;
            a.acceleration.z = 0;
            f.loopPhase = 0;
        }
        slide(a);
        return;
    default: return;
    }
}
// exe 0x4ac070: a wall -- the yaw reflected, backing off 0x10 ticks, the brake -> the loops
void offWall(AnimatedActor& a, EaterFields& f) {
    switch (a.step) {
    case 0:
        f.bits = 0;
        ++a.step;
        a.tint = kTintNeutral;
        a.anim = 2;
        f.timer = 0x10;
        a.velocity.z = 0x320;
        a.acceleration.z = 0x64;
        reflectRotationOffWalls(a.rotation, a.wallHit); // exe 0x410740
        [[fallthrough]];
    case 1:
        --f.timer;
        if (f.timer == 0) ++a.step;
        break;
    case 2:
        if (decelerateToward(a, 0)) { // exe 0x5e54a0
            toSub(a, EaterSub::Loops);
            a.velocity.x = 0;
            a.velocity.z = 0;
            a.acceleration.x = 0;
            a.acceleration.z = 0;
            f.loopPhase = 0;
        }
        break;
    default: return;
    }
    integrateVelocity(a, 0, 0, a.velocity.z); // exe 0x5e3690
}
// exe 0x4ac140: the animation 7 to its end, 8 ticks of the animation 8, a hop (-0x333, gravity 0xa3) -> the loops
void hop(AnimatedActor& a, EaterFields& f) {
    switch (a.step) { // table 0x4ac220
    case 0:
        f.bits = kEaterBounces | kEaterNoFall | kEaterNoHitReact | kEaterNoHitEntry;
        ++a.step;
        a.tint = kTintNeutral;
        a.anim = 7;
        f.kept = a.pos.y.whole;
        return;
    case 1:
        if (!a.animEnded()) return;
        ++a.step;
        a.anim = 8;
        f.timer = 8;
        return;
    case 2:
        --f.timer;
        if (f.timer != 0) return;
        a.velocity.y = -0x333;
        a.acceleration.y = 0xa3;
        ++a.step;
        return;
    case 3: {
        const int16_t vy = a.velocity.y;
        if (vy > 0 && ((f.contact & kEaterLanded) || a.onPlatform != 0)) {
            toSub(a, EaterSub::Loops);
            a.velocity.y = 0;
            return;
        }
        const uint16_t ny = uint16_t(uint16_t(a.acceleration.y) + uint16_t(vy));
        a.velocity.y = int16_t(ny);
        integrateVelocity(a, 0, int16_t(ny), 0); // exe 0x5e3690
        return;
    }
    default: return;
    }
}
void fall(AnimatedActor& a, EaterFields& f) { // exe 0x4ac230: the ground lost -- until it lands -> the loops
    switch (a.step) {
    case 0:
        f.bits = kEaterBounces | kEaterNoFall | kEaterNoWallTurn | kEaterNoHitEntry;
        a.step = 1;
        a.anim = 0;
        a.velocity.y = 0;
        a.acceleration.y = 0x40;
        [[fallthrough]];
    case 1:
        if (flyStep(a, f, a.rotation.y)) { // exe 0x4ac3b0
            a.airborne = 0;
            a.velocity.y = 0;
            toSub(a, EaterSub::Loops);
        }
        return;
    default: return;
    }
}
// exe 0x4ac290 (the three knock-backs): thrown back from the hit's direction, a slide, then dying (no hit points left)
// or the hop
void knocked(AnimatedActor& a, EaterFields& f) {
    switch (a.step) { // table 0x4ac3a0
    case 0:
        f.bits = kEaterBounces | kEaterNoFall | kEaterNoHitReact | kEaterNoHitEntry;
        ++a.step;
        a.tint = kTintNeutral;
        a.anim = 5;
        a.rotation.y = int16_t(uint16_t(uint16_t(f.kept) + 0x800u));
        a.velocity.x = 0;
        a.velocity.z = 0x4c;
        a.velocity.y = -0x6d5;
        a.acceleration.y = 0x247;
        return;
    case 1:
        if (!flyStep(a, f, a.rotation.y + 0x800)) return; // exe 0x4ac3b0
        a.airborne = 0;
        a.velocity.y = 0;
        ++a.step;
        a.anim = 6;
        a.velocity.z = 0x2b;
        f.timer = 6;
        return;
    case 2:
        integrateVelocity(a, 0, a.velocity.y, a.velocity.z); // exe 0x5e3690
        --f.timer;
        if (f.timer != 0) return;
        ++a.step;
        f.timer = 5;
        return;
    case 3:
        --f.timer;
        if (f.timer != 0) return;
        if (a.hitPoints < 0) a.enterState(uint8_t(EaterState::Dying));
        else a.enterState(uint8_t(EaterState::Main), uint8_t(EaterSub::Hop));
        return;
    default: return;
    }
}

// ---- states ---------------------------------------------------------------------------------------------------------
void init(AnimatedActor& a, EaterFields& f) { // exe 0x4ab850
    if (!bindActorModel(a, kAnyModelVariant)) { // exe 0x5e0bc0(a, 0)
        freeActor(a);                           // exe 0x5e1490
        return;
    }
    a.scale.x = kEaterScale;
    a.scale.y = kEaterScale;
    a.scale.z = kEaterScale;
    a.anim = 0;
    a.prevAnim = 0xff;
    a.runFlags |= kActorRunMarked;
    const uint16_t hp = hasAnimatedStats(a.type) ? uint16_t(statsOf(a).hitPoints) : 0;
    a.maxHitPoints = int16_t(hp);
    a.hitPoints = int16_t(hp);
    a.collisionBox.pointAt(eaterBox.stateAddress());
    a.interactionBox.pointAt(eaterLiftBox.stateAddress());
    a.interaction = kInteractLift;
    a.airborne = 0;
    a.onPlatform = 0;
    a.clearClassBlock(); // exe 0x40dc40
    f.hitBox.previousX = a.pos.x.whole;
    f.hitBox.previousZ = a.pos.z.whole;
    f.hitBox.result = 0;
    a.enterState(uint8_t(EaterState::Main));
}
void mainState(const PlayerEnv& env, AnimatedActor& a, EaterFields& f) { // exe 0x4ab920
    a.tint = kTintNeutral;
    for (size_t i = StaticPool::size(); i != 0; --i) { // eats the pickups within reach (the last slot first)
        StaticActor& e = staticPool()[i - 1];
        if (edible(e) && uint32_t(distance3(e.pos, a.pos)) < kEatDistance) freeActor(e); // exe 0x5e3a10 / 0x5e1540
    }
    switch (EaterSub(a.subState)) { // table 0x894038
    case EaterSub::Loops: loops(a, f); break;
    case EaterSub::Shies: shies(env, a, f); break;
    case EaterSub::ToPickup: toPickup(a, f); break;
    case EaterSub::OffWall: offWall(a, f); break;
    case EaterSub::Hop: hop(a, f); break;
    case EaterSub::Fall: fall(a, f); break;
    case EaterSub::KnockBack:
    case EaterSub::KnockAir:
    case EaterSub::KnockAside: knocked(a, f); break;
    default: notPortedAt(kStoryPort, "0x4ab920 sub-state (table 0x894038 runs into data)", a.subState);
    }
    if (!(f.bits & kEaterNoHitReact)) hitReact(a, f); // exe 0x4ac710
    if (collide(env, a, f) != 0) {                     // exe 0x4ac7d0
        if (!(f.bits & kEaterNoFall)) {
            a.subState = uint8_t(EaterSub::Fall);
            a.airborne = 1;
            a.restartStep();
        }
    } else if ((f.contact & kEaterWalls) && !(f.bits & kEaterNoWallTurn)) {
        a.subState = uint8_t(EaterSub::OffWall);
        a.restartStep();
    }
    if (playerLiftTargetCheck(a, 0)) { // exe 0x573a60
        a.enterState(uint8_t(EaterState::Carried));
        f.bits |= kEaterNoHitEntry;
        a.runFlags |= kActorRunMarked;
    }
}
// exe 0x4ac650: flying; a solid hit turns it round (its hit points to -1); landed twice -> the hop / dying
void thrown(const PlayerEnv& env, AnimatedActor& a, EaterFields& f) {
    if (f.hitBox.result & kHitAttackSolid) {
        a.rotation.y += 0x800u;
        if (a.hitPoints >= 0) {
            a.hitPoints = -1;
            f.bits |= kEaterNoHitEntry;
        }
    }
    if (flyStep(a, f, a.rotation.y + 0x800)) { // exe 0x4ac3b0
        if (a.stepCounter == 0) {
            a.stepCounter = 1;
            a.anim = 6;
            a.airborne = 1;
            f.bits &= uint8_t(~kEaterThrownEntry);
            integrateVelocity(a, 0, a.velocity.y, a.velocity.z); // exe 0x5e3690
            collide(env, a, f);                                  // exe 0x4ac7d0
            return;
        }
        if (a.hitPoints >= 0) a.enterState(uint8_t(EaterState::Main), uint8_t(EaterSub::Hop));
        else a.enterState(uint8_t(EaterState::Dying));
        a.velocity.y = 0;
        a.airborne = 0;
    }
    collide(env, a, f);
}
// exe 0x4ac570: at the player's hands until thrown / dropped
void inHands(const PlayerEnv& env, AnimatedActor& a, EaterFields& f) {
    if (a.step == 0) {
        f.bits = kEaterBounces | kEaterNoHitEntry;
        a.anim = 1;
        a.enterStep(1);
    }
    carryAtHands(a, 0, 0, 0);                 // exe 0x573ab0
    const CarryRelease rel = carryReleased(); // exe 0x573950
    if (rel == CarryRelease::Held) return;
    if (rel == CarryRelease::Thrown) {
        int32_t v[3];
        playerAimVector(0, -0x100, -0x400, v); // exe 0x5e3e50
        a.anim = 5;
        a.velocity.x = 0;
        a.velocity.y = int16_t(v[1]);
        a.velocity.z = int16_t(v[2]);
        f.bits = kEaterBounces | kEaterThrownEntry;
    } else {
        a.velocity.y = -0x200;
        a.velocity.z = -0x100;
        f.bits = kEaterBounces;
    }
    a.acceleration.y = 0x40;
    a.rotation.y = int16_t(uint16_t(uint16_t(player().rotation.y) + 0x800u));
    a.airborne = 1;
    a.hitPoints = 1;
    carryDropFromPlayer(a); // exe 0x573990
    a.subState = uint8_t(EaterCarriedSub::Thrown);
    a.enterStep(0);
    thrown(env, a, f); // exe 0x4ac650
}
// exe 0x4ac420: the explosion (static global type 8), a blink of 0x14 (airborne: 8) ticks, the drops, freed
void dying(const PlayerEnv& env, AnimatedActor& a, EaterFields& f) {
    a.tint = kTintFlash;
    if (a.subState == 0) {
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
        if (const uint32_t address = allocActor60(0)) { // exe 0x5e1300
            StaticActor& x = staticActorAt(address);
            ExplosionParams e(x);
            x.flags = kActorLive;
            x.loopFlags |= kActorSweepGroup2;
            x.type = typeByte(GlobalStaticType::Explosion);
            x.variant = 0;
            x.pos.x.whole = a.pos.x.whole;
            x.pos.y.whole = int16_t(uint16_t(uint16_t(a.pos.y.whole) - 0x60u));
            x.pos.z.whole = a.pos.z.whole;
            e.spread = 0x30;
            e.flashes = uint16_t(f.timer);
        }
    }
    if (uint8_t(f.timer) & 1) a.tint = kTintDyingBlink;
    --f.timer;
    if (f.timer == 0) {
        spawnDrops(a.pos, statsOf(a).dropTable); // exe 0x5f1270
        freeActor(a);                            // exe 0x5e1490
        return;
    }
    integrateVelocity(a, a.velocity.x, a.velocity.y, a.velocity.z); // exe 0x5e3690
    if (a.airborne != 0) a.velocity.y = int16_t(uint16_t(a.velocity.y) + uint16_t(a.acceleration.y));
    collide(env, a, f); // exe 0x4ac7d0
    if (a.wasAirborne != 0 && a.airborne == 0) {
        a.velocity.z = 0;
        a.velocity.x = 0;
    }
}

void pickupEater(const PlayerEnv& env, AnimatedActor& a) { // exe 0x4ab6f0
    EaterFields& f = classState<EaterFields>(a);
    a.prevAnim = a.anim;
    a.savePreviousPose();
    a.savePreviousContact();
    switch (EaterState(a.state)) { // table 0x894024
    case EaterState::Init: init(a, f); break;
    case EaterState::Main: mainState(env, a, f); break;
    case EaterState::Carried: // exe 0x4ac550: table 0x8940bc
        switch (EaterCarriedSub(a.subState)) {
        case EaterCarriedSub::InHands: inHands(env, a, f); break;
        case EaterCarriedSub::Thrown: thrown(env, a, f); break;
        default: notPortedAt(kStoryPort, "0x4ac550 sub-state (table 0x8940bc runs into data)", a.subState);
        }
        break;
    case EaterState::Dying: dying(env, a, f); break;
    case EaterState::Freed: freeActor(a); break; // exe 0x468fa0 -> 0x5e1490
    default: notPortedAt(kStoryPort, "0x4ab6f0 state (table 0x894024 runs into the sub-state table)", a.state);
    }
    if (a.flags & kActorLive) {
        if (a.anim == a.prevAnim) animStepPose(a); // exe 0x5e1e80
        else animSetPose(a, a.anim, 0);            // exe 0x5e1c30
        const AnimatedActorStats row = statsOf(a);
        const uint32_t mode = f.bits;
        f.hitBox.result = 0;
        f.hitBox.previousX = a.prevPos.x.whole;
        f.hitBox.previousZ = a.prevPos.z.whole;
        if (!(mode & kEaterNoHitEntry)) {
            const ShortVec4& bp = bonePoint(a, 0, kStoryPort); // exe 0x5e1810
            const int16_t pos[3] = {bp.x, bp.y, bp.z};
            uint32_t p1, p2;
            if (!(mode & kEaterThrownEntry)) {
                p2 = uint32_t(row.attack) | kHitAttackDeflects;
                p1 = ((((uint32_t(a.slot) << 12) | (mode & kEaterNotTarget) | 0x508u) << 3 | row.hitHeight) << 9) |
                     (uint32_t(row.hitSize) >> 2);
            } else {
                p2 = uint32_t(row.shotAttack) | kHitAttackByPlayer;
                p1 = (uint32_t(row.hitSize) >> 2) | (uint32_t(a.slot) << 24) | 0x288000u;
            }
            hitListSubmit(pos, f.hitBox, p1, p2); // exe 0x5e4120
        }
        // u16 +8 != 2: exe 0x5f20f0(a, 0x30, 0x160, 0): the shadow (render output)
    }
    actorCollisionSubmitMover(a); // exe 0x4140b0
}

} // namespace

void pickupEaterUpdate(const PlayerEnv& env, uint32_t actor) { pickupEater(env, animatedActorAt(actor)); }

} // namespace od::port
