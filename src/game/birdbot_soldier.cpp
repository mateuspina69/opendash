// The Birdbot soldier class 0x493a60 (see birdbot_soldier.hpp). States [0x892c0c]: 0 init 0x493e30, 1 0x494100 (the
// behaviour [0x892c20 + +9 * 4], 15 sub-states), 2 carried 0x495580 (sub-states [0x892c68]: 0x4955a0 at the hands,
// 0x495720 thrown / dropped), 3 dead 0x495860 (the remains: a knocked-down Birdbot of variant +0x165 | 0x80), 4 0x468fa0
// (freed). Its animations are model-bank indices keyed by the behaviour. The shadow's entry (exe 0x5f20f0) is render
// output, its ground probe is kept (the collision scratch it leaves).
#include "game/actor_state.hpp"
#include "game/birdbot_soldier_state.hpp"
#include "game/birdbot_soldier.hpp"

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
#include "game/encounter.hpp"
#include "game/enemy_motion.hpp"
#include "game/enemy_neighbours.hpp"
#include "game/game_state.hpp"
#include "game/hit_effects.hpp"
#include "game/fixed_point.hpp"
#include "game/hit_list.hpp"
#include "game/hit_points.hpp"
#include "game/player/player_motion.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/shadows.hpp"
#include "game/stage_state.hpp"
#include "game/vectors.hpp"
#include "game/visibility.hpp"
#include "game/walker_shot.hpp"

namespace od::port {

using namespace birdbot_soldier_state;

namespace {

constexpr const char* kSoldierPort = "birdbot soldier port"; // the area of its port-gap errors

// +0x15e (besides kBirdbotBounces)
constexpr uint8_t kSoldierButt = 0x02, kSoldierNoHitEntry = 0x04, kSoldierNoFall = 0x08, kSoldierThrown = 0x10;
constexpr uint8_t kSoldierNoDamage = 0x20, kSoldierStays = 0x40, kSoldierNoMover = 0x80;
// +0xc
constexpr uint8_t kRangeChaser = 3, kRangeStopper = 4; // the range classes that start / end the chase near the player

enum class SoldierState : uint8_t { Init = 0, Main = 1, Carried = 2, Dead = 3, Freed = 4 };
enum class SoldierSub : uint8_t { // [0x892c20]
    Wake = 0,      // exe 0x4941f0
    Walk = 1,      // exe 0x494240
    GiveWay = 2,   // exe 0x4943c0
    WallTurn = 3,  // exe 0x494540
    Shoot = 4,     // exe 0x494610
    Butt = 5,      // exe 0x494900
    Alarm = 6,     // exe 0x494ab0
    KnockBack = 7, // exe 0x494bc0
    Overturn = 8,  // exe 0x494d10
    Fall = 9,      // exe 0x494d90
    DropIn = 10,   // exe 0x494e20
    JumpIn = 11,   // exe 0x494ed0
    GetUp = 12,    // exe 0x495080
    WaitLeap = 13, // exe 0x4950f0
    Gangway = 14,  // exe 0x4951f0
};
enum class SoldierCarriedSub : uint8_t { InHands = 0, Flying = 1 }; // exe 0x4955a0 / 0x495720

const DataTable<CollisionBoxRecord> soldierBox{TableId::BirdbotSoldierBox};
const DataTable<InteractionBoxRecord> soldierLiftBox{TableId::BirdbotSoldierLiftBox};
const DataTable<ScalarTable<int16_t, 6>> shootRanges{TableId::BirdbotSoldierShootRanges}, buttRanges{TableId::BirdbotSoldierButtRanges}; // by the range class
const DataTable<StridedScalarTable<uint8_t, 0x10, 4>> carryCries{TableId::BirdbotSoldierCarryCries};
constexpr uint8_t kBoatType = 0x21; // the ST17 boat's static type
constexpr int16_t kWalkSpeed = -0x140, kGravity = 0x40, kFallGravity = 0x18;
constexpr uint16_t kSoundHurt = 0x8d, kSoundButt = 0x111, kSoundAlarm = 0x195, kSoundKnock = 400;
constexpr uint16_t kSoundKnockHard = 0x192, kSoundDrop = 0x193, kSoundCry = 0x194, kSoundThrown = 0x196;
constexpr uint16_t kSoundHitFlying = 0x191;
constexpr uint8_t kSparkLoops = 2;
constexpr uint32_t kKnockHurt = 0x38; // the hurts summed that knock it back

// the type's row of the Birdbot table (its 0x10-byte sub-row 1: the soldier's)
AnimatedActorStats statsOf(const AnimatedActor& a) { return animatedStats(a.type, 1); }
void toSub(AnimatedActor& a, SoldierSub s) {
    a.subState = uint8_t(s);
    a.enterStep(0);
}
// the player is down (its knock-down) or dead: the group's call starts the alarm
bool playerDown() {
    const Player& p = player();
    return (p.state == uint8_t(PlayerState::Playing) && p.subState == uint8_t(PlayerSub::KnockDown)) ||
           p.state == uint8_t(PlayerState::Dead);
}

// A stack local the exe may read before it writes it (exe 0x4953b0 returns early without its outputs; the exe then
// compares stale stack contents -- not reproducible): an unwritten one reads as 0x10000000 (far / way off to the side),
// so the comparisons that use it fail. A deliberate deviation, only where the exe reads garbage.
struct StaleLocal {
    int v = 0x10000000;
    bool set = false;
};

// exe 0x494380(actor, &local): the target -- the player while the chase runs, else the remembered point
void target(const SoldierFields& f, int32_t& tx, int32_t& tz) {
    if (f.chase == 0) {
        tx = int32_t(uint32_t(f.targetX) << 16);
        tz = int32_t(uint32_t(f.targetZ) << 16);
    } else {
        tx = int32_t(uint32_t(uint16_t(player().pos.x.whole)) << 16);
        tz = int32_t(uint32_t(uint16_t(player().pos.z.whole)) << 16);
    }
}
int targetDistance(const AnimatedActor& a, int32_t tx, int32_t tz) { // exe 0x5e3b00(+0x10, &local)
    const int32_t local[3] = {tx, 0, tz};
    return planarDistance(a.pos, local);
}
int targetAngle(const AnimatedActor& a, int32_t tx, int32_t tz) { return angleOf(tx, tz, a.pos.x, a.pos.z); } // 0x5e3bc0
bool guarding(const SoldierFields& f) {
    const uint8_t c = f.range;
    return (c & kSoldierRangeMask) != 0 && f.chase == 0 && (c & kSoldierGuards);
}
// exe 0x4953b0: in front (+-0x100) and within its shoot range -> 1, within its butt range -> 2; else 0 (a guard that
// does not chase: 0 without its outputs)
int inRange(const AnimatedActor& a, const SoldierFields& f, int32_t tx, int32_t tz, int& diff, int& dist) {
    if (guarding(f)) return 0;
    diff = angleDiff(uint32_t(int32_t(a.rotation.y)), uint32_t(targetAngle(a, tx, tz)));
    dist = targetDistance(a, tx, tz);
    const uint32_t k = f.range & kSoldierRangeMask;
    if (uint32_t(diff + 0x100) <= 0x200 && dist <= shootRanges[k]) return (dist <= buttRanges[k]) + 1;
    return 0;
}
int range(const AnimatedActor& a, const SoldierFields& f, int32_t tx, int32_t tz, StaleLocal& diff, StaleLocal& dist) {
    int d = 0, r = 0;
    const int k = inRange(a, f, tx, tz, d, r);
    if (!guarding(f)) {
        diff = {d, true};
        dist = {r, true};
    }
    return k;
}
void attack(AnimatedActor& a, int r) { // exe 0x495440
    if (r == 1) toSub(a, SoldierSub::Shoot);
    else if (r == 2) toSub(a, SoldierSub::Butt);
}
// exe 0x495470(actor, block, p3, p4) with its two locals as passed: the group's call (its mark bit 0x100 << the call
// bit) or the player down close in front -> the alarm
void groupCall(AnimatedActor& a, SoldierFields& f, const StaleLocal& p3, const StaleLocal& p4) {
    if (f.chase == 0 && (f.range & kSoldierRangeMask) != 0) {
        if ((f.range & kSoldierRangeMask) < 3 &&
            ((0x100u << (f.callBit & 0x1f)) & enemyGroupMarks(f.group, EnemyGroupMarks::Either)) != 0) {
            if (p4.v < 1000) {
                toSub(a, SoldierSub::Alarm);
                return;
            }
            if ((f.range & kSoldierRangeMask) != 0 && f.chase == 0) f.range &= 0xf0u;
            int32_t tx, tz;
            target(f, tx, tz);
            StaleLocal d2, r2;
            const int r = range(a, f, tx, tz, d2, r2);
            if (r != 0) {
                attack(a, r);
                return;
            }
            toSub(a, SoldierSub::Wake);
        }
        return;
    }
    if (uint32_t(p3.v + 0x200) < 0x401 && p4.v < 0x3e9 && playerDown()) toSub(a, SoldierSub::Alarm);
}
int lastAnim(AnimatedActor& a) { // exe 0x4948c0: the walk (2) -> its end (3) once, then 1 when it ends
    if (a.stepCounter == 0) {
        if (a.anim != 2) return 1;
        a.anim = 3;
        a.stepCounter = 1;
    } else if (a.animEnded()) {
        return 1;
    }
    return 0;
}
// exe 0x4959f0: a hit: the sound, the hurt summed (0x38 or more: a knock), strong hits -> a knock; the flash, the hit
// points; 0 no reaction, 1 a knock, 2 dead
int damage(AnimatedActor& a, SoldierFields& f) {
    const uint32_t v = f.hitBox.result, dmg = v & kHitDamageMask;
    if (f.bits & kSoldierNoDamage) return 0;
    f.hitKind = 0;
    if (dmg == 0 && !(v & kHitAttackWater)) return 0;
    soundPlayAtPos(kSoundHurt, a.pos);
    int r = 0;
    if (a.airborne == 0) {
        const bool fill = uint32_t(f.hurt) + dmg < kKnockHurt;
        if (fill) {
            f.hurt = uint8_t(f.hurt + dmg);
        } else {
            f.hurt = 0;
            f.hitKind = 1;
        }
        const bool strong = (v & (kHitLevelWithin | kHitAttackThrown | kHitAttackWater)) != 0;
        if (strong) f.hitKind = 1;
        r = (strong || !fill) ? 1 : 0;
        bool chased = r != 0;
        if (v & (kHitLevelBelow | kHitAttackStrongPush)) {
            r = 1;
            f.hitKind = 2;
            chased = true;
        }
        if (chased && (f.range & kSoldierRangeMask) > 1 && f.chase == 0) f.chase = 0xf0;
    }
    a.tint = kTintFlash;
    if (hitPointsAdd(a, -int(dmg)) != 0) r = 2; // exe 0x495ad5: dead -> 2 (the remains)
    return r;
}

// ---- sub-states ---------------------------------------------------------------------------------------------------
void wake(AnimatedActor& a, SoldierFields& f) { // exe 0x4941f0: (anim 1) -> walk
    if (a.step == 0) {
        f.bits = 0;
        a.anim = 1;
        a.velocity.y = 0;
        a.velocity.z = kWalkSpeed;
        a.airborne = 0;
        f.parts = 2;
        a.enterStep(1);
        return;
    }
    if (a.animEnded()) {
        a.anim = 2;
        toSub(a, SoldierSub::Walk);
    }
}
void wallTurn(AnimatedActor& a, SoldierFields& f);
void walk(AnimatedActor& a, SoldierFields& f) { // exe 0x494240: to the target; the ranges, neighbours, the group, walls
    int32_t tx, tz;
    target(f, tx, tz);
    if (a.step == 0) {
        a.anim = 2;
        f.timer = 0;
        a.enterStep(1);
    }
    if (stepTowardLimit(a.velocity.z, 0xfff0, 0x140) != 0) a.velocity.z = kWalkSpeed; // exe 0x5e5510
    turnYawTo(a.rotation.y, uint32_t(targetAngle(a, tx, tz)), (f.timer & 3) * 8 + 0x30);
    integrateVelocity(a, 0, 0, a.velocity.z);
    StaleLocal diff{int(a.at), true}, dist; // the exe's param_1 (the actor's address until written) and local_10
    const int r = range(a, f, tx, tz, diff, dist);
    if (r != 0) attack(a, r);
    const uint8_t k = f.range & kSoldierRangeMask;
    if (k == kRangeChaser) {
        if (planarDistance(a.pos, player().pos) < 900) f.chase = 0xf0;
    } else if (k == kRangeStopper) {
        if (planarDistance(a.pos, player().pos) < 900) f.range = 0;
    }
    if (neighbourFind(a, f.neighbour, 0) != 0 && !(f.range & kSoldierAlone)) toSub(a, SoldierSub::GiveWay); // 0x5e5e00
    groupCall(a, f, dist, diff); // exe 0x495470(a, a + 0x14c, local_10, param_1)
    if (f.contact & kBirdbotWall) {
        toSub(a, SoldierSub::WallTurn);
        wallTurn(a, f);
    }
}
void giveWay(AnimatedActor& a, SoldierFields& f) { // exe 0x4943c0: the neighbour first (0x5a ticks at most)
    int32_t tx, tz;
    target(f, tx, tz);
    const Actor& o = neighbourActor(uint16_t(f.neighbour)); // exe 0x5e5cb0
    if (a.step == 0) {
        f.bits = 0;
        f.timer = 0x5a;
        neighbourClassify(a, f.neighbour); // exe 0x5e5f70
        a.enterStep(1);
    }
    const uint16_t b = f.neighbour;
    if (b & kNeighbourOpposite)
        turnYawTo(a.rotation.y, uint32_t(headingFromOther(a, o, neighbourSide(f.neighbour), 0x100)), 0x30); // 0x5e5d90
    if ((b & kNeighbourCrossingSlow) && stepTowardLimit(a.velocity.z, 0xfff0, 0x140) != 0) a.velocity.z = kWalkSpeed;
    if ((b & kNeighbourCatching) && stepTowardLimit(a.velocity.z, 0xfff0, 0xc0) != 0) a.velocity.z = -0xc0;
    // apart once the high bytes of their integer x / z differ (the exe's byte compares at +0x13 / +0x1b)
    auto high = [](const Fixed16& v) { return uint8_t(uint16_t(v.whole) >> 8); };
    if (o.flags == 0 || high(o.pos.x) != high(a.pos.x) || high(o.pos.z) != high(a.pos.z)) toSub(a, SoldierSub::Walk);
    --f.timer;
    if (f.timer == 0) toSub(a, SoldierSub::Walk);
    integrateVelocity(a, 0, 0, a.velocity.z);
    StaleLocal l14, l10;
    const int r = range(a, f, tx, tz, l14, l10);
    if (f.timer < 0x4b && r != 0) attack(a, r);
    groupCall(a, f, l10, l14);
    if (f.contact & kBirdbotWall) {
        toSub(a, SoldierSub::WallTurn);
        wallTurn(a, f);
    }
}
void wallTurn(AnimatedActor& a, SoldierFields& f) { // exe 0x494540: away from the wall (its sides) while walking
    if (a.step == 0) {
        f.bits = 0;
        uint32_t r = a.actorHit;
        if (r == 0) r = a.wallHit;
        int32_t dx = 0, dz = 0;
        if (r & 0x100) dx = -0x1000000;
        if (r & 0x200) dx = 0x1000000;
        if (r & 0x400) dz = -0x1000000;
        if (r & 0x800) dz = 0x1000000;
        const int32_t lx = int32_t(uint32_t(dx) + uint32_t(a.pos.x)), lz = int32_t(uint32_t(dz) + uint32_t(a.pos.z));
        const int d = angleDiff(uint32_t(int32_t(a.rotation.y)), uint32_t(angleOf(lx, lz, a.pos.x, a.pos.z)));
        a.enterStep(1);
        a.spin.y = int16_t(d >= 0 ? 0x30 : -0x30);
    }
    integrateVelocity(a, 0, 0, a.velocity.z);
    a.rotation.y = int16_t(uint16_t(a.rotation.y) + uint16_t(a.spin.y));
    if (!(f.contact & kBirdbotWall)) toSub(a, SoldierSub::Walk);
}
// exe 0x494610: face the target, then the shot (game/walker_shot) from bone 2 with a recoil
void shoot(AnimatedActor& a, SoldierFields& f) {
    int32_t tx, tz;
    target(f, tx, tz);
    switch (a.step) {
    case 0:
        f.bits = 0;
        f.parts = 2;
        a.velocity.z = 0;
        a.acceleration.z = 0;
        a.enterStep(1);
        [[fallthrough]];
    case 1:
        if (lastAnim(a) != 0) {
            a.anim = 0;
            f.timer = 0x1e;
            a.enterStep(2);
        }
        return;
    case 2: {
        uint32_t ang = uint32_t(targetAngle(a, tx, tz));
        if (pointVisibleOrNear(a.pos, 0x40) == 0) f.sideOffset = 0x40;
        else if ((f.range & kSoldierRangeMask) == 0 || f.chase != 0) ang += f.sideOffset;
        const int diff = turnYawTo(a.rotation.y, ang, 8);
        const int d2 = targetDistance(a, tx, tz);
        if (uint32_t(diff + 0x120) < 0x241 && d2 <= shootRanges[f.range & kSoldierRangeMask] + 0x100) {
            if (f.timer != 0) --f.timer;
            if (f.timer == 0 && (enemyGroupMarks(f.group, EnemyGroupMarks::ThisTick) & 1) == 0) {
                a.anim = 5;
                a.enterStep(3);
                enemyGroupMark(f.group, 1);
            }
            groupCall(a, f, StaleLocal{diff, true}, StaleLocal{d2, true});
            return;
        }
        toSub(a, SoldierSub::Wake);
        return;
    }
    case 3: {
        enemyGroupMark(f.group, 1);
        if (a.animKey.events & 1) {
            if (const uint32_t address = allocActor40(kActorStageClass)) { // exe 0x5e1280
                ProjectileActor& s = projectileActorAt(address);
                s.flags = kActorLive;
                s.type = kWalkerShotStageType;
                s.variant = 0;
                const int16_t in[3] = {0, 0, 0};
                int16_t out[3];
                bonePointOffset(a, 2, in, out);
                s.pos.x.whole = out[0];
                s.pos.y.whole = out[1];
                s.pos.z.whole = out[2];
                s.rotation.y = a.rotation.y;
                a.velocity.z = 0x100;
                a.acceleration.z = -0x40;
                if (f.sideOffset != 0) f.sideOffset = uint8_t(f.sideOffset >> 1);
            }
        }
        integrateVelocity(a, 0, 0, a.velocity.z);
        a.velocity.z = int16_t(uint16_t(a.velocity.z) + uint16_t(a.acceleration.z));
        if (int16_t(uint16_t(a.velocity.z) ^ uint16_t(a.acceleration.z)) >= 0) {
            a.acceleration.z = 0;
            a.velocity.z = 0;
        }
        if (a.animEnded()) {
            a.acceleration.z = 0;
            a.velocity.z = 0;
            a.anim = 6;
            a.enterStep(4);
        }
        return;
    }
    case 4:
        if (a.animEnded()) {
            StaleLocal p1{int(a.at), true}, l20;
            if (range(a, f, tx, tz, p1, l20) != 1) a.subState = uint8_t(SoldierSub::Wake);
            a.enterStep(0);
            groupCall(a, f, l20, p1);
        }
        return;
    default: return;
    }
}
void butt(AnimatedActor& a, SoldierFields& f) { // exe 0x494900: the butt (anims 7 / 8 / 9, the attack entry, a sound)
    int32_t tx, tz;
    target(f, tx, tz);
    switch (a.step) {
    case 0:
        f.bits = 0;
        f.parts = 2;
        a.enterStep(1);
        [[fallthrough]];
    case 1:
        if (lastAnim(a) != 0) a.enterStep(2);
        return;
    case 2:
        a.anim = 7;
        a.enterStep(3);
        return;
    case 3:
        if (a.animKey.events & 1) {
            f.parts = 3;
            f.bits |= kSoldierButt;
        }
        if (a.animKey.frame == 8) soundPlayAtPos(kSoundButt, a.pos);
        if (a.animEnded()) {
            StaleLocal l10, p1;
            const int r = range(a, f, tx, tz, l10, p1);
            a.enterStep(r != 2 ? 6 : 4);
        }
        return;
    case 4:
        a.anim = 8;
        f.bits &= uint8_t(~kSoldierButt);
        a.enterStep(5);
        return;
    case 5:
        turnYawTo(a.rotation.y, uint32_t(targetAngle(a, tx, tz) - 0x30), 0x20);
        if (a.animEnded()) {
            a.anim = 7;
            f.animKeyStart = 6;
            f.bits |= kSoldierButt;
            a.enterStep(3);
        }
        return;
    case 6:
        a.anim = 9;
        f.bits &= uint8_t(~kSoldierButt);
        a.enterStep(7);
        return;
    case 7:
        if (a.animKey.events & 1) f.parts = 2;
        toSub(a, SoldierSub::Wake);
        return;
    default: return;
    }
}
void alarm(AnimatedActor& a, SoldierFields& f) { // exe 0x494ab0: the alarm (a sound, anim 0xb for 0x5a ticks, then 0xc)
    switch (a.step) {
    case 0:
        f.bits = 0;
        f.parts = 2;
        a.enterStep(1);
        soundPlayAtPos(kSoundAlarm, a.pos);
        [[fallthrough]];
    case 1:
        if (lastAnim(a) != 0) a.enterStep(2);
        return;
    case 2:
        a.anim = 0xb;
        f.timer = 0x5a;
        a.enterStep(3);
        return;
    case 3:
        --f.timer;
        if (f.timer == 0) {
            a.anim = 0xc;
            a.enterStep(4);
        }
        return;
    case 4:
        if (a.animEnded()) {
            if ((f.range & kSoldierRangeMask) != 0 && f.chase == 0) f.range &= 0xf0u;
            int32_t tx, tz;
            target(f, tx, tz);
            StaleLocal l10, p1;
            const int r = range(a, f, tx, tz, l10, p1);
            if (r == 0) {
                toSub(a, SoldierSub::Wake);
                return;
            }
            attack(a, r);
        }
        return;
    default: return;
    }
}
// exe 0x494bc0: knocked back (anim 0x18 hit from the front / 0x17 from behind)
void knockBack(AnimatedActor& a, SoldierFields& f) {
    const uint8_t s = a.step;
    if (s == 0) {
        f.bits = kSoldierNoDamage;
        a.velocity.z = 0x180;
        a.acceleration.z = -0x30;
        uint16_t snd = kSoundKnock;
        if (f.hitKind == 2) {
            a.velocity.z = 0x280;
            snd = kSoundKnockHard;
        }
        soundPlayAtPos(snd, a.pos);
        const int16_t me[3] = {a.pos.x.whole, a.pos.y.whole, a.pos.z.whole};
        const int ang = atanAngle(float(me[0] - f.hitBox.contact.x), float(me[2] - f.hitBox.contact.z)) & kAngleMask;
        const int d = angleDiff(uint32_t(int32_t(a.rotation.y)), uint32_t(ang)); // exe 0x5e3c00(contact, me)
        if (uint32_t(d + kQuarterTurn) < uint32_t(kHalfTurn)) {
            a.anim = 0x18;
            f.timer = 8;
            a.enterStep(1);
            return;
        }
        a.anim = 0x17;
        f.timer = 9;
        a.velocity.z = int16_t(-int(a.velocity.z));
        a.acceleration.z = int16_t(-int(a.acceleration.z));
        a.enterStep(1);
        return;
    }
    if (s == 1) {
        integrateVelocity(a, 0, 0, a.velocity.z);
        a.velocity.z = int16_t(uint16_t(a.velocity.z) + uint16_t(a.acceleration.z));
        --f.timer;
        if (f.timer == 0 || int16_t(uint16_t(a.velocity.z) ^ uint16_t(a.acceleration.z)) >= 0) {
            a.enterStep(2);
            f.bits &= uint8_t(~kSoldierNoDamage);
        }
    } else if (s != 2) {
        return;
    }
    if (a.animEnded()) toSub(a, SoldierSub::Wake);
}
void overturn(AnimatedActor& a, SoldierFields& f) { // exe 0x494d10: (anims 0x12 / 0x13); dead -> the remains (variant 2)
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
        toSub(a, SoldierSub::Wake);
        if (a.hitPoints < 0) {
            a.enterState(uint8_t(SoldierState::Dead));
            f.remainsVariant = 2;
        }
    }
}
void fall(AnimatedActor& a, SoldierFields& f) { // exe 0x494d90: the ground lost, anim 0xf -> 0x10 on landing
    const uint8_t s = a.step;
    if (s == 0) {
        a.anim = 0xf;
        a.acceleration.y = kFallGravity;
        a.velocity.y = 0;
        if (f.parts > 1) f.parts = 3;
        f.bits = uint8_t(kSoldierNoFall | kBirdbotBounces);
        a.airborne = 1;
        a.enterStep(1);
    } else if (s != 1) {
        if (s != 2) return;
        if (a.animEnded()) toSub(a, SoldierSub::Wake);
        return;
    }
    if (birdbotArc(a, f, a.rotation.y) != 0) {
        a.anim = 0x10;
        a.enterStep(2);
        a.airborne = 0;
    }
}
void land(AnimatedActor& a) { // the landing of the drops: registered (exe 0x5e5c10(slot, 0)), -> get up
    registerEnemy(a.slot, 0);
    toSub(a, SoldierSub::GetUp);
    a.airborne = 0;
}
void dropIn(AnimatedActor& a, SoldierFields& f) { // exe 0x494e20: dropped in (variant 1; a sound)
    if (a.step == 0) {
        a.anim = 0xf;
        a.acceleration.y = kFallGravity;
        a.velocity.y = 0;
        f.parts = 1;
        f.bits = uint8_t(kSoldierNoFall | kBirdbotBounces);
        a.airborne = 1;
        soundPlayAtPos(kSoundDrop, a.pos);
        a.enterStep(1);
    } else if (a.step != 1) {
        if (f.contact & kBirdbotLanded) land(a);
        return;
    }
    if (birdbotArc(a, f, a.rotation.y) != 0) land(a);
    if (f.contact & kBirdbotLanded) land(a);
}
void jumpIn(AnimatedActor& a, SoldierFields& f) { // exe 0x494ed0: jumps in (variant 2; anims 0xd / 0xe / 0xf, a sound)
    switch (a.step) {
    case 0:
        a.anim = 0xd;
        a.acceleration.y = kFallGravity;
        a.velocity.y = 0x200;
        f.parts = 1;
        f.bits = uint8_t(kSoldierNoFall | kBirdbotBounces);
        soundPlayAtPos(kSoundDrop, a.pos);
        a.airborne = 1;
        a.enterStep(1);
        break;
    case 1:
        if (a.animEnded()) {
            a.anim = 0xe;
            a.enterStep(2);
        }
        integrateVelocity(a, 0, a.velocity.y, a.velocity.z);
        break;
    case 2:
        integrateVelocity(a, 0, a.velocity.y, a.velocity.z);
        if (int(f.groundHeight) - 0x5e < int(a.pos.y.whole)) {
            a.anim = 0xf;
            a.enterStep(3);
        }
        break;
    case 3:
        a.anim = 0xf;
        f.timer = 0xf;
        a.enterStep(4);
        [[fallthrough]];
    case 4:
        a.velocity.y = int16_t(uint16_t(a.velocity.y) - 0x80u);
        if (a.velocity.y < -0x40) a.velocity.y = -0x40;
        integrateVelocity(a, 0, a.velocity.y, a.velocity.z);
        if (a.velocity.y < 1) {
            --f.timer;
            if (f.timer == 0) a.enterStep(5);
        }
        break;
    case 5:
        a.acceleration.y = kGravity;
        a.enterStep(6);
        [[fallthrough]];
    case 6:
        if (birdbotArc(a, f, a.rotation.y) != 0) land(a);
        break;
    default: break;
    }
    if (f.contact & kBirdbotLanded) land(a);
}
void getUp(AnimatedActor& a, SoldierFields& f) { // exe 0x495080: (anims 0x10 / 4)
    const uint8_t s = a.step;
    if (s == 0) {
        a.anim = 0x10;
        f.parts = 1;
        f.bits = uint8_t(kSoldierNoFall | kBirdbotBounces);
        a.enterStep(1);
    } else if (s == 1) {
        if (a.animEnded()) {
            a.anim = 4;
            a.enterStep(2);
        }
    } else if (s == 2) {
        if (a.animKey.events & 1) f.parts = 2;
        if (a.animEnded()) toSub(a, SoldierSub::Wake);
    }
}
void waitLeap(AnimatedActor& a, SoldierFields& f) { // exe 0x4950f0: waits for its boat slot byte 0, then leaps (variant 3)
    switch (a.step) {
    case 0:
        f.bits = 0x4d; // kSoldierStays | kSoldierNoFall | kSoldierNoHitEntry | kBirdbotBounces
        a.enterStep(1);
        [[fallthrough]];
    case 1:
        if (f.boatSlot == 0) a.enterStep(2);
        return;
    case 2: {
        a.anim = 0x1a;
        a.acceleration.y = kFallGravity;
        const uint32_t r = rngStep() & 3;
        a.velocity.z = -0x80;
        a.velocity.y = int16_t(-0x180 - int(r << 4));
        f.parts = 1;
        f.bits = uint8_t(kSoldierNoFall | kBirdbotBounces);
        a.airborne = 1;
        a.enterStep(3);
        [[fallthrough]];
    }
    case 3:
        integrateVelocity(a, 0, a.velocity.y, a.velocity.z);
        a.velocity.z = int16_t(uint16_t(a.velocity.z) + 8u);
        if (a.velocity.z >= 0) a.velocity.z = 0;
        a.velocity.y = int16_t(uint16_t(a.velocity.y) + 0x40u);
        if (a.velocity.y > 0x1ff) {
            a.velocity.z = 0;
            a.velocity.y = 0x200;
            a.anim = 0xe;
            a.subState = uint8_t(SoldierSub::JumpIn);
            a.enterStep(2);
        }
        return;
    default: return;
    }
}
// exe 0x4952c0(a, r, yaw): on the static of its boat slot (alive, the ST17 boat, a stage static): 0x240 along its yaw,
// then r (at most 0xe0) along its yaw + yaw, up by r * 0x20 / 0xe0 + 0xe0 (the gangway), facing its yaw + yaw
int onBoat(AnimatedActor& a, SoldierFields& f, uint32_t r, int16_t yaw) {
    const StaticActor& b = staticPool()[size_t(int32_t(f.boatSlot) - int32_t(kStaticClass))];
    if (!(b.flags & kActorLive) || b.type != kBoatType || !(b.loopFlags & kActorStageClass)) return 0;
    if (r > 0xe0) r = 0xe0;
    int16_t dx, dz;
    angleVector(b.rotation.y, 0x240, dx, dz);
    a.pos.x.whole = int16_t(b.pos.x.whole + dx);
    a.pos.y.whole = b.pos.y.whole;
    a.pos.z.whole = int16_t(b.pos.z.whole + dz);
    angleVector(int16_t(uint16_t(b.rotation.y) + uint16_t(yaw)), int16_t(r), dx, dz);
    a.pos.x.whole = int16_t(a.pos.x.whole + dx);
    a.pos.z.whole = int16_t(a.pos.z.whole + dz);
    a.pos.y.whole = int16_t(a.pos.y.whole + int(r * 0x20) / 0xe0 + 0xe0);
    a.rotation.y = int16_t(uint16_t(b.rotation.y) + uint16_t(yaw));
    return 1;
}
void gangway(AnimatedActor& a, SoldierFields& f) { // exe 0x4951f0: down the boat's gangway, then 6 ticks on
    const uint8_t s = a.step;
    if (s == 0) {
        f.bits = 0xe0; // kSoldierNoMover | kSoldierStays | kSoldierNoDamage
        a.anim = 2;
        a.velocity.y = 0;
        a.velocity.z = -0x50;
        a.airborne = 1;
        f.parts = 2;
        f.timer = 0;
        a.enterStep(1);
    } else if (s == 2) {
        integrateVelocity(a, 0, a.velocity.y, a.velocity.z);
        --f.timer;
        if (f.timer == 0) toSub(a, SoldierSub::Walk);
        return;
    } else if (s != 1) {
        return;
    }
    onBoat(a, f, f.timer, f.boatYaw);
    const uint8_t t = uint8_t(f.timer + 0x14);
    f.timer = t;
    if (t > 0xdf) {
        f.bits &= 0x17u;
        a.airborne = 0;
        a.enterStep(2);
        f.timer = 6;
    }
}

// ---- states ---------------------------------------------------------------------------------------------------------
void dead(AnimatedActor& a, SoldierFields& f) { // exe 0x495860: the remains (a knocked-down Birdbot), freed
    if (a.subState == 0) {
        countDefeat(); // exe 0x5e53e0
        if (const uint32_t address = allocActor20()) { // exe 0x5e1160
            AnimatedActor& r = animatedActorAt(address);
            r.flags = kActorLive;
            r.type = kBirdbotType;
            r.subtype = 0;
            r.variant = uint8_t(f.remainsVariant | 0x80u);
            r.params[0] = 0; // the knocked-down Birdbot's model variant
            r.pos = a.pos;
            r.rotation.y = a.rotation.y;
            if (f.trackId != 0) {
                encounterUntrack(a); // exe 0x5e6540
                encounterTrackInScriptSlot(r, uint16_t(int16_t(f.trackId))); // exe 0x5e6500
            }
        }
    }
    freeActor(a); // exe 0x5e1490
}
void mainState(const PlayerEnv& env, AnimatedActor& a, SoldierFields& f) { // exe 0x494100
    if ((f.range & kSoldierRangeMask) == 0) {
        f.targetX = uint16_t(player().pos.x.whole);
        f.targetZ = uint16_t(player().pos.z.whole);
    }
    if (a.actorHit != 0) birdbotContactHits(a, f);
    if (a.wasOnPlatform != 0 && a.onPlatform == 0 && !(f.bits & kSoldierNoFall)) toSub(a, SoldierSub::Fall);
    const int d = damage(a, f);
    if (d == 1) {
        toSub(a, SoldierSub::KnockBack);
    } else if (d == 2) {
        spawnHitSpark(kSparkLoops, a.pos.x.whole, a.pos.y.whole, a.pos.z.whole);
        a.enterState(uint8_t(SoldierState::Dead));
        f.remainsVariant = 0;
        dead(a, f);
        return;
    }
    switch (SoldierSub(a.subState)) { // [0x892c20]
    case SoldierSub::Wake: wake(a, f); break;
    case SoldierSub::Walk: walk(a, f); break;
    case SoldierSub::GiveWay: giveWay(a, f); break;
    case SoldierSub::WallTurn: wallTurn(a, f); break;
    case SoldierSub::Shoot: shoot(a, f); break;
    case SoldierSub::Butt: butt(a, f); break;
    case SoldierSub::Alarm: alarm(a, f); break;
    case SoldierSub::KnockBack: knockBack(a, f); break;
    case SoldierSub::Overturn: overturn(a, f); break;
    case SoldierSub::Fall: fall(a, f); break;
    case SoldierSub::DropIn: dropIn(a, f); break;
    case SoldierSub::JumpIn: jumpIn(a, f); break;
    case SoldierSub::GetUp: getUp(a, f); break;
    case SoldierSub::WaitLeap: waitLeap(a, f); break;
    case SoldierSub::Gangway: gangway(a, f); break;
    default: notPortedAt(kSoldierPort, "class 0x493a60 sub-state", a.subState);
    }
    if (birdbotCollide(env, a, f, f.groundHeight) != 0 && !(f.bits & kSoldierNoFall)) toSub(a, SoldierSub::Fall);
    if (playerLiftTargetCheck(a, 0)) { // exe 0x573a60
        a.enterState(uint8_t(SoldierState::Carried));
        a.onPlatform = 0;
        f.bits |= kSoldierNoHitEntry;
    }
}
void flying(const PlayerEnv& env, AnimatedActor& a, SoldierFields& f);
void inHands(const PlayerEnv& env, AnimatedActor& a, SoldierFields& f) { // exe 0x4955a0: its cries until let go
    if (a.step == 0) {
        if (f.parts < 2) {
            a.anim = 0x16;
        } else {
            a.anim = 0x15;
            f.parts = 2;
        }
        f.timer = carryCries[rngStep() & 0xf];
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
        f.bits = uint8_t(kSoldierThrown | kBirdbotBounces);
        soundPlayAtPos(kSoundThrown, a.pos);
    } else {
        a.velocity.y = -0x200;
        a.velocity.z = -0x100;
        f.bits = kBirdbotBounces;
        soundPlayAtPos(kSoundKnockHard, a.pos);
    }
    a.acceleration.y = kGravity;
    a.rotation.y = player().rotation.y;
    a.airborne = 1;
    carryDropFromPlayer(a); // exe 0x573990
    a.subState = uint8_t(SoldierCarriedSub::Flying);
    a.enterStep(0);
    flying(env, a, f);
}
// exe 0x495720: thrown / dropped -- tumbles; a hit while flying kills; lands -> the main state, sub-state 8
void flying(const PlayerEnv& env, AnimatedActor& a, SoldierFields& f) {
    const AnimatedActorStats row = statsOf(a);
    if (!(f.hitBox.result & kHitAttackSolid)) {
        if (a.actorHit != 0) birdbotContactHits(a, f);
    } else {
        a.rotation.y = int16_t(uint16_t(a.rotation.y) + uint16_t(kHalfTurn));
        if (a.hitPoints >= 0) {
            spawnHitSpark(kSparkLoops, a.pos.x.whole, a.pos.y.whole, a.pos.z.whole);
            a.hitPoints = -1;
            soundPlayAtPos(kSoundHitFlying, a.pos);
            f.bits |= kSoldierNoHitEntry;
        }
    }
    a.rotation.x = int16_t(uint16_t(a.rotation.x) + 0x100u);
    if (birdbotArc(a, f, a.rotation.y) != 0) {
        if (a.step == 0 && !(f.contact & kBirdbotOnPlatform)) {
            integrateVelocity(a, 0, a.velocity.y, a.velocity.z);
            a.velocity.y = int16_t(uint16_t(a.velocity.y) + uint16_t(a.acceleration.y));
            a.airborne = 1;
            if (a.hitPoints >= 0 && (f.bits & kSoldierThrown)) {
                if (hitPointsAdd(a, -int(row.bodyAttack >> 1)) != 0)
                    spawnHitSpark(kSparkLoops, a.pos.x.whole, a.pos.y.whole, a.pos.z.whole);
            }
            a.enterStep(1);
            birdbotCollide(env, a, f, f.groundHeight);
            return;
        }
        a.airborne = 0;
        a.enterState(uint8_t(SoldierState::Main), uint8_t(SoldierSub::Overturn));
    }
    birdbotCollide(env, a, f, f.groundHeight);
}
void init(AnimatedActor& a, SoldierFields& f) { // exe 0x493e30
    if (!bindActorModel(a, 0)) {
        freeActor(a);
        return;
    }
    a.flags |= kActorDrawn;
    a.anim = 0;
    a.prevAnim = 0xff;
    const uint16_t hp = hasAnimatedStats(a.type) ? statsOf(a).hitPoints : 0;
    a.maxHitPoints = int16_t(hp);
    a.hitPoints = int16_t(hp);
    a.velocity.setXY(0, 0);
    a.velocity.z = 0;
    a.spin.setXY(0, 0);
    a.rotation.z = 0;
    a.rotation.x = 0;
    a.collisionBox.pointAt(soldierBox.stateAddress());
    a.interactionBox.pointAt(soldierLiftBox.stateAddress());
    a.interaction = kInteractLift;
    a.airborne = 0;
    a.wasOnPlatform = 0;
    a.onPlatform = 0;
    a.actorHit = 0;
    a.wallHit = 0;
    a.clearClassBlock();
    f.group = int8_t(enemyGroupFindFor(a, 1)); // exe 0x5e6cd0
    f.sideOffset = 0x40;
    a.enterState(uint8_t(SoldierState::Main));
    // the body part set: the parts' hide bits
    auto show = [&](size_t part, bool hidden) {
        auto&& p = a.parts[part];
        p.flags = hidden ? uint8_t(p.flags | kPartHidden) : uint8_t(p.flags & ~kPartHidden);
    };
    auto partSet = [&](bool h2, bool h6, bool h7, bool h8) {
        show(2, h2);
        show(6, h6);
        show(7, h7);
        show(8, h8);
    };
    switch (a.variant & 0xf) {
    case 0:
        f.parts = 2;
        partSet(true, false, true, true);
        registerEnemy(a.slot, 0); // exe 0x5e5c10
        break;
    case 1:
        f.parts = 1;
        partSet(false, true, true, false);
        a.subState = uint8_t(SoldierSub::DropIn);
        break;
    case 2:
        a.anim = 0xd;
        f.parts = 1;
        partSet(false, true, true, false);
        a.subState = uint8_t(SoldierSub::JumpIn);
        break;
    case 3:
        a.anim = 0x19;
        f.parts = 1;
        partSet(false, true, true, false);
        f.bits |= kSoldierStays;
        a.subState = uint8_t(SoldierSub::WaitLeap);
        break;
    case 4: // the ST17 boat's crew: on the boat's gangway
        f.parts = 2;
        partSet(true, false, true, true);
        f.bits |= kSoldierStays;
        a.subState = uint8_t(SoldierSub::Gangway);
        registerEnemy(a.slot, 0); // exe 0x5e5c10
        f.boatYaw = a.rotation.y;
        onBoat(a, f, 0, a.rotation.y); // exe 0x4952c0
        break;
    default: break;
    }
    show(1, false);
    show(9, true);
    show(10, true);
    show(11, true);
}
// the part set's hide bits (the parts 2 / 6 / 7 / 8) by the set shown
void showPartSet(AnimatedActor& a, uint8_t set) {
    auto hide = [&](size_t part, bool hidden) {
        auto&& p = a.parts[part];
        p.flags = hidden ? uint8_t(p.flags | kPartHidden) : uint8_t(p.flags & ~kPartHidden);
    };
    switch (set) {
    case 0:
        hide(2, false);
        hide(6, true);
        hide(7, true);
        hide(8, true);
        break;
    case 1:
        hide(2, false);
        hide(6, true);
        hide(7, true);
        hide(8, false);
        break;
    case 2:
        hide(6, false);
        hide(2, true);
        hide(7, true);
        hide(8, true);
        break;
    default:
        hide(2, true);
        hide(6, true);
        hide(7, false);
        hide(8, true);
        break;
    }
}

void soldier(const PlayerEnv& env, AnimatedActor& a) { // exe 0x493a60
    SoldierFields& f = classState<SoldierFields>(a);
    a.prevAnim = a.anim;
    a.savePreviousPose();
    a.wasAirborne = a.airborne;
    a.tint = a.hitPoints < 0 ? kTintSpent : kTintNeutral;
    const uint8_t part0 = f.parts;
    switch (SoldierState(a.state)) { // [0x892c0c]
    case SoldierState::Init: init(a, f); break;
    case SoldierState::Main: mainState(env, a, f); break;
    case SoldierState::Carried: // exe 0x495580: [0x892c68]
        if (a.subState == uint8_t(SoldierCarriedSub::InHands)) inHands(env, a, f);
        else if (a.subState == uint8_t(SoldierCarriedSub::Flying)) flying(env, a, f);
        else notPortedAt(kSoldierPort, "class 0x493a60 state 2 sub-state", a.subState);
        break;
    case SoldierState::Dead: dead(a, f); break;
    case SoldierState::Freed: freeActor(a); break; // exe 0x468fa0
    default: notPortedAt(kSoldierPort, "class 0x493a60 state", a.state);
    }
    a.wasOnPlatform = a.onPlatform;
    if (!(a.flags & kActorLive)) return;
    const uint8_t b = f.bits;
    if ((gameState().frame & 1) && f.hurt != 0) --f.hurt;
    if (f.chase != 0) --f.chase;
    const Player& p = player();
    const uint16_t depthRange = stage().depthRange;
    if (!(b & kSoldierStays)) {
        if ((f.contact & kBirdbotInWater) &&
            (distance3(a.pos, p.pos) > int(uint32_t(depthRange) << 2) || pointVisibleOrNear(a.pos, 0) != 0)) {
            freeActor(a);
            return;
        }
        if (int(a.pos.y.whole) > int(uint32_t(depthRange) << 1)) {
            freeActor(a);
            return;
        }
    }
    const int pd = planarDistance(a.pos, p.pos); // exe 0x5e3b00
    uint32_t high = 0;
    if (a.airborne != 0 && int(a.pos.y.whole) - int(p.pos.y.whole) > (int(fixedSin(0x80)) * pd >> 12))
        high = kHitNoLockOn;
    const AnimatedActorStats row = statsOf(a);
    const uint8_t part = f.parts;
    if (part != part0 && part <= 3) showPartSet(a, part);
    if (a.anim == a.prevAnim) {
        animStepPose(a); // exe 0x5e1e80
    } else {
        animSetPose(a, a.anim, f.animKeyStart); // exe 0x5e1c30
        f.animKeyStart = 0;
    }
    f.hitBox.result = 0;
    f.hitBox.previousX = a.prevPos.x.whole;
    f.hitBox.previousZ = a.prevPos.z.whole;
    if (!(b & kSoldierNoHitEntry)) {
        int16_t pos[3] = {a.pos.x.whole, wrap16(a.pos.y.whole - 0x40), a.pos.z.whole};
        if (!(b & kSoldierThrown)) {
            hitListSubmit(pos, f.hitBox, ((uint32_t(a.slot) << 15 | row.hitHeight) << 9) | high | 0x50400cu,
                          uint32_t(row.attack) | kHitAttackSolid);
            if (b & kSoldierButt) {
                const int16_t in[3] = {0, 0x28, -0x30};
                bonePointOffset(a, 2, in, pos);
                hitListSubmit(pos, f.hitBox, hitOwner(a.slot) | 0x60000au,
                              uint32_t(row.shotAttack) | kHitAttackStrong); // (+3: the butt's damage)
            }
        } else {
            hitListSubmit(pos, f.hitBox, hitOwner(a.slot) | 0x20400cu,
                          uint32_t(row.bodyAttack) | kHitAttackThrown | kHitAttackByPlayer);
        }
    }
    // exe 0x5f20f0(a, 0x30, 0x100, 0) unless state 2 sub 0: the shadow list entry is render output; its ground probe
    // (not on a platform, the list 0x6d0070 not full) leaves the collision scratch
    if (!(a.state == uint8_t(SoldierState::Carried) && a.subState == 0) && (a.flags & kActorDrawn))
        shadowGroundProbe(env, a);
    if (!(b & kSoldierNoMover)) actorCollisionSubmitMover(a); // exe 0x4140b0
}

} // namespace

void birdbotSoldierUpdate(const PlayerEnv& env, uint32_t actor) { soldier(env, animatedActorAt(actor)); }

} // namespace od::port
