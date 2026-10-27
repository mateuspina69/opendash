// The Birdbot tank outside Glyde's base (ST1F, pool 0x20 type 0x40 sub 0; see st1f_birdbot_tank.hpp), transcribed from
// dash2.exe one function per exe function. Shadows (exe 0x5f20f0) are render output and left out.
//
// ---- exe 0x4bd080 (type 0x40 sub 0; ST1F) ------------------------------------------------------------------------
// The update: the previous animation, the tint neutral, the previous pose, the fire request cleared, states [0x894a5c]
// (0 bind 0x4bd2b0, 1 main 0x4bd3a0, 2 dying 0x4bdd80, 3 0x4dd9b0 riders detached + freed); then (live) the animation
// (exe 0x5e1c50 key 0 / 0x5e1ea0), the turret's yaw added to part 1's decoded yaw, a recoil (part 0's pitch - 0x10),
// the pose; the target (exe 0x4bec90); the fire request -> a shot (exe 0x4bea30; spread / jitter by on-screen exe
// 0x5e59a0); the hit entry at y - 0x80 (kTankNoHitEntry: none, kTankHitStrong -> 0x80000; dead 0x10000) and a
// stand-on solid (its stat row's attack | 0x20000).
// Bind: model, hit points from its stat row << 2, box 0x894a40, its home cell, all parts unscaled (exe 0x5e3220),
// registered in the neighbour list (exe 0x5e5c10(slot, 2)).
// Main: the player on it -> Ridden (unless kTankNotRidable); exe 0x4699a0 hurt (1 -> Flinch, 2 -> state 2) unless
// kTankNoHurt; a neighbour (exe 0x5e5e00 k 2) -> a sidestep; subs [0x894a6c] (0 0x4bd4f0 reset, 1 0x4bd5f0 stand and
// face the target (re-aim [0x894a00] / [0x894a10]), 2 0x4bd750 approach, 3 0x4bd7f0 keep the distance round the centre
// (0x2000, 0x1e00) / head for a quadrant cell [0x894a8c], 4 0x4bd9d0 back to the cell, 5 0x4bda20 turned back
// (territory / wall), 6 0x4bdb20 flinch, 7 0x4bdbe0 ridden); then the territory (0x4be620), the gait (0x4be670), the
// head (0x4be800), the ground and slope (0x4bdf60: probes 0x4be000 / 0x4be1c0 / 0x4be390), the tilt (0x4bdeb0); walls
// / territory -> TurnBack; below a quarter of its hit points, smoke (static stage type 3) at random.
//
// Deviations: the low halves of the 16.16 target points of 0x4bd7f0 / 0x4be4b0 (stack locals the exe leaves
// uninitialised) are taken as 0 (0x4be800's are modelled, see tankHead); the probe height of 0x4be390 (an uninitialised stack word passed to exe
// 0x40f1c0) is the actor's own height.
// notPorted where the exe would read through a null pointer (a missing bone point) or index past a dispatch table.
#include "content/calbania/st1f_birdbot_tank.hpp"
#include "content/calbania/st1f_birdbot_tank_state.hpp"

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
#include "game/distances.hpp"
#include "game/enemy_motion.hpp"
#include "game/enemy_neighbours.hpp"
#include "game/enemy_shot.hpp"
#include "game/explosions.hpp"
#include "game/field_alias.hpp"
#include "game/game_state.hpp"
#include "game/hit_list.hpp"
#include "game/hit_points.hpp"
#include "game/pickups.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/room_grid.hpp"
#include "game/script_state.hpp"
#include "game/step_dust.hpp"
#include "game/story_flags.hpp"
#include "game/territory.hpp"
#include "game/visibility.hpp"

namespace od::port {

using namespace st1f_birdbot_tank_state;

namespace {

using address_forms::E;

inline constexpr const char* kEnemyPort = "st1f birdbot tank port"; // the area of its port-gap errors

// mode bits (+0x163)
constexpr uint8_t kTankNoTurnBack = 0x01;   // walls / the territory do not turn it back
constexpr uint8_t kTankRecoil = 0x02;       // the shot's recoil this tick
constexpr uint8_t kTankFrontProbes = 0x04;  // the slope by the front / back probes only
constexpr uint8_t kTankNoHurt = 0x08;
constexpr uint8_t kTankNoTerritory = 0x10;
constexpr uint8_t kTankHitStrong = 0x20;    // its hit entry hits strong (0x80000)
constexpr uint8_t kTankNotRidable = 0x40;
constexpr uint8_t kTankNoHitEntry = 0x80;
constexpr uint8_t kStandMode = kTankNoTurnBack, kChaseMode = 0;
constexpr uint8_t kFlinchMode = kTankNoHurt | kTankNotRidable;
constexpr uint8_t kRiddenMode = kTankNoTurnBack | kTankNoTerritory | kTankNotRidable;
// contact bits (+0x162)
constexpr uint8_t kTankWall = 0x01, kTankOffFloor = 0x02, kTankTerritory = 0x04;
// the gait (+0x15a) and its bits (+0x15b)
enum class TankGait : uint8_t { Level = 1, Settled = 2, Uphill = 3, Downhill = 4 };
constexpr uint8_t kGaitHold = 0x01;          // the speed held (no gait speed)
constexpr uint8_t kGaitFromUphill = 0x02, kGaitFromDownhill = 0x04;
constexpr uint8_t kGaitUphillSpeed = 0x08, kGaitDownhillSpeed = 0x10, kGaitLevelSpeed = 0x20;
constexpr uint8_t kGaitKeepMask = 0x3e;      // the gait bits a new sub-state keeps (the hold cleared)
constexpr uint8_t kGaitSlopeMask = 0xc1;     // ... a new slope keeps
enum class FireCycle : uint8_t { Off = 0, Start = 1, Fire = 2, Pause = 3 };

enum class TankState : uint8_t { Bind = 0, Main = 1, Dying = 2, Free = 3 };
enum class TankSub : uint8_t { Reset = 0, Stand = 1, Approach = 2, Circle = 3, Return = 4, TurnBack = 5, Flinch = 6,
                               Ridden = 7 };
enum class StandStep : uint8_t { Enter = 0, Aim = 1, Turn = 2 };
enum class RiddenStep : uint8_t { Enter = 0, Slow = 1, Stopped = 2 };
enum class DyingStep : uint8_t { Explode = 0, Burn = 1 };

// the random tables of its timings (exe data)
const DataTable<ScalarTable<uint8_t, 8>> pauseTicksTable{TableId::St1fBirdbotTankPauseTicksTable};
const DataTable<ScalarTable<uint8_t, 8>> burstTicksTable{TableId::St1fBirdbotTankBurstTicksTable};
const DataTable<ScalarTable<uint8_t, 16>> aimTicksTable{TableId::St1fBirdbotTankAimTicksTable};
const DataTable<ScalarTable<uint8_t, 16>> turnRateTable{TableId::St1fBirdbotTankTurnRateTable};
// the quadrant cells it heads for (exe table 0x894a8c: {x, z} by quadrant)
struct QuadrantCell : DataRecord {
    uint8_t x = 0;
    uint8_t z = 0;
    static QuadrantCell load(const TableReader& r) {
        QuadrantCell rec;
        rec.x = r.u8(0);
        rec.z = r.u8(1);
        return rec;
    }
};
const DataTable<RecordTable<QuadrantCell, 4, 2>> quadrantCells{TableId::St1fBirdbotTankQuadrantCells};
constexpr int8_t kArenaCellX = 0x20, kArenaCellZ = 0x1e;
constexpr int32_t kArenaCentre[3] = {0x20000000, 0, 0x1e000000}; // the arena's centre (0x2000, -, 0x1e00)
constexpr int16_t kTankAccel = int16_t(0xfff8);
constexpr uint16_t kStepSound = 0xac, kHurtSound = 0x8d, kHurtHardSound = 0x8e;
constexpr uint8_t kShotVariant = 6;
constexpr int16_t kShotTicks = 0x2d;
constexpr uint8_t kExplosionType = 8, kKnockedDownBirdbotType = 0x2d, kSmokeType = 3;
constexpr uint32_t kFlagAppoCarried = 0x581, kFlagDahCarried = 0x582;
constexpr uint32_t kHitByPlayer = 0x40000, kHitStrong = 0x400000, kHitDamageMask = 0xfff;
constexpr int kNoTarget = 0x7fff;
constexpr uint8_t kTankDyingTicks = 0x1e;

inline int16_t t16(int64_t v) { return int16_t(uint16_t(uint64_t(v))); }
inline uint8_t randomOf(const DataTable<ScalarTable<uint8_t, 8>>& t) { return t[rngStep() & 7]; }
inline uint8_t randomOf(const DataTable<ScalarTable<uint8_t, 16>>& t) { return t[rngStep() & 0xf]; }
inline int ground(const AnimatedActor& actor) { return actorGround(*E, actor); } // exe 0x40f190
// the sub-state s at its first step
inline void enterSub(AnimatedActor& actor, TankSub s) {
    actor.subState = uint8_t(s);
    actor.enterStep(0);
}

// the actor of a pair's slot (the exe's unchecked slot arithmetic)
uint32_t pairActorAddress(int8_t slot) { return address_forms::animatedSlotAddress(uint32_t(int32_t(slot))); }
// exe 0x4bec90(actor, &target): the nearest of the player (unless a script runs) and the actors of the pair's slots
// (unless flags 0x581 / 0x582 or their hit points are out); the planar distance (kNoTarget: none)
int tankTarget(AnimatedActor& actor, BirdbotTankFields& f, uint32_t& target) {
    int d = kNoTarget;
    target = kPlayer;
    if (!scriptState().inUse()) d = planarDistance(actor.pos, player().pos);
    int de = d, df = d;
    if (!storyFlag(kFlagAppoCarried) && f.appoSlot != 0) {
        const Actor& o = actorAt(pairActorAddress(f.appoSlot));
        de = planarDistance(actor.pos, o.pos);
        if (o.hitPoints <= -1) de = d;
    }
    if (!storyFlag(kFlagDahCarried) && f.dahSlot != 0) {
        const Actor& o = actorAt(pairActorAddress(f.dahSlot));
        df = planarDistance(actor.pos, o.pos);
        if (o.hitPoints < 0) df = d;
    }
    if (de < d) {
        target = pairActorAddress(f.appoSlot);
        d = de;
    }
    if (df < d) {
        target = pairActorAddress(f.dahSlot);
        return df;
    }
    return d;
}
// exe 0x4bfda0(actor, speed, turn): the speed (>= 0: toward it, the velocity -speed when reached), turned toward the
// steer yaw, moved; -1 = facing it (within turn)
int tankSteer(AnimatedActor& actor, BirdbotTankFields& f, int speed, int turn) {
    if (speed >= 0 && speedToward(actor, speed)) actor.velocity.z = int16_t(-speed);
    const int d = turnYawTo(actor.rotation.y, uint32_t(int32_t(f.steerYaw)), turn);
    integrateVelocity(actor, actor.velocity.x, actor.velocity.y, actor.velocity.z);
    return (uint32_t(turn * 2) < uint32_t(d + turn)) ? 0 : -1;
}
// exe 0x4be4b0(actor, speed, turn): steered to its home cell's centre (* 0x100 + 0x80); -1 = arrived
int tankToCell(AnimatedActor& actor, BirdbotTankFields& f, int speed, int turn) {
    const int16_t cx = t16(int(f.homeCellX) * 0x100 + 0x80), cz = t16(int(f.homeCellZ) * 0x100 + 0x80);
    const int32_t px = int32_t(uint32_t(uint16_t(cx)) << 16), pz = int32_t(uint32_t(uint16_t(cz)) << 16);
    f.steerYaw = int16_t(angleOf(px, pz, actor.pos.x, actor.pos.z));
    if (speed >= 0) f.gaitBits = uint8_t((f.gaitBits & kGaitKeepMask) | kGaitHold);
    else f.gaitBits &= kGaitKeepMask;
    tankSteer(actor, f, speed, turn);
    if (uint32_t(int(actor.pos.x.whole) - cx + 0x100) <= 0x1ffu && uint32_t(int(actor.pos.z.whole) - cz + 0x100) <= 0x1ffu)
        return -1;
    return 0;
}
// exe 0x4bdd10(actor, target): its home cell = the quadrant cell of the target (x <= 0x2000 | z <= 0x1e00 * 2)
void tankPickCell(BirdbotTankFields& f, const Actor& target) {
    const uint32_t k = (target.pos.x.whole <= 0x2000 ? 1u : 0u) | (target.pos.z.whole <= 0x1e00 ? 2u : 0u);
    const QuadrantCell cell = quadrantCells[k];
    f.homeCellX = int8_t(cell.x);
    f.homeCellZ = int8_t(cell.z);
}
// exe 0x4bec50(actor, box): a step sound every 0x15 ticks while moving
void tankSteps(AnimatedActor& actor, BirdbotTankFields& f) {
    if (actor.velocity.z == 0) {
        f.stepTicks = 0;
        return;
    }
    if (f.stepTicks == 0) {
        soundPlayAtPos(kStepSound, actor.pos);
        f.stepTicks = 0x14;
        return;
    }
    --f.stepTicks;
}
// exe 0x4be8c0: four tread dusts (exe 0x46a130) 0xc0 around the feet (yaw -0x200 / -0x400 / +0x200 / +0x400)
void tankFeetDust(AnimatedActor& actor) {
    for (int off : {-0x200, -0x400, 0x200, 0x400}) {
        const uint32_t i = ((uint32_t(uint16_t(actor.rotation.y)) + uint32_t(off)) >> 6) & 0x3f;
        spawnTreadDust(t16((int32_t(sin64(i)) * -0xc0 >> 12) + actor.pos.x.whole), actor.pos.y.whole,
                 t16((int32_t(cos64(i)) * -0xc0 >> 12) + actor.pos.z.whole));
    }
}
// exe 0x4bdcb0(actor, distance): within 5000 and the turret within 0x200 of the target -> the fire cycle starts
void tankSpot(AnimatedActor& actor, BirdbotTankFields& f, int d) {
    if (d > 4999) return;
    uint32_t t;
    tankTarget(actor, f, t);
    const int ang = angleOf(actorAt(t).pos, actor.pos);
    if (uint32_t(angleDiff(uint32_t(f.headYaw + actor.rotation.y), uint32_t(ang)) + 0x200) <= 0x400u &&
        FireCycle(f.fireCycle) == FireCycle::Off)
        f.fireCycle = uint8_t(FireCycle::Start);
}
// exe 0x4be570: the firing cycle (Start -> Fire: a shot every 4th tick of its ticks, Pause)
void tankFireCycle(BirdbotTankFields& f) {
    const FireCycle s = FireCycle(f.fireCycle);
    if (s == FireCycle::Start) {
        f.fireCycle = uint8_t(FireCycle::Fire);
        f.fireTicks = randomOf(burstTicksTable);
    } else if (s == FireCycle::Pause) {
        const uint8_t n = uint8_t(f.fireTicks - 1u);
        f.fireTicks = n;
        if (n == 0) f.fireCycle = uint8_t(FireCycle::Start);
        return;
    } else if (s != FireCycle::Fire) {
        return;
    }
    if (!(f.fireTicks & 3)) f.fireRequest = 1;
    const uint8_t n = uint8_t(f.fireTicks - 1u);
    f.fireTicks = n;
    if (n != 0) return;
    const uint8_t v = randomOf(pauseTicksTable);
    f.fireCycle = uint8_t(FireCycle::Pause);
    f.fireTicks = v;
}
// exe 0x4bd920: the chase step (spot / fire cycle, give up past 0x1450, dust at speed, steps)
void tankChaseStep(AnimatedActor& actor, BirdbotTankFields& f) {
    uint32_t t;
    tankTarget(actor, f, t);
    const int d = planarDistance(actorAt(t).pos, actor.pos);
    tankSpot(actor, f, d);
    if (d < 5000 || f.fireCycle != uint8_t(FireCycle::Off)) tankFireCycle(f);
    if (d >= 0x1451) {
        f.fireTicks = randomOf(pauseTicksTable);
        enterSub(actor, TankSub::Stand);
    }
    if (!(gameState().frame & 3) && actor.velocity.z <= -0xe0) tankFeetDust(actor);
    tankSteps(actor, f);
}
// exe 0x4be390(actor, s16 point, mode): the grid cell of the point (+0x100 bias): {corner x, height, corner z, unset};
// a floor cell (no volumes) -> its height; else (mode) the probe exe 0x40f1c0, or the height unset; kTankOffFloor
struct Probe {
    int16_t x, y, z, unset;
};
Probe tankProbe(AnimatedActor& actor, BirdbotTankFields& f, int16_t px, int16_t pz, int mode) {
    Probe r{0, 0, 0, 0};
    f.contact &= uint8_t(~kTankOffFloor);
    const int32_t cz = (int32_t(pz) + 0x8100) >> 9, cx = (int32_t(px) + 0x8100) >> 9;
    const GridCell cell(gridCellAtIndex(uint32_t(cx), uint32_t(cz)));
    r.x = t16((cx - 0x40) * 0x200);
    r.z = t16((cz - 0x40) * 0x200);
    if ((cell.head ^ kCellSegment) & (kCellFloor | kCellSegment)) {
        r.y = t16(-int32_t((cell.corners >> 12) & 0xff0));
        return r;
    }
    if (mode != 0) {
        // the probe point's height is an uninitialised stack word in the exe: the actor's height is used
        const int16_t p[3] = {r.x, actor.pos.y.whole, r.z};
        r.y = t16(probeGroundAt(*E, p, actor.collisionBox, int(actor.pos.y.whole) - int(actor.prevPos.y.whole),
                                2));
    } else {
        r.unset = 1;
    }
    f.contact |= kTankOffFloor;
    return r;
}
// exe 0x4be000 / 0x4be1c0(actor, mode): the slope angle between two probes 0xc0 from the actor (ahead / behind, or
// right / left): atan2(height difference, distance) (exe 0x40b960), an unset height replaced by the actor's
int16_t tankSlope(AnimatedActor& actor, BirdbotTankFields& f, int mode, int angA, int angB) {
    auto at = [&](int ang, Probe& r) {
        const uint32_t i = (uint32_t(int32_t(actor.rotation.y) + ang) >> 6) & 0x3f;
        const int16_t px = t16((int32_t(sin64(i)) * -0xc0 >> 12) + actor.pos.x.whole);
        const int16_t pz = t16((int32_t(cos64(i)) * -0xc0 >> 12) + actor.pos.z.whole);
        r = tankProbe(actor, f, px, pz, mode);
        if ((mode ^ 1) & r.unset) r.y = actor.pos.y.whole;
    };
    Probe r1, r2;
    at(angA, r1);
    at(angB, r2);
    const int dx = int(r1.x) - int(r2.x), dz = int(r1.z) - int(r2.z);
    const int d = isqrt(int32_t(uint32_t(dz * dz) + uint32_t(dx * dx)));
    int v = 0;
    if (r2.y < r1.y) v = atanAngle(float(int(r1.y) - int(r2.y)), float(d));
    if (r1.y < r2.y) v = -atanAngle(float(int(r2.y) - int(r1.y)), float(d));
    return int16_t(uint16_t(uint32_t(v)));
}
// exe 0x4bdf60: the walls (kTankWall), the height = the ground + 0x10, the slope's pitch / roll
void tankGround(AnimatedActor& actor, BirdbotTankFields& f) {
    f.contact &= uint8_t(~kTankWall);
    if (int32_t(actorTestMove(*E, actor)) < 0) {
        const uint32_t p = actorPushOut(*E, actor);
        if (int32_t(p) < 0) actor.pos = actor.prevPos;
        if (p & 0xf00) f.contact |= kTankWall;
    }
    actor.pos.y.whole = int16_t(ground(actor) + 0x10);
    if (!(f.mode & kTankFrontProbes)) {
        f.slopePitch = tankSlope(actor, f, 0, 0, -0x800);    // exe 0x4be000(actor, 0)
        f.slopeRoll = tankSlope(actor, f, 0, -0x400, 0x400); // exe 0x4be1c0(actor, 0)
        return;
    }
    f.slopePitch = tankSlope(actor, f, 1, 0, -0x800); // exe 0x4be000(actor, 1)
}
// exe 0x4be620: the territory's push back (kTankTerritory) unless kTankNoTerritory
void tankTerritory(AnimatedActor& actor, BirdbotTankFields& f) {
    f.contact &= uint8_t(~kTankTerritory);
    if (f.mode & kTankNoTerritory) return;
    const int z = territoryViolated(actor.pos);
    if (z == 0) return;
    territoryPushBack(actor, z);
    f.contact |= kTankTerritory;
}
// a new slope's gait: the tilt's steps reset
void enterSlopeGait(BirdbotTankFields& f, TankGait g) {
    f.gait = uint8_t(g);
    f.gaitBits &= kGaitSlopeMask;
    f.pitchStep = 4;
    f.pitchGrowth = 2;
}
// exe 0x4be670: the gait by the slope's pitch; true when it asked the speed step (exe 0x5e55a0)
bool tankGait(AnimatedActor& actor, BirdbotTankFields& f) {
    bool asked = false;
    auto level = [&]() {
        if (!(f.gaitBits & kGaitHold)) actor.acceleration.z = kTankAccel;
        if (f.slopePitch <= -0x41) enterSlopeGait(f, TankGait::Uphill);
        if (f.slopePitch <= 0x40) return;
        enterSlopeGait(f, TankGait::Downhill);
    };
    switch (TankGait(f.gait)) {
    case TankGait::Level:
        if (f.gaitBits & kGaitHold) {
            if (f.slopePitch <= -0x41) enterSlopeGait(f, TankGait::Uphill);
            if (f.slopePitch <= 0x40) return asked;
            enterSlopeGait(f, TankGait::Downhill);
            return asked;
        }
        actor.acceleration.z = kTankAccel;
        if (asked = true; speedToward(actor, 0xe0)) {
            actor.velocity.z = int16_t(0xff20);
            f.gaitBits |= kGaitLevelSpeed;
        }
        level();
        return asked;
    case TankGait::Settled: level(); return asked;
    case TankGait::Uphill:
        if (!(f.gaitBits & (kGaitHold | kGaitUphillSpeed))) {
            actor.acceleration.z = kTankAccel;
            if (asked = true; speedToward(actor, 0x120)) {
                actor.velocity.z = int16_t(0xfee0);
                f.gaitBits |= kGaitUphillSpeed;
            }
        }
        if (f.slopePitch <= -1) return asked;
        f.gait = uint8_t(TankGait::Level);
        f.gaitBits |= kGaitFromUphill;
        return asked;
    case TankGait::Downhill:
        if (!(f.gaitBits & (kGaitHold | kGaitDownhillSpeed))) {
            actor.acceleration.z = kTankAccel;
            if (asked = true; speedToward(actor, 0xa0)) {
                actor.velocity.z = int16_t(0xff60);
                f.gaitBits |= kGaitDownhillSpeed;
            }
        }
        if (f.slopePitch >= 1) return asked;
        f.gait = uint8_t(TankGait::Level);
        f.gaitBits |= kGaitFromDownhill;
        return asked;
    default: return asked;
    }
}
// exe 0x4be800: the turret's yaw toward the target's position one tick ahead (+-0x20 a tick). The exe writes only the
// whole halves of its 16.16 aim point (a stack local); the fractions are what the calls before it left there: z the
// actor's address (0x4be670's saved esi), x the actor's address when the gait asked the speed step (its argument to
// 0x5e55a0), else 0 (the territory's 0x5e6ab0 saved ebx, the tank's zero register, exe 0x4bd3a6). With
// kTankNoTerritory the x fraction is the sub-state handler's frame, not modelled: taken as 0
void tankHead(AnimatedActor& actor, BirdbotTankFields& f, bool gaitAsked) {
    uint32_t t;
    tankTarget(actor, f, t);
    const Actor& target = actorAt(t);
    const int32_t px = int32_t(uint32_t(uint16_t(t16(target.pos.x.whole * 0x11 + target.prevPos.x.whole * -0x10))) << 16);
    const int32_t pz = int32_t(uint32_t(uint16_t(t16(target.pos.z.whole * 0x11 + target.prevPos.z.whole * -0x10))) << 16);
    const uint32_t self = actor.at & 0xffff;
    const int ang = angleOf(int32_t(uint32_t(px) | (gaitAsked ? self : 0)), int32_t(uint32_t(pz) | self), actor.pos.x,
                            actor.pos.z);
    const int d = angleDiff(uint32_t(int(f.headYaw) + actor.rotation.y), uint32_t(ang));
    if (uint32_t(d + 0x20) <= 0x40u) {
        f.headYaw = int16_t(angleDiff(uint32_t(int32_t(actor.rotation.y)), uint32_t(ang)));
        return;
    }
    f.headYaw = int16_t(f.headYaw + (d >= 1 ? 0x20 : -0x20));
}
// one axis of the tilt: the angle toward its target by the step (snapped within it), the step growing
void tiltToward(int16_t& angle, const int16_t& target, int16_t& step,
                const int16_t& growth) {
    const int16_t v = step;
    const int d = angleDiff(uint32_t(int32_t(angle)), uint32_t(int32_t(target)));
    if (uint32_t(d + v) <= uint32_t(v * 2)) {
        angle = target;
        return;
    }
    angle = int16_t(angle + (d >= 1 ? v : -v));
    step = int16_t(growth + v);
}
// exe 0x4bdeb0: the pitch / the roll toward the slope's (accelerating steps)
void tankTilt(AnimatedActor& actor, BirdbotTankFields& f) {
    tiltToward(actor.rotation.x, f.slopePitch, f.pitchStep, f.pitchGrowth);
    tiltToward(actor.rotation.z, f.slopeRoll, f.rollStep, f.rollGrowth);
}
// exe 0x4bd520: the motion and the cycles reset
void tankReset(AnimatedActor& actor, BirdbotTankFields& f) {
    actor.anim = 0;
    actor.velocity.x = 0;
    actor.velocity.y = 0;
    actor.velocity.z = 0;
    actor.acceleration.x = 0;
    actor.acceleration.z = kTankAccel;
    f.pitchStep = 4;
    f.pitchGrowth = 2;
    f.rollStep = 8;
    f.rollGrowth = 0;
    f.fireTicks = randomOf(pauseTicksTable);
    const uint8_t v = randomOf(aimTicksTable);
    f.gait = uint8_t(TankGait::Level);
    f.aimTicks = v;
    f.neighbour = uint16_t(rngStep() & 1);
    f.fireCycle = uint8_t(FireCycle::Off);
    f.gaitBits &= kGaitKeepMask;
}
// exe 0x4bd4f0
void tankStartSub(AnimatedActor& actor, BirdbotTankFields& f) {
    tankReset(actor, f);
    f.homeCellX = kArenaCellX;
    f.homeCellZ = kArenaCellZ;
    enterSub(actor, TankSub::Stand);
}
// exe 0x4bd5f0: stands, faces the target (re-aims after its ticks off by 0x300)
void tankStand(AnimatedActor& actor, BirdbotTankFields& f) {
    uint32_t t;
    const int dist = tankTarget(actor, f, t);
    const StandStep s = StandStep(actor.step);
    bool aim = false;
    if (s == StandStep::Enter) {
        actor.anim = 0;
        actor.acceleration.z = kTankAccel;
        f.mode = kStandMode;
        f.gaitBits = uint8_t((f.gaitBits & kGaitKeepMask) | kGaitHold);
        actor.enterStep(uint8_t(StandStep::Aim));
        f.fireCycle = uint8_t(FireCycle::Off);
        aim = true;
    } else if (s == StandStep::Aim) {
        aim = true;
    } else if (s == StandStep::Turn) {
        if (tankSteer(actor, f, 0, f.turnRate)) actor.enterStep(uint8_t(StandStep::Aim));
    }
    if (aim) {
        const int ang = angleOf(actorAt(t).pos, actor.pos);
        if (uint32_t(angleDiff(uint32_t(int32_t(actor.rotation.y)), uint32_t(ang)) + 0x300) >= 0x600u) {
            const uint8_t n = uint8_t(f.aimTicks - 1u);
            f.aimTicks = n;
            if (n == 0) {
                f.steerYaw = int16_t(ang);
                f.turnRate = int8_t(randomOf(turnRateTable));
                actor.enterStep(uint8_t(StandStep::Turn));
            }
        } else {
            f.aimTicks = randomOf(aimTicksTable);
        }
        integrateVelocity(actor, 0, actor.velocity.y, actor.velocity.z);
    }
    if (speedToward(actor, 0)) actor.velocity.z = 0;
    tankSteps(actor, f);
    if (dist >= 6000) return;
    enterSub(actor, TankSub::Approach);
}
// exe 0x4bd750: approaches until within 4000
void tankApproach(AnimatedActor& actor, BirdbotTankFields& f) {
    uint32_t t;
    tankTarget(actor, f, t);
    if (actor.step == 0) {
        f.mode = kChaseMode;
        f.gaitBits &= kGaitKeepMask;
        actor.enterStep(1);
    }
    const Actor& target = actorAt(t);
    const int ang = angleOf(target.pos, actor.pos);
    const int d = planarDistance(target.pos, actor.pos);
    turnYawTo(actor.rotation.y, uint32_t(ang), 0x18);
    integrateVelocity(actor, actor.velocity.x, actor.velocity.y, actor.velocity.z);
    tankChaseStep(actor, f);
    if (d >= 4000) return;
    enterSub(actor, TankSub::Circle);
}
// exe 0x4bd7f0: keeps the distance, circling the arena centre (0x2000, 0x1e00)
void tankCircle(AnimatedActor& actor, BirdbotTankFields& f) {
    uint32_t t;
    tankTarget(actor, f, t);
    if (actor.step == 0) {
        f.mode = kChaseMode;
        f.gaitBits &= kGaitKeepMask;
        actor.enterStep(1);
    }
    const Actor& target = actorAt(t);
    int ang = angleOf(target.pos, actor.pos);
    const int d = planarDistance(target.pos, actor.pos);
    if (d < 0x300) {
        ang = -ang;
    } else if (planarDistance(actor.pos, kArenaCentre) <= d) {
        tankPickCell(f, target);
        tankToCell(actor, f, -1, 0x18);
    } else {
        const int dd = angleDiff(uint32_t(ang),
                                 uint32_t(angleOf(kArenaCentre[0], kArenaCentre[2], actor.pos.x, actor.pos.z)));
        ang += dd <= 0 ? -0x400 : 0x400;
        if (f.circleBack != 0) ang += 0x800;
    }
    turnYawTo(actor.rotation.y, uint32_t(ang), 0x18);
    integrateVelocity(actor, actor.velocity.x, actor.velocity.y, actor.velocity.z);
    tankChaseStep(actor, f);
    if (d <= 6000) return;
    f.homeCellX = kArenaCellX;
    f.homeCellZ = kArenaCellZ;
    enterSub(actor, TankSub::Return);
}
// exe 0x4bd9d0: walks back to its home cell
void tankReturn(AnimatedActor& actor, BirdbotTankFields& f) {
    if (actor.step == 0) {
        f.mode = kStandMode;
        f.gaitBits &= kGaitKeepMask;
        actor.enterStep(1);
    }
    tankChaseStep(actor, f);
    if (!tankToCell(actor, f, -1, 0x18)) return;
    enterSub(actor, TankSub::Stand);
}
// exe 0x4bda20: turned back by the territory / a wall
void tankTurnBack(AnimatedActor& actor, BirdbotTankFields& f) {
    uint32_t t;
    const int dist = tankTarget(actor, f, t);
    if (actor.step == 0) {
        const int d = angleDiff(uint32_t(angleOf(actorAt(t).pos, actor.pos)), uint32_t(int32_t(actor.rotation.y)));
        f.steerYaw = int16_t(actor.rotation.y + (d >= 1 ? -0x400 : 0x400));
        f.gaitBits = uint8_t((f.gaitBits & kGaitKeepMask) | kGaitHold);
        actor.acceleration.z = kTankAccel;
        f.mode = kStandMode;
        actor.enterStep(1);
    }
    if (tankSteer(actor, f, 0xa0, 0x18)) enterSub(actor, TankSub::Approach);
    if (dist <= 5999) enterSub(actor, TankSub::Approach);
    tankSpot(actor, f, dist);
    if (!(gameState().frame & 3) && actor.velocity.z <= -0xe0) tankFeetDust(actor);
    tankSteps(actor, f);
}
// exe 0x4bdb20: flinches
void tankFlinch(AnimatedActor& actor, BirdbotTankFields& f) {
    if (actor.step == 0) {
        f.mode = kFlinchMode;
        f.gaitBits &= kGaitKeepMask;
        f.neighbour ^= uint16_t(rngStep() & 1);
        actor.anim = 1;
        actor.enterStep(1);
        integrateVelocity(actor, 0, 0, actor.velocity.z);
        return;
    }
    if (actor.animEnded()) {
        f.fireTicks = randomOf(pauseTicksTable);
        enterSub(actor, TankSub::Stand);
    }
    integrateVelocity(actor, 0, 0, actor.velocity.z);
}
// exe 0x4bdbe0: the player stands on it -- slows down, waits until it steps off
void tankRidden(AnimatedActor& actor, BirdbotTankFields& f) {
    bool slowing = true;
    if (RiddenStep(actor.step) == RiddenStep::Enter) {
        f.gaitBits = uint8_t((f.gaitBits & kGaitKeepMask) | kGaitHold);
        f.mode = kRiddenMode;
        actor.enterStep(uint8_t(RiddenStep::Slow));
    } else if (RiddenStep(actor.step) != RiddenStep::Slow) {
        slowing = false;
    }
    if (slowing) {
        actor.velocity.z += 8u;
        if (actor.velocity.z >= 0) {
            actor.enterStep(uint8_t(RiddenStep::Stopped));
            actor.velocity.z = 0;
        }
        tankSteps(actor, f);
        integrateVelocity(actor, 0, 0, actor.velocity.z);
    }
    if (!(f.mode & kTankNoTerritory)) {
        const int z = territoryViolated(actor.pos);
        if (z != 0) territoryPushBack(actor, z);
    }
    if (playerRides(actor)) return;
    f.fireTicks = randomOf(pauseTicksTable);
    enterSub(actor, TankSub::Stand);
}
// exe 0x4bea30(actor, target, spread, offscreen): a shot (pool 0x40 stage type 9 variant 6) from bone 1 at one of
// four muzzle offsets (the fire ticks >> 2 & 3), pitched / yawed at the target with RNG jitter
void tankShot(AnimatedActor& actor, BirdbotTankFields& f, uint32_t t, int spread, int off) {
    int xo, yo, a0, v10;
    switch ((f.fireTicks >> 2) & 3) {
    case 0: xo = 5, yo = -0x14, a0 = 0xa, v10 = -0x14; break;
    case 1: xo = -0x14, yo = -5, a0 = -0xa, v10 = -0x14; break;
    case 2: yo = 0x14, xo = -5, a0 = -0xa, v10 = 0x14; break;
    default: xo = 0x14, yo = 5, a0 = 0xa, v10 = 0x14; break;
    }
    const int16_t v[3] = {t16(xo), t16(yo - 0x5a), int16_t(-0xe0)};
    int16_t o[3];
    bonePointOffset(actor, 1, v, o); // exe 0x5e1970
    const uint32_t address = allocActor40(kActorStageClass); // exe 0x5e1280
    if (!address) return;
    ProjectileActor& s = projectileActorAt(address);
    EnemyShotFields shot(projectileActorAt(address));
    s.flags = kActorLive;
    s.type = kEnemyShotType;
    s.variant = kShotVariant;
    s.loopFlags |= kActorSweepGroup2;
    s.pos.x.whole = o[0];
    s.pos.y.whole = o[1];
    s.pos.z.whole = o[2];
    uint32_t r = rngStep();
    const int p = pitchOf(actorAt(t).pos, s.pos); // exe 0x5e3ca0
    if (off) {
        s.rotation.x = int16_t(uint32_t(0x800 - (xo << (r & 1)) - spread - a0 - p));
        r = rngStep();
        s.rotation.y = int16_t(uint32_t(int(f.headYaw) - int(t16(yo << (r & 1))) + actor.rotation.y - v10));
    } else {
        s.rotation.x = int16_t(uint32_t(0x800 - (xo << (r & 1)) - spread - p));
        r = rngStep();
        s.rotation.y = int16_t(uint32_t(int(f.headYaw) - int(int16_t(yo) >> (r & 3)) + actor.rotation.y));
    }
    s.rotation.z = 0;
    shot.ticks = kShotTicks;
    shot.attack = animatedStats(actor.type).shotAttack;
}
// exe 0x4bdd80 -> 0x4bdda0 (sub 0; the others 0x52ef00: nothing): two explosions, then the knocked-down Birdbot and
// the drops
void tankDying(AnimatedActor& actor, BirdbotTankFields& f) {
    if (actor.subState != 0) return;
    if (DyingStep(actor.step) != DyingStep::Explode) {
        if (DyingStep(actor.step) != DyingStep::Burn) return;
    } else {
        actor.enterStep(uint8_t(DyingStep::Burn));
        actor.anim = 1;
        f.turnRate = int8_t(kTankDyingTicks);
        countDefeat();
        for (int v = 1; v >= 0; --v) {
            const uint32_t address = allocActor60(0); // exe 0x5e1300: explosions (static global type 8)
            if (!address) continue;
            StaticActor& e = staticActorAt(address);
            e.flags = kActorLive;
            e.type = kExplosionType;
            e.variant = uint8_t(v);
            e.loopFlags |= kActorSweepGroup2;
            e.pos.x.whole = actor.pos.x.whole;
            e.pos.y.whole = int16_t(actor.pos.y.whole - 0x60);
            e.pos.z.whole = actor.pos.z.whole;
            ExplosionParams explosion(actorHeaderAt(address));
            explosion.spread = 0xc0;
            explosion.flashes = 0x1e;
        }
    }
    if (uint8_t(f.turnRate) & 1) actor.tint = 0x4214;
    const uint8_t n = uint8_t(uint8_t(f.turnRate) - 1u);
    f.turnRate = int8_t(n);
    if (n != 0) return;
    actorDetachRiders(actor); // exe 0x414e70
    if (const uint32_t address = allocActor20()) { // exe 0x5e1160: the knocked-down Birdbot (type 0x2d sub 0, 0x492330)
        AnimatedActor& c = animatedActorAt(address);
        c.flags = kActorLive;
        c.type = kKnockedDownBirdbotType;
        c.subtype = 0;
        c.variant = 0;
        c.loopFlags |= kActorSweepGroup2;
        c.pos.x = actor.pos.x;
        c.pos.z = actor.pos.z;
        c.pos.y.whole = int16_t(actor.pos.y.whole - 200);
    }
    spawnDrops(actor.pos, animatedStats(actor.type).dropTable); // exe 0x5f1270
    actorDetachRiders(actor);
    freeActor(actor);
}
// the parameters of the stage's smoke (static stage type 3; its class reads them)
struct StageSmokeParams {
    explicit StageSmokeParams(const StaticActor& actor) : StageSmokeParams(const_cast<StaticActor&>(actor)) {}
    explicit StageSmokeParams(StaticActor& actor) : style(actor.params[0]), phase(actor.params[1]), puffs(actor.params, 2) {}
    uint8_t& style;
    uint8_t& phase;
    BytesField<uint16_t, 4> puffs;
};
// the part whose bit 7 the bind clears
constexpr size_t kShownPart = 2;
// exe 0x4bd2b0
void tankBind(AnimatedActor& actor, BirdbotTankFields& f) {
    actor.setDrawn(true);
    if (!bindActorModel(actor, 0)) { // exe 0x5e0ba0
        actorDetachRiders(actor);
        freeActor(actor);
        return;
    }
    actor.rotation.x = 0;
    actor.rotation.z = 0;
    actor.anim = 0;
    actor.prevAnim = 0xff;
    const uint16_t hp =
        hasAnimatedStats(actor.type) ? uint16_t(uint32_t(animatedStats(actor.type).hitPoints) << 2) : 0;
    actor.velocity.clearBytes();
    actor.rotation.z = 0;
    actor.rotation.x = 0;
    f.headYaw = 0;
    actor.clearInteractionBytes();
    actor.airborne = 0;
    actor.onPlatform = 0;
    actor.platform = nullptr;
    actor.actorHit = 0;
    actor.maxHitPoints = int16_t(hp);
    actor.hitPoints = int16_t(hp);
    actor.collisionBox.pointAt(TableId::St1fBirdbotTankBox);
    actor.clearClassBlock();
    f.homeCellX = int8_t(uint16_t(actor.pos.x.whole) >> 8);
    f.homeCellZ = int8_t(uint16_t(actor.pos.z.whole) >> 8);
    resetBoneScales(actor);       // exe 0x5e3220
    registerEnemy(actor.slot, 2); // exe 0x5e5c10
    const uint8_t partFlags = actor.parts[kShownPart].flags;
    actor.enterState(uint8_t(TankState::Main));
    actor.parts[kShownPart].flags = uint8_t(partFlags & 0x7fu);
}
// exe 0x4bd3a0
void tankMain(AnimatedActor& actor, BirdbotTankFields& f) {
    if (!(f.mode & kTankNotRidable) && playerRides(actor)) enterSub(actor, TankSub::Ridden);
    if (!(f.mode & kTankNoHurt)) {
        // exe 0x4699a0: a hit by the player -> the flash, sound 0x8e (strong: 1) / 0x8d, the hit points (dead: 2)
        int r = 0;
        const uint32_t v = f.hitBox.result;
        if (v & kHitByPlayer) {
            r = (v & kHitStrong) ? 1 : 0;
            actor.tint = kTintFlash;
            soundPlayAtPos(r ? kHurtHardSound : kHurtSound, actor.pos);
            if (hitPointsAdd(actor, -int(v & kHitDamageMask))) r = 2;
        }
        if (r == 1) {
            enterSub(actor, TankSub::Flinch);
        } else if (r == 2) {
            actor.enterState(uint8_t(TankState::Dying));
            tankDying(actor, f);
            return;
        }
    }
    if (neighbourFind(actor, f.neighbour, 2)) tankSteer(actor, f, 0xa0, 0x18); // exe 0x5e5e00
    switch (TankSub(actor.subState)) {
    case TankSub::Reset: tankStartSub(actor, f); break;
    case TankSub::Stand: tankStand(actor, f); break;
    case TankSub::Approach: tankApproach(actor, f); break;
    case TankSub::Circle: tankCircle(actor, f); break;
    case TankSub::Return: tankReturn(actor, f); break;
    case TankSub::TurnBack: tankTurnBack(actor, f); break;
    case TankSub::Flinch: tankFlinch(actor, f); break;
    case TankSub::Ridden: tankRidden(actor, f); break;
    default: notPortedAt(kEnemyPort, "0x4bd080 state 1 sub-state (table 0x894a6c has 8)", actor.subState);
    }
    tankTerritory(actor, f);
    tankHead(actor, f, tankGait(actor, f));
    tankGround(actor, f);
    tankTilt(actor, f);
    if ((f.contact & (kTankWall | kTankTerritory)) && !(f.mode & kTankNoTurnBack)) enterSub(actor, TankSub::TurnBack);
    if (actor.hitPoints < (actor.maxHitPoints >> 2) && !(gameState().frame & 7) &&
        ((0x2bc4u << (rngStep() & 0xf)) & 0x8000u)) {
        if (const uint32_t address = allocActor60(kActorStageClass)) { // exe 0x5e12f0: smoke (static stage type 3)
            StaticActor& e = staticActorAt(address);
            e.flags = kActorLive;
            e.type = kSmokeType;
            e.variant = 0;
            e.loopFlags |= kActorSweepGroup2;
            e.pos.x = actor.pos.x;
            e.pos.y = int32_t(uint32_t(actor.pos.y) - 0x800000u);
            e.pos.z = actor.pos.z;
            StageSmokeParams smoke(staticActorAt(address));
            smoke.style = 0;
            smoke.phase = 0;
            smoke.puffs = 7;
        }
    }
}
// exe 0x4bd080's body
void birdbotTank(AnimatedActor& actor) {
    BirdbotTankFields& f = classState<BirdbotTankFields>(actor);
    actor.prevAnim = actor.anim;
    actor.tint = kTintNeutral;
    actor.savePreviousPose();
    actor.savePreviousContact();
    f.fireRequest = 0;
    switch (TankState(actor.state)) {
    case TankState::Bind: tankBind(actor, f); break;
    case TankState::Main: tankMain(actor, f); break;
    case TankState::Dying: tankDying(actor, f); break;
    case TankState::Free: // exe 0x4dd9b0
        actorDetachRiders(actor);
        freeActor(actor);
        break;
    default: notPortedAt(kEnemyPort, "0x4bd080 state (table 0x894a5c has 4)", actor.state);
    }
    if (!(actor.flags & kActorLive)) return;
    if (actor.anim != actor.prevAnim) animSet(actor, actor.anim, 0); // exe 0x5e1c50
    else animStep(actor);                                            // exe 0x5e1ea0
    LaneVec4 turret = decodedFrame().entries[2]; // part 1's angles
    turret.y = int16_t(f.headYaw + turret.y);
    if (f.mode & kTankRecoil) {
        decodedFrame().entries[1].x -= 0x10u; // part 0's pitch
        f.mode &= uint8_t(~kTankRecoil);
    }
    animPose(actor); // exe 0x5e2d90
    uint32_t t;
    tankTarget(actor, f, t);
    if (f.fireRequest != 0) {
        const uint32_t r = rngStep();
        const bool visible = pointVisibleOrNear(actor.pos, 0) == 0; // exe 0x5e59a0
        const int spread = visible ? ((int(r & 0xf) - int8_t(f.fireTicks)) >> 2) + 0x10
                                   : (-int(int8_t(f.fireTicks)) >> 2) + 0x18;
        tankShot(actor, f, t, spread, visible ? 0 : 1);
    }
    if (!(actor.flags & kActorDrawn)) return;
    f.hitBox.previousX = actor.prevPos.x.whole;
    f.hitBox.previousZ = actor.prevPos.z.whole;
    f.hitBox.result = 0;
    const AnimatedActorStats row = animatedStats(actor.type);
    const int16_t pos[3] = {actor.pos.x.whole, t16(actor.pos.y.whole - 0x80), actor.pos.z.whole};
    const uint32_t strong = (f.mode & kTankHitStrong) << 0xe;
    if (f.mode & kTankNoHitEntry) return;
    const uint32_t param = ((uint32_t(actor.slot) << 15 | row.hitHeight) << 9) | strong | 0x400030;
    hitListSubmit(pos, f.hitBox, param, actor.hitPoints >= 0 ? 0x20000u : 0x10000u); // exe 0x5e4120
    actorCollisionSubmitSolid(actor, SolidType::StandOn, kNoBone, row.attack | 0x20000u); // exe 0x414050
}

} // namespace

void st1fBirdbotTankUpdate(const PlayerEnv& env, uint32_t actor) {
    E = &env;
    birdbotTank(animatedActorAt(actor));
}

} // namespace od::port
