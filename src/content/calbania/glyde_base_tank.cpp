// Glyde's tanks (ST30 sub 2, pool 0x20 type 0x40 sub 1; see glyde_base_tank.hpp), transcribed from dash2.exe one
// function per exe function. The gate's wave counter (3, set by the ST30 sub 2 room handler exe 0x585890) counts them
// down; the last one sets flag 0x22 and drops the key. Shadows (exe 0x5f20f0) are render output and left out.
//
// The update: the previous animation, the tint neutral, the previous pose, the fire request cleared, states [0x894b04]
// (0 init 0x4beff0, 1 main 0x4bf0e0, 2 destroyed 0x4bfb90, 3 0x4dd9b0 riders detached + freed); then (live) the
// animation (exe 0x5e1c50 key 0 / 0x5e1ea0), the turret's yaw added to part 1's decoded yaw, a recoil (part 0's pitch -
// 0x10), the pose; the fire request -> a shot at the player (exe 0x4bea30; spread / jitter by on-screen exe 0x5e59a0);
// the hit entry at y - 0x80 (kTankNoHitEntry: none, kTankHitStrong -> 0x80000; dead 0x10000) and a stand-on solid.
// Main: the player on it -> Ridden (unless kTankNotRidable); exe 0x4699a0 hurt (1 -> Flinch, 2 -> destroyed) unless
// kTankNoHurt; variant 0 and a neighbour (exe 0x5e5e00 k 2) -> a sidestep; sub-states [0x894b14] (0 0x4bf240 wait, 1
// 0x4bf380 patrol (variant 0 drives; the others turn and fire), 2 0x4bf590 chase, 3 0x4bf630 circle the player round
// the base's centre, 4 0x4bf810 back to the home cell, 5 0x4bf860 turned back (territory / wall), 6 0x4bf960 flinch, 7
// 0x4bfa20 ridden); then the territory (0x4be620), the gait (0x4bff70), the turret (0x4c0100), the walls and the
// ground (0x4bfd40), the tilt (0x4bdeb0); walls / territory -> TurnBack; below a quarter of its hit points, smoke
// (static stage type 3) at random.
//
// Deviation: the low halves of the 16.16 points of 0x4bf630 / 0x4bfe00 / 0x4c0100 (stack locals the exe leaves
// uninitialised) are taken as 0.
#include "content/calbania/glyde_base_tank.hpp"
#include "content/calbania/glyde_base_tank_state.hpp"

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
#include "game/encounter.hpp"
#include "game/enemy_motion.hpp"
#include "game/enemy_neighbours.hpp"
#include "game/enemy_shot.hpp"
#include "game/explosions.hpp"
#include "game/game_state.hpp"
#include "game/global_class_types.hpp"
#include "game/hit_list.hpp"
#include "game/hit_points.hpp"
#include "game/pickups.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/stage_smoke.hpp"
#include "game/step_dust.hpp"
#include "game/story_flags.hpp"
#include "game/territory.hpp"
#include "game/visibility.hpp"

namespace od::port {

using namespace glyde_base_tank_state;

namespace {

constexpr const char* kTankPort = "glyde base tank port"; // the area of its port-gap errors

// mode bits (+0x163)
constexpr uint8_t kTankNoTurnBack = 0x01;  // walls / the territory do not turn it back
constexpr uint8_t kTankRecoil = 0x02;      // the shot's recoil this tick
constexpr uint8_t kTankNoHurt = 0x08;
constexpr uint8_t kTankNoTerritory = 0x10;
constexpr uint8_t kTankHitStrong = 0x20;   // its hit entry hits strong (0x80000)
constexpr uint8_t kTankNotRidable = 0x40;
constexpr uint8_t kTankNoHitEntry = 0x80;
constexpr uint8_t kStandMode = kTankNoTurnBack, kChaseMode = 0;
constexpr uint8_t kFlinchMode = kTankNoHurt | kTankNotRidable;
constexpr uint8_t kRiddenMode = kTankNoTurnBack | kTankNoTerritory | kTankNotRidable;
// contact bits (+0x162)
constexpr uint8_t kTankWall = 0x01, kTankTerritory = 0x04;
// the gait (+0x15a) and its bits (+0x15b)
enum class TankGait : uint8_t { Level = 1, Settled = 2, Uphill = 3, Downhill = 4 };
constexpr uint8_t kGaitHold = 0x01; // the speed held (no gait speed)
constexpr uint8_t kGaitFromUphill = 0x02, kGaitFromDownhill = 0x04;
constexpr uint8_t kGaitUphillSpeed = 0x08, kGaitDownhillSpeed = 0x10, kGaitLevelSpeed = 0x20;
constexpr uint8_t kGaitKeepMask = 0x3e;  // the gait bits a new sub-state keeps (the hold cleared)
constexpr uint8_t kGaitSlopeMask = 0xc1; // ... a new slope keeps
enum class FireCycle : int8_t { Off = 0, Start = 1, Fire = 2, Pause = 3 };

enum class TankState : uint8_t { Init = 0, Main = 1, Destroyed = 2, Free = 3 };
enum class TankSub : uint8_t { Wait = 0, Patrol = 1, Chase = 2, Circle = 3, Return = 4, TurnBack = 5, Flinch = 6,
                               Ridden = 7 };
enum class PatrolStep : uint8_t { Enter = 0, Watch = 1, Turn = 2, Burst = 3, Pause = 4 };
enum class RiddenStep : uint8_t { Enter = 0, Slow = 1, Stopped = 2 };
enum class DestroyedStep : uint8_t { Explode = 0, Burn = 1 };

// the random tables of its timings (exe data)
const DataTable<ScalarTable<uint8_t, 16>> aimTicksTable{TableId::GlydeBaseTankAimTicksTable};
const DataTable<ScalarTable<uint8_t, 16>> turnRateTable{TableId::GlydeBaseTankTurnRateTable};
const DataTable<ScalarTable<uint8_t, 16>> burstLengthTable{TableId::GlydeBaseTankBurstLengthTable};
const DataTable<ScalarTable<uint8_t, 8>> pauseTicksTable{TableId::GlydeBaseTankPauseTicksTable};
const DataTable<ScalarTable<uint8_t, 8>> burstTicksTable{TableId::GlydeBaseTankBurstTicksTable};
// the quadrant cells it heads for (exe table 0x894b34: {x, z} by quadrant)
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
const DataTable<RecordTable<QuadrantCell, 4, 2>> quadrantCells{TableId::GlydeBaseTankQuadrantCells};
constexpr int8_t kBaseCellX = 0x20, kBaseCellZ = 0x1e;
constexpr int32_t kBaseCentre[3] = {0x20000000, 0, 0x1e000000}; // the base's centre (0x2000, -, 0x1e00)
constexpr int16_t kTankAccel = int16_t(0xfff8);
constexpr uint16_t kStepSound = 0xac, kHurtSound = 0x8d, kHurtHardSound = 0x8e;
constexpr uint8_t kShotVariant = 6;
constexpr int16_t kShotTicks = 0x2d;
constexpr uint8_t kKnockedDownBirdbotType = 0x2d, kKeyType = 0x42;
constexpr uint8_t kBirdbotStandsUp = 3; // the knocked-down Birdbot's parameter: the last tank's
constexpr uint8_t kKeyFromTank = 2;     // the key's parameter
constexpr uint32_t kFlagGateBeaten = 0x22;
constexpr uint32_t kHitByPlayer = 0x40000, kHitStrong = 0x400000;
constexpr int kFireRange = 0x1388, kGiveUpRange = 0x1450, kSpotRange = 0x1770, kCircleRange = 0xfa0;
constexpr uint8_t kDestroyedTicks = 0x1e;
constexpr uint32_t kMuzzleBone = 1;
constexpr size_t kTurretAngles = 2, kHullAngles = 1; // the decoded frame's entries of part 1 / part 0
constexpr size_t kShownPart = 2;                      // the part whose bit 7 the init clears
constexpr uint16_t kFlashTint = 0x4214;

inline uint8_t randomOf(const DataTable<ScalarTable<uint8_t, 8>>& t) { return t[rngStep() & 7]; }
inline uint8_t randomOf(const DataTable<ScalarTable<uint8_t, 16>>& t) { return t[rngStep() & 0xf]; }
AnimatedActorStats statsOf(const AnimatedActor& actor) { return animatedStats(actor.type); }
// the sub-state s at its first step
void enterSub(AnimatedActor& actor, TankSub s) {
    actor.subState = uint8_t(s);
    actor.restartStep();
}
// the gait bits a new sub-state keeps, with or without the hold
void keepGait(GlydeTankFields& f, bool hold) {
    f.gaitBits = uint8_t((f.gaitBits & kGaitKeepMask) | (hold ? kGaitHold : 0));
}

// exe 0x4bec50: a step sound every 0x15 ticks while moving
void stepSounds(AnimatedActor& actor, GlydeTankFields& f) {
    if (actor.velocity.z == 0) {
        f.stepTicks = 0;
        return;
    }
    const uint8_t c = f.stepTicks;
    if (c == 0) {
        soundPlayAtPos(kStepSound, actor.pos);
        f.stepTicks = 0x14;
    } else {
        f.stepTicks = uint8_t(c - 1u);
    }
}
// exe 0x4bea30(actor, target, spread, aimed): a shot (pool 0x40 stage type 9 variant 6) from the muzzle at one of four
// offsets (the fire ticks >> 2 & 3), pitched / yawed at the player with RNG jitter
void shoot(AnimatedActor& actor, GlydeTankFields& f, int spread, bool aimed) {
    int xo, yo, pitchOff, yawOff;
    switch ((f.fireTicks >> 2) & 3) {
    case 0: xo = 5; yo = -0x14; pitchOff = 0xa; yawOff = -0x14; break;
    case 1: xo = -0x14; yo = -5; pitchOff = -0xa; yawOff = -0x14; break;
    case 2: yo = 0x14; xo = -5; pitchOff = -0xa; yawOff = 0x14; break;
    default: xo = 0x14; yo = 5; pitchOff = 0xa; yawOff = 0x14; break;
    }
    const int16_t in[3] = {int16_t(xo), int16_t(yo - 0x5a), int16_t(-0xe0)};
    int16_t out[3];
    bonePointOffset(actor, kMuzzleBone, in, out); // exe 0x5e1970
    const uint32_t address = allocActor40(kActorStageClass); // exe 0x5e1280
    if (!address) return;
    ProjectileActor& s = projectileActorAt(address);
    s.flags = kActorLive;
    s.type = kEnemyShotType;
    s.variant = kShotVariant;
    s.loopFlags |= kActorSweepGroup2;
    s.pos.x.whole = out[0];
    s.pos.y.whole = out[1];
    s.pos.z.whole = out[2];
    const Player& p = player();
    uint16_t yaw;
    if (aimed) {
        const uint32_t r = rngStep();
        const int pitch = pitchOf(p.pos, s.pos); // exe 0x5e3ca0
        const int e = int(uint32_t(xo) << (r & 1));
        s.rotation.x = int16_t(kHalfTurn - e - spread - pitchOff - pitch);
        const uint32_t r2 = rngStep();
        const int b = int(uint32_t(yo) << (r2 & 1));
        yaw = uint16_t(uint16_t(f.turretYaw) - uint16_t(b) + uint16_t(actor.rotation.y) - uint16_t(yawOff));
    } else {
        const uint32_t r = rngStep();
        const int pitch = pitchOf(p.pos, s.pos);
        const int e = int(uint32_t(xo) << (r & 1));
        s.rotation.x = int16_t(kHalfTurn - e - spread - pitch);
        const uint32_t r2 = rngStep();
        const int16_t b = int16_t(int16_t(yo) >> (r2 & 3));
        yaw = uint16_t(uint16_t(f.turretYaw) - uint16_t(b) + uint16_t(actor.rotation.y));
    }
    s.rotation.y = int16_t(yaw);
    s.rotation.z = 0;
    EnemyShotFields shot(projectileActorAt(address));
    shot.ticks = kShotTicks;
    shot.attack = statsOf(actor).shotAttack;
}
// exe 0x4bfda0(actor, speed, turn): the speed (>= 0: toward it, the velocity -speed when reached), turned toward the
// steer yaw, moved; -1 = facing it (within turn)
int steer(AnimatedActor& actor, GlydeTankFields& f, int speed, int turn) {
    if (speed >= 0 && speedToward(actor, speed) != 0) actor.velocity.z = int16_t(-speed);
    const int d = turnYawTo(actor.rotation.y, uint32_t(int32_t(f.steerYaw)), turn); // exe 0x5e3df0
    integrateVelocity(actor, actor.velocity.x, actor.velocity.y, actor.velocity.z);
    return uint32_t(turn * 2) < uint32_t(d + turn) ? 0 : -1;
}
// exe 0x4bf2b0: the motion and the cycles reset
void resetMotion(AnimatedActor& actor, GlydeTankFields& f) {
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
    const uint8_t t = randomOf(aimTicksTable);
    f.gait = uint8_t(TankGait::Level);
    f.aimTicks = t;
    f.neighbour = uint16_t(rngStep() & 1);
    f.fireCycle = int8_t(FireCycle::Off);
    f.gaitBits &= kGaitKeepMask;
}
// exe 0x4bfec0: the burst cycle (Start -> Fire: a shot every 4th tick of its ticks -> Pause)
void fireCycleStep(GlydeTankFields& f) {
    switch (FireCycle(f.fireCycle)) {
    case FireCycle::Start:
        f.fireCycle = int8_t(FireCycle::Fire);
        f.fireTicks = randomOf(burstTicksTable);
        [[fallthrough]];
    case FireCycle::Fire: {
        const uint8_t t = f.fireTicks;
        if (!(t & 3)) f.fireRequest = 1;
        f.fireTicks = uint8_t(t - 1u);
        if (uint8_t(t - 1u) != 0) return;
        f.fireTicks = randomOf(pauseTicksTable);
        f.fireCycle = int8_t(FireCycle::Pause);
        return;
    }
    case FireCycle::Pause:
        --f.fireTicks;
        if (f.fireTicks == 0) f.fireCycle = int8_t(FireCycle::Start);
        return;
    default: return;
    }
}
// exe 0x4bfaf0: the player within kFireRange and the turret within 0x200 of him -> a burst starts
void spotPlayer(AnimatedActor& actor, GlydeTankFields& f, int dist) {
    if (dist >= kFireRange) return;
    const int ang = angleOf(player().pos, actor.pos);
    const int d = angleDiff(uint32_t(int32_t(f.turretYaw) + int32_t(actor.rotation.y)), uint32_t(ang));
    if (uint32_t(d + 0x200) <= 0x400u && f.fireCycle == int8_t(FireCycle::Off)) f.fireCycle = int8_t(FireCycle::Start);
}
// exe 0x4c01c0: four tread dusts (exe 0x46a130) 0xc0 around (yaw -0x200 / -0x400 / +0x200 / +0x400)
void treadDust(AnimatedActor& actor) {
    static constexpr int kOff[4] = {-0x200, -0x400, 0x200, 0x400};
    for (const int off : kOff) {
        const uint32_t i = (uint32_t(int(uint16_t(actor.rotation.y)) + off) >> 6) & 0x3f;
        const int c = cos64(i), s = sin64(i);
        const int z = (int32_t(uint32_t(-(c * 3)) << 6) >> 12) + actor.pos.z.whole;
        const int x = (int32_t(uint32_t(-(s * 3)) << 6) >> 12) + actor.pos.x.whole;
        spawnTreadDust(int16_t(x), actor.pos.y.whole, int16_t(z));
    }
}
// exe 0x4bf760: the chase's step (spot / fire cycle, give up past kGiveUpRange, dust at speed, steps)
void chaseStep(AnimatedActor& actor, GlydeTankFields& f) {
    const int dist = planarDistance(player().pos, actor.pos);
    spotPlayer(actor, f, dist);
    if (dist < kFireRange || f.fireCycle != int8_t(FireCycle::Off)) fireCycleStep(f);
    if (dist > kGiveUpRange) {
        f.fireTicks = randomOf(pauseTicksTable);
        enterSub(actor, TankSub::Patrol);
    }
    if (!(gameState().frame & 3) && actor.velocity.z <= -0xe0) treadDust(actor);
    stepSounds(actor, f);
}
// exe 0x4bfe00(actor, speed, turn): steered to its home cell's centre (* 0x100 + 0x80); -1 = arrived
int toHomeCell(AnimatedActor& actor, GlydeTankFields& f, int speed, int turn) {
    const int16_t gx = int16_t(int(f.homeCellX) * 256 + 0x80), gz = int16_t(int(f.homeCellZ) * 256 + 0x80);
    const int32_t px = int32_t(uint32_t(uint16_t(gx)) << 16), pz = int32_t(uint32_t(uint16_t(gz)) << 16);
    f.steerYaw = int16_t(angleOf(px, pz, actor.pos.x, actor.pos.z));
    keepGait(f, speed >= 0);
    steer(actor, f, speed, turn);
    if (uint32_t(int(actor.pos.x.whole) - gx + 0x100) < 0x200u && uint32_t(int(actor.pos.z.whole) - gz + 0x100) < 0x200u)
        return -1;
    return 0;
}
// exe 0x4bfb50(actor, target): its home cell = the quadrant cell of the target (x <= 0x2000 | z > 0x1e00 * 2)
void pickHomeCell(GlydeTankFields& f, const Actor& target) {
    const uint32_t k = (target.pos.x.whole <= 0x2000 ? 1u : 0u) | (target.pos.z.whole <= 0x1e00 ? 0u : 2u);
    const QuadrantCell cell = quadrantCells[k];
    f.homeCellX = int8_t(cell.x);
    f.homeCellZ = int8_t(cell.z);
}

// ---- sub-states ---------------------------------------------------------------------------------------------------
// exe 0x4bf240: waits its aim ticks
void wait(AnimatedActor& actor, GlydeTankFields& f) {
    if (actor.step == 0) {
        actor.step = 1;
        resetMotion(actor, f);
        f.homeCellX = kBaseCellX;
        f.homeCellZ = kBaseCellZ;
        return;
    }
    --f.aimTicks;
    if (f.aimTicks != 0) return;
    f.aimTicks = randomOf(aimTicksTable);
    enterSub(actor, TankSub::Patrol);
}
// exe 0x4bf380: patrols -- watches the player (re-aims after its ticks off by 0x300); variant 0 drives and turns, the
// others turn by firing a burst and pausing; variant 0 spots the player within kSpotRange
void patrol(AnimatedActor& actor, GlydeTankFields& f) {
    const int dist = planarDistance(actor.pos, player().pos); // exe 0x4c0330
    const PatrolStep step = PatrolStep(actor.step);
    bool backToWatch = false;
    switch (step) {
    case PatrolStep::Enter:
        actor.anim = 0;
        actor.acceleration.z = kTankAccel;
        f.mode = kStandMode;
        keepGait(f, true);
        actor.enterStep(uint8_t(PatrolStep::Watch));
        f.fireCycle = int8_t(FireCycle::Off);
        [[fallthrough]];
    case PatrolStep::Watch: {
        const int ang = angleOf(player().pos, actor.pos);
        const int d = angleDiff(uint32_t(int32_t(actor.rotation.y)), uint32_t(ang));
        bool turn = false;
        if (uint32_t(d + 0x300) >= 0x600u) {
            --f.aimTicks;
            if (f.aimTicks == 0) {
                f.steerYaw = int16_t(ang);
                f.turnRate = int8_t(randomOf(turnRateTable));
                turn = true;
            }
        } else {
            f.aimTicks = randomOf(aimTicksTable);
            if (actor.variant != 0) turn = true;
        }
        if (turn) actor.enterStep(uint8_t(PatrolStep::Turn));
        if (actor.variant == 0) integrateVelocity(actor, 0, actor.velocity.y, actor.velocity.z);
        break;
    }
    case PatrolStep::Turn:
        if (actor.variant == 0) {
            if (steer(actor, f, 0, f.turnRate) != 0) backToWatch = true;
        } else {
            actor.enterStep(uint8_t(PatrolStep::Burst));
            const uint32_t r = rngStep();
            f.fireCycle = int8_t(FireCycle::Start);
            f.phaseTicks = burstLengthTable[r & 0xf];
        }
        break;
    case PatrolStep::Burst:
        --f.phaseTicks;
        if (f.phaseTicks == 0) {
            actor.enterStep(uint8_t(PatrolStep::Pause));
            const uint32_t r = rngStep();
            f.fireCycle = int8_t(FireCycle::Off);
            f.phaseTicks = uint8_t((r & 0x1f) + 0x1e);
        }
        fireCycleStep(f);
        break;
    case PatrolStep::Pause:
        --f.phaseTicks;
        if (f.phaseTicks == 0) backToWatch = true;
        break;
    default: break;
    }
    if (backToWatch) actor.enterStep(uint8_t(PatrolStep::Watch));
    if (actor.variant != 0) return;
    if (speedToward(actor, 0) != 0) actor.velocity.z = 0;
    stepSounds(actor, f);
    if (dist < kSpotRange) enterSub(actor, TankSub::Chase);
}
// exe 0x4bf590: chases the player until within kCircleRange
void chase(AnimatedActor& actor, GlydeTankFields& f) {
    if (actor.step == 0) {
        f.mode = kChaseMode;
        keepGait(f, false);
        actor.enterStep(1);
    }
    const Player& p = player();
    const int ang = angleOf(p.pos, actor.pos);
    const int dist = planarDistance(p.pos, actor.pos);
    turnYawTo(actor.rotation.y, uint32_t(ang), 0x18);
    integrateVelocity(actor, actor.velocity.x, actor.velocity.y, actor.velocity.z);
    chaseStep(actor, f);
    if (dist < kCircleRange) enterSub(actor, TankSub::Circle);
}
// exe 0x4bf630: circles the player round the base's centre (0x2000, 0x1e00); too far -> back to the home cell
void circle(AnimatedActor& actor, GlydeTankFields& f) {
    if (actor.step == 0) {
        f.mode = kChaseMode;
        keepGait(f, false);
        actor.enterStep(1);
    }
    const Player& p = player();
    int ang = angleOf(p.pos, actor.pos);
    const int dist = planarDistance(p.pos, actor.pos);
    if (dist < 0x300) {
        ang = -ang;
    } else if (dist < planarDistance(actor.pos, kBaseCentre)) {
        const int d = angleDiff(uint32_t(ang), uint32_t(angleOf(kBaseCentre[0], kBaseCentre[2], actor.pos.x, actor.pos.z)));
        ang += d > 0 ? 0x400 : -0x400;
        if (f.circleBack != 0) ang += 0x800;
    } else {
        pickHomeCell(f, p);
        toHomeCell(actor, f, -1, 0x18);
    }
    turnYawTo(actor.rotation.y, uint32_t(ang), 0x18);
    integrateVelocity(actor, actor.velocity.x, actor.velocity.y, actor.velocity.z);
    chaseStep(actor, f);
    if (dist > kSpotRange) {
        f.homeCellX = kBaseCellX;
        f.homeCellZ = kBaseCellZ;
        enterSub(actor, TankSub::Return);
    }
}
// exe 0x4bf810: back to its home cell
void returnHome(AnimatedActor& actor, GlydeTankFields& f) {
    if (actor.step == 0) {
        f.mode = kStandMode;
        keepGait(f, false);
        actor.enterStep(1);
    }
    chaseStep(actor, f);
    if (toHomeCell(actor, f, -1, 0x18) != 0) enterSub(actor, TankSub::Patrol);
}
// exe 0x4bf860: a wall / the territory's edge -- turns a quarter away from the player
void turnBack(AnimatedActor& actor, GlydeTankFields& f) {
    const int dist = planarDistance(actor.pos, player().pos);
    if (actor.step == 0) {
        const int ang = angleOf(player().pos, actor.pos) & kAngleMask;
        const uint16_t yaw = uint16_t(actor.rotation.y);
        int d = int(yaw & kAngleMask) - ang;
        if (uint32_t(d + 0x800) > 0x1000u) d = (-d ^ kAngleMask) + 1;
        f.steerYaw = int16_t(d > 0 ? yaw - 0x400u : yaw + 0x400u);
        keepGait(f, true);
        actor.acceleration.z = kTankAccel;
        f.mode = kStandMode;
        actor.enterStep(1);
    }
    if (steer(actor, f, 0xa0, 0x18) != 0) enterSub(actor, TankSub::Chase);
    if (dist < kSpotRange) enterSub(actor, TankSub::Chase);
    spotPlayer(actor, f, dist);
    if (!(gameState().frame & 3) && actor.velocity.z <= -0xe0) treadDust(actor);
    stepSounds(actor, f);
}
// exe 0x4bf960: flinches (animation 1) until the animation ends
void flinch(AnimatedActor& actor, GlydeTankFields& f) {
    if (actor.step == 0) {
        f.mode = kFlinchMode;
        keepGait(f, false);
        f.neighbour ^= uint16_t(rngStep() & 1);
        actor.anim = 1;
        actor.enterStep(1);
        integrateVelocity(actor, 0, 0, actor.velocity.z);
        return;
    }
    if (actor.animEnded()) {
        f.fireTicks = randomOf(pauseTicksTable);
        enterSub(actor, TankSub::Patrol);
    }
    integrateVelocity(actor, 0, 0, actor.velocity.z);
}
// exe 0x4bfa20: the player stands on it -- brakes, stays in the territory, until he steps off
void ridden(AnimatedActor& actor, GlydeTankFields& f) {
    switch (RiddenStep(actor.step)) {
    case RiddenStep::Enter:
        keepGait(f, true);
        f.mode = kRiddenMode;
        actor.enterStep(uint8_t(RiddenStep::Slow));
        [[fallthrough]];
    case RiddenStep::Slow:
        actor.velocity.z += 8u;
        if (actor.velocity.z >= 0) {
            actor.enterStep(uint8_t(RiddenStep::Stopped));
            actor.velocity.z = 0;
        }
        stepSounds(actor, f);
        integrateVelocity(actor, 0, 0, actor.velocity.z);
        break;
    default: break;
    }
    if (!(f.mode & kTankNoTerritory)) {
        if (const int z = territoryViolated(actor.pos)) territoryPushBack(actor, z);
    }
    if (!playerRides(actor)) { // exe 0x59beb0
        f.fireTicks = randomOf(pauseTicksTable);
        enterSub(actor, TankSub::Patrol);
    }
}

// ---- the main state's tail ----------------------------------------------------------------------------------------
// exe 0x4be620: the territory's push back (kTankTerritory) unless kTankNoTerritory
void territory(AnimatedActor& actor, GlydeTankFields& f) {
    f.contact &= uint8_t(~kTankTerritory);
    if (f.mode & kTankNoTerritory) return;
    const int z = territoryViolated(actor.pos);
    if (z == 0) return;
    territoryPushBack(actor, z);
    f.contact |= kTankTerritory;
}
// a new slope's gait: the tilt's pitch step reset
void enterSlopeGait(GlydeTankFields& f, TankGait g) {
    f.gait = uint8_t(g);
    f.gaitBits &= kGaitSlopeMask;
    f.pitchStep = 4;
    f.pitchGrowth = 2;
}
// exe 0x4bff70: the speed by the slope's pitch (the gait)
void gaitStep(AnimatedActor& actor, GlydeTankFields& f) {
    const TankGait g = TankGait(f.gait);
    switch (g) {
    case TankGait::Level:
    case TankGait::Settled:
        if (g == TankGait::Level && !(f.gaitBits & kGaitHold)) {
            actor.acceleration.z = kTankAccel;
            if (speedToward(actor, 0xe0) != 0) {
                actor.velocity.z = int16_t(0xff20);
                f.gaitBits |= kGaitLevelSpeed;
            }
        }
        if (!(f.gaitBits & kGaitHold)) actor.acceleration.z = kTankAccel;
        if (f.slopePitch < -0x40) enterSlopeGait(f, TankGait::Uphill);
        if (f.slopePitch > 0x40) enterSlopeGait(f, TankGait::Downhill);
        return;
    case TankGait::Uphill:
        if (!(f.gaitBits & (kGaitHold | kGaitUphillSpeed))) {
            actor.acceleration.z = kTankAccel;
            if (speedToward(actor, 0x120) != 0) {
                actor.velocity.z = int16_t(0xfee0);
                f.gaitBits |= kGaitUphillSpeed;
            }
        }
        if (f.slopePitch >= 0) {
            f.gait = uint8_t(TankGait::Level);
            f.gaitBits |= kGaitFromUphill;
        }
        return;
    case TankGait::Downhill:
        if (!(f.gaitBits & (kGaitHold | kGaitDownhillSpeed))) {
            actor.acceleration.z = kTankAccel;
            if (speedToward(actor, 0xa0) != 0) {
                actor.velocity.z = int16_t(0xff60);
                f.gaitBits |= kGaitDownhillSpeed;
            }
        }
        if (f.slopePitch <= 0) {
            f.gait = uint8_t(TankGait::Level);
            f.gaitBits |= kGaitFromDownhill;
        }
        return;
    default: return;
    }
}
// exe 0x4c0100: the turret's yaw toward the player's position one tick ahead (+-0x20 a tick)
void aimTurret(AnimatedActor& actor, GlydeTankFields& f) {
    const Player& p = player();
    const uint16_t px = uint16_t(uint32_t(uint16_t(p.pos.x.whole)) * 17u - (uint32_t(uint16_t(p.prevPos.x.whole)) << 4));
    const uint16_t pz = uint16_t(uint32_t(uint16_t(p.pos.z.whole)) * 17u - (uint32_t(uint16_t(p.prevPos.z.whole)) << 4));
    const int ang = angleOf(int32_t(uint32_t(px) << 16), int32_t(uint32_t(pz) << 16), actor.pos.x, actor.pos.z);
    const int d = angleDiff(uint32_t(int32_t(f.turretYaw) + int32_t(actor.rotation.y)), uint32_t(ang));
    if (uint32_t(d + 0x20) > 0x40u) {
        f.turretYaw = int16_t(uint16_t(f.turretYaw) + (d > 0 ? 0x20u : 0xffe0u));
        return;
    }
    f.turretYaw = int16_t(angleDiff(uint32_t(int32_t(actor.rotation.y)), uint32_t(ang)));
}
// exe 0x4bfd40: the walls (the push-out; back to the previous position when it fails; kTankWall), on the ground + 0x10
void ground(const PlayerEnv& env, AnimatedActor& actor, GlydeTankFields& f) {
    f.contact &= uint8_t(~kTankWall);
    if (int32_t(actorTestMove(env, actor)) < 0) {   // exe 0x40f570
        const uint32_t r = actorPushOut(env, actor); // exe 0x410960
        if (int32_t(r) < 0) actor.pos = actor.prevPos;
        if (r & 0xf00) f.contact |= kTankWall;
    }
    actor.pos.y.whole = int16_t(actorGround(env, actor) + 0x10); // exe 0x40f190
}
// one axis of the tilt: the angle toward its target by the step (snapped within it), the step growing
void tiltToward(int16_t& angle, const int16_t& target, int16_t& step,
                const int16_t& growth) {
    const int s = step;
    const int d = angleDiff(uint32_t(int32_t(angle)), uint32_t(int32_t(target)));
    if (uint32_t(d + s) <= uint32_t(s + s)) {
        angle = target;
        return;
    }
    const uint16_t sv = uint16_t(step);
    angle = int16_t(d > 0 ? uint16_t(angle) + sv : uint16_t(angle) - sv);
    step = int16_t(uint16_t(growth) + sv);
}
// exe 0x4bdeb0: the pitch / the roll toward the slope's (accelerating steps)
void tilt(AnimatedActor& actor, GlydeTankFields& f) {
    tiltToward(actor.rotation.x, f.slopePitch, f.pitchStep, f.pitchGrowth);
    tiltToward(actor.rotation.z, f.slopeRoll, f.rollStep, f.rollGrowth);
}
// exe 0x4699a0: a hit by the player -> the flash, sound 0x8e (strong: 1) / 0x8d, the hit points (out: 2)
int hurt(AnimatedActor& actor, GlydeTankFields& f) {
    const uint32_t v = f.hitBox.result, d = v & kHitDamageMask;
    if (!(v & kHitByPlayer)) return 0;
    int r = 0;
    actor.tint = kTintFlash;
    if (v & kHitStrong) {
        r = 1;
        soundPlayAtPos(kHurtHardSound, actor.pos);
    } else {
        soundPlayAtPos(kHurtSound, actor.pos);
    }
    if (hitPointsAdd(actor, -int(d)) != 0) return 2;
    return r;
}
// exe 0x4bfbb0: two explosions, flashing 0x1e ticks, then a Birdbot bails out (the knocked-down one, pool 0x20 type
// 0x2d sub 0); the last of the wave: flag 0x22, the Birdbot stands up and the key (static type 0x42) falls
void destroyed(AnimatedActor& actor, GlydeTankFields& f) {
    switch (DestroyedStep(actor.step)) {
    case DestroyedStep::Explode:
        actor.enterStep(uint8_t(DestroyedStep::Burn));
        actor.anim = 1;
        f.phaseTicks = kDestroyedTicks;
        countDefeat(); // exe 0x5e53e0
        for (int v = 1; v >= 0; --v) {
            if (const uint32_t address = allocActor60(0)) { // exe 0x5e1300
                StaticActor& e = staticActorAt(address);
                e.flags = kActorLive;
                e.type = typeByte(GlobalStaticType::Explosion);
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
        [[fallthrough]];
    case DestroyedStep::Burn: {
        if (f.phaseTicks & 1) actor.tint = kFlashTint;
        --f.phaseTicks;
        if (f.phaseTicks != 0) return;
        actorDetachRiders(actor);                // exe 0x414e70
        const uint32_t birdbot = allocActor20(); // exe 0x5e1160
        if (birdbot) {
            AnimatedActor& c = animatedActorAt(birdbot);
            c.flags = kActorLive;
            c.loopFlags |= kActorSweepGroup2;
            c.type = kKnockedDownBirdbotType;
            c.subtype = 0;
            c.variant = 0;
            c.pos.x = actor.pos.x;
            c.pos.z = actor.pos.z;
            c.params[0] = 0;
            c.pos.y.whole = int16_t(actor.pos.y.whole - 0xc8);
        }
        EncounterState& wave = encounterState();
        wave.setWaveEnemiesWord(uint16_t(wave.waveEnemiesWord() - 1u));
        if (wave.waveEnemiesWord() == 0) {
            if (birdbot) animatedActorAt(birdbot).params[0] = kBirdbotStandsUp;
            setStoryFlag(kFlagGateBeaten);
            if (const uint32_t address = allocActor60(kActorStageClass)) { // exe 0x5e12f0
                StaticActor& key = staticActorAt(address);
                key.flags = kActorLive;
                key.loopFlags |= kActorSweepGroup2;
                key.type = kKeyType;
                key.subtype = 0;
                key.variant = 0;
                key.pos = actor.pos;
                key.params[0] = kKeyFromTank;
                key.velocity.z = 0;
                key.pos.y.whole = int16_t(key.pos.y.whole - 0x100);
                key.velocity.x = 0;
                key.velocity.y = int16_t(0xfe00);
            }
        }
        spawnDrops(actor.pos, statsOf(actor).dropTable); // exe 0x5f1270
        actorDetachRiders(actor);
        freeActor(actor);
        return;
    }
    default: return;
    }
}
// exe 0x4beff0
void init(AnimatedActor& actor, GlydeTankFields& f) {
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
    const uint16_t hp = hasAnimatedStats(actor.type) ? uint16_t(uint32_t(statsOf(actor).hitPoints) << 2) : 0;
    actor.velocity.clearWords();
    actor.rotation.z = 0;
    actor.rotation.x = 0;
    f.turretYaw = 0;
    actor.interactionBox = nullptr;
    actor.interaction = 0;
    actor.airborne = 0;
    actor.onPlatform = 0;
    actor.platform = nullptr;
    actor.actorHit = 0;
    actor.maxHitPoints = int16_t(hp);
    actor.hitPoints = int16_t(hp);
    actor.collisionBox.pointAt(TableId::GlydeBaseTankBox);
    actor.clearClassBlock();
    f.homeCellX = int8_t(uint16_t(actor.pos.x.whole) >> 8);
    f.homeCellZ = int8_t(uint16_t(actor.pos.z.whole) >> 8);
    resetBoneScales(actor);       // exe 0x5e3220
    registerEnemy(actor.slot, 2); // exe 0x5e5c10
    actor.enterState(uint8_t(TankState::Main));
    actor.parts[kShownPart].flags &= uint8_t(~kPartHidden);
}
// exe 0x4bf0e0
void mainState(const PlayerEnv& env, AnimatedActor& actor, GlydeTankFields& f) {
    if (!(f.mode & kTankNotRidable) && playerRides(actor)) enterSub(actor, TankSub::Ridden);
    if (!(f.mode & kTankNoHurt)) {
        const int r = hurt(actor, f);
        if (r == 2) {
            actor.enterState(uint8_t(TankState::Destroyed));
            if (actor.subState == 0) destroyed(actor, f); // exe 0x4bfb90
            return;
        }
        if (r == 1) enterSub(actor, TankSub::Flinch);
    }
    if (actor.variant == 0 && neighbourFind(actor, f.neighbour, 2) != 0) steer(actor, f, 0xa0, 0x18); // exe 0x5e5e00
    switch (TankSub(actor.subState)) {
    case TankSub::Wait: wait(actor, f); break;
    case TankSub::Patrol: patrol(actor, f); break;
    case TankSub::Chase: chase(actor, f); break;
    case TankSub::Circle: circle(actor, f); break;
    case TankSub::Return: returnHome(actor, f); break;
    case TankSub::TurnBack: turnBack(actor, f); break;
    case TankSub::Flinch: flinch(actor, f); break;
    case TankSub::Ridden: ridden(actor, f); break;
    default: notPortedAt(kTankPort, "class 0x4bedc0 sub-state", actor.subState);
    }
    territory(actor, f);
    gaitStep(actor, f);
    aimTurret(actor, f);
    ground(env, actor, f);
    tilt(actor, f);
    if ((f.contact & (kTankWall | kTankTerritory)) && !(f.mode & kTankNoTurnBack)) enterSub(actor, TankSub::TurnBack);
    if (actor.hitPoints < int16_t(actor.maxHitPoints >> 2) && !(gameState().frame & 7)) {
        const uint32_t r = rngStep();
        if (int16_t(uint16_t(0x2bc4u << (r & 0xf))) < 0) {
            if (const uint32_t address = allocActor60(kActorStageClass)) { // exe 0x5e12f0: smoke (static stage type 3)
                StaticActor& e = staticActorAt(address);
                e.flags = kActorLive;
                e.type = kSmokeType;
                e.variant = 0;
                e.loopFlags |= kActorSweepGroup2;
                e.pos.x = actor.pos.x;
                e.pos.y = int32_t(uint32_t(actor.pos.y) - 0x800000u);
                e.pos.z = actor.pos.z;
                SmokeParams smoke(actorHeaderAt(address));
                smoke.driftX = 0;
                smoke.driftZ = 0;
                smoke.life = 7;
            }
        }
    }
}

// exe 0x4bedc0's body
void tank(const PlayerEnv& env, AnimatedActor& actor) {
    GlydeTankFields& f = classState<GlydeTankFields>(actor);
    actor.prevAnim = actor.anim;
    actor.tint = kTintNeutral;
    actor.savePreviousPose();
    actor.savePreviousContact();
    f.fireRequest = 0;
    switch (TankState(actor.state)) {
    case TankState::Init: init(actor, f); break;
    case TankState::Main: mainState(env, actor, f); break;
    case TankState::Destroyed: // exe 0x4bfb90
        if (actor.subState == 0) destroyed(actor, f);
        break;
    case TankState::Free: // exe 0x4dd9b0
        actorDetachRiders(actor);
        freeActor(actor);
        break;
    default: notPortedAt(kTankPort, "class 0x4bedc0 state", actor.state);
    }
    if (!(actor.flags & kActorLive)) return;
    if (actor.anim == actor.prevAnim) animStep(actor); // exe 0x5e1ea0
    else animSet(actor, actor.anim, 0);                // exe 0x5e1c50
    LaneVec4 turret = decodedFrame().entries[kTurretAngles];
    turret.y = int16_t(uint16_t(f.turretYaw) + uint16_t(turret.y));
    if (f.mode & kTankRecoil) {
        decodedFrame().entries[kHullAngles].x -= 0x10u;
        f.mode &= uint8_t(~kTankRecoil);
    }
    animPose(actor); // exe 0x5e2d90
    if (f.fireRequest != 0) {
        const uint32_t r = rngStep() & 0xf;
        const int ticks = int8_t(f.fireTicks);
        if (pointVisibleOrNear(actor.pos, 0) != 0) shoot(actor, f, ((-ticks) >> 2) + 0x18, true); // exe 0x5e59a0
        else shoot(actor, f, ((int(r) - ticks) >> 2) + 0x10, false);
    }
    if (!(actor.flags & kActorDrawn)) return;
    f.hitBox.previousX = actor.prevPos.x.whole;
    f.hitBox.previousZ = actor.prevPos.z.whole;
    f.hitBox.result = 0;
    const AnimatedActorStats stats = statsOf(actor);
    const int16_t pos[3] = {actor.pos.x.whole, int16_t(actor.pos.y.whole - 0x80), actor.pos.z.whole};
    const uint8_t b = f.mode;
    if (b & kTankNoHitEntry) return;
    const uint32_t param =
        (((uint32_t(actor.slot) << 15) | stats.hitHeight) << 9) | (uint32_t(b & kTankHitStrong) << 14) | 0x400030u;
    hitListSubmit(pos, f.hitBox, param, actor.hitPoints >= 0 ? kHitAttackSolid : kHitAttackBump); // exe 0x5e4120
    actorCollisionSubmitSolid(actor, SolidType::StandOn, kNoBone, stats.attack | kHitAttackSolid); // exe 0x414050
}

} // namespace

void glydeBaseTankUpdate(const PlayerEnv& env, uint32_t actor) { tank(env, animatedActorAt(actor)); }

} // namespace od::port
