// Data in the Flutter's kitchen during the fire mission (ST1E pool 0x20 type 0x63, exe 0x4e8530): he wanders, catches
// fire when a burning thing hits him (a flame on his head, one more fire to put out), then runs in panic -- to a point
// near the counter, around his home point, along one of four paths, spinning at home, at the player; the
// extinguisher's water (or a shot) puts him out and he walks home. Once every fire is out he stands at home. Also Data
// in the mission's result scene (sub 1, exe 0x4ea1d0): hopping around the player (success) or burnt (failure).
#include "content/intro/fire_mission.hpp"
#include "content/intro/burning_data_state.hpp"

#include "engine/data_source.hpp"
#include "game/actor_animation.hpp"
#include "game/actor_collision.hpp"
#include "game/actor_model.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/collision.hpp"
#include "game/distances.hpp"
#include "game/fixed_point.hpp"
#include "game/game_state.hpp"
#include "game/geometry_scratch.hpp"
#include "game/hit_list.hpp"
#include "game/matrix.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/table_records.hpp"

namespace od::port {

using namespace burning_data_state;

namespace {

inline constexpr const char* kDataArea = "burning Data";

constexpr uint32_t kDataBlock = 0x14c;

// [0x896430]
enum class DataState : uint8_t { Spawn = 0, Active = 1, Free = 2 }; // exe 0x4e85f0 / 0x4e86a0 / 0x468fa0
enum class DataSub : uint8_t {
    Wander = 0,       // exe 0x4e8860
    CatchFire = 1,    // exe 0x4e8980
    RunToCounter = 2, // exe 0x4e8a50: to the panic point
    Circle = 3,       // exe 0x4e8b50: around his home point
    PutOut = 4,       // exe 0x4e8c90: back home
    RunPath = 5,      // exe 0x4e8d90
    Spin = 6,         // exe 0x4e9190
    RunAtPlayer = 7,  // exe 0x4e92d0
    Stand = 8,        // exe 0x4e9560: the fires are all out
};
// the animations
enum class DataAnim : uint8_t { Stand = 0, Walk = 1, CatchFire = 2, Run = 3, Burnt = 5 };
constexpr uint8_t kNoAnim = 0xff;

const DataTable<CollisionBoxRecord> dataBox{TableId::BurningDataBox};
const DataTable<ShortPoint> dataHome{TableId::BurningDataHome}; // his home point (exe data 0x831870)
// one point of his panic paths (exe data 0x831878: 4 paths of 8 points): the point, its control word (bits 12..13
// the move to it; negative: the path's end, & 0xf its restart step)
struct DataPathPoint : DataRecord {
    ShortPoint point;
    int16_t control = 0;
    static DataPathPoint load(const TableReader& r) { return {{}, ShortPoint::load(r), r.s16(6)}; }
};
constexpr uint32_t kDataPathCount = 4, kPathPoints = 8;
const DataTable<RecordTable<DataPathPoint, kDataPathCount * kPathPoints, 8>> dataPaths{TableId::BurningDataPaths};
enum class PathMove : uint8_t { Walk = 0, Hop = 1, Climb = 2 }; // [0x896460]: exe 0x4e8ef0 / 0x4e8fc0 / 0x4e90c0
const DataTable<ScalarTable<uint32_t, 16>> animCells{TableId::BurningDataAnimCells}; // the texture cell of each animation (exe data)

constexpr uint16_t kFullHitPoints = 0x800;
constexpr int16_t kGravity = 0x28;
constexpr int16_t kSpinSpeed = 0x10; // his spin.y at the spawn
constexpr int16_t kWalkSpeed = -0xc0, kRunSpeed = -0x190, kSceneHop = -0x100;
constexpr int16_t kSpinTurn = 0x230;
constexpr int kCircleRadius = 0x70, kPanicRadius = 0x700;
constexpr uint32_t kCirclePull = 0x700; // back on the circle (the normalised offset * this: kCircleRadius units)
constexpr int16_t kCircleTicks = 0x5a, kSpinTicks = 0x4b;
constexpr int kJumpAtPlayer = 0x100;
constexpr int kSceneRadius = 0x80;
constexpr uint32_t kSceneRadiusPull = 0x800; // pulled back this far (the normalised offset's scale)
constexpr uint32_t kDataGroundMask = 2;
constexpr int kHopSpeed = 400;
constexpr int16_t kRegrowth = 0x10;
constexpr int16_t kOutHitPoints = 0x201;
constexpr uint16_t kCatchFireSound = 0x154, kCryA = 0x155, kCryB = 0x15e;
constexpr uint16_t kBurntTint = 0x2108;
// the room he stays in (x 0x20..0x130, z 0x50..0x150)
constexpr int16_t kRoomMinX = 0x20, kRoomMaxX = 0x130, kRoomMinZ = 0x50, kRoomMaxZ = 0x150;
constexpr int kHeadBone = 6;
constexpr int kHeadReach = -0x20;

// The lanes of the geometry scratch the exe leaves its points in (+0x60.., +0x70..; kept written: the routines that
// follow may read them; +0xa0.. / +0xb0..: Data's own)
struct DataScratch {
    LaneVec3 walkPoint = geometryScratch().vec3(0x60); // the walk's target less its margin; the hit entry's point
    std::array<Lane32, 3> target = geometryScratch().words<3>(0x70); // the point the angle / distance helpers read
    std::array<Lane32, 3> offset = geometryScratch().words<3>(0xa0);
    std::array<Lane32, 3> unit = geometryScratch().words<3>(0xb0);
};
// the scratch's one instance
DataScratch& scratch() {
    static DataScratch s;
    return s;
}
// a 16.16 point into the scratch the helpers read it from
Fixed16Vec3 scratchPoint(const int32_t p[3]) {
    auto& t = scratch().target;
    Fixed16Vec3 v;
    v.x = t[0] = p[0];
    v.y = t[1] = p[1];
    v.z = t[2] = p[2];
    return v;
}
// his home point as a 16.16 point
void homePoint(int32_t p[3]) {
    p[0] = int32_t(uint32_t(int32_t(dataHome->x)) << 16);
    p[1] = int32_t(uint32_t(int32_t(dataHome->y)) << 16);
    p[2] = int32_t(uint32_t(int32_t(dataHome->z)) << 16);
}
// the actor put at the 16.16 point
void setPos(Actor& actor, const int32_t p[3]) {
    actor.pos.x = p[0];
    actor.pos.y = p[1];
    actor.pos.z = p[2];
}
// a sub-state entered (its step and step counter cleared: the exe's word store)
void setSub(AnimatedActor& actor, DataSub sub) {
    actor.subState = uint8_t(sub);
    actor.restartStep();
}
// the animation asked for
void setAnim(AnimatedActor& actor, DataAnim anim) { actor.anim = uint8_t(anim); }

// exe 0x4e9a60: a cry every 0xb4..0xd3 ticks
void cry(const AnimatedActor& actor, DataFields& data) {
    if (data.cryTimer == 0) {
        const uint32_t r = rngStep();
        data.cryTimer = int16_t((r & 0x1f) + 0xb4);
        soundPlayAtPos(((r >> 5) & 1) ? kCryB : kCryA, actor.pos);
        return;
    }
    --data.cryTimer;
}
// exe 0x4e98c0: kept inside the kitchen; the wall bits for the reflection
uint32_t keepInRoom(AnimatedActor& actor) {
    uint32_t r = 0;
    if (actor.pos.x.whole < kRoomMinX) {
        actor.pos.x = int32_t(uint32_t(kRoomMinX) << 16);
        r = kWallOnXMinus;
    } else if (actor.pos.x.whole > kRoomMaxX) {
        actor.pos.x = int32_t(uint32_t(kRoomMaxX) << 16);
        r = kWallOnXPlus;
    }
    if (actor.pos.z.whole < kRoomMinZ) {
        actor.pos.z = int32_t(uint32_t(kRoomMinZ) << 16);
        return r | kWallOnZMinus;
    }
    if (actor.pos.z.whole > kRoomMaxZ) {
        actor.pos.z = int32_t(uint32_t(kRoomMaxZ) << 16);
        r |= kWallOnZPlus;
    }
    return r;
}
// exe 0x4e9720: the water / a shot on the burning Data (the fires' formula, on his hit points); -1 = put out
int takeWater(AnimatedActor& actor, const DataFields& data) {
    const uint32_t v = data.hitBox.result;
    int d = int(v & kHitDamageMask) * 0x20;
    if (!(v & (kHitAttackByPlayer | kHitAttackWater))) return 0;
    if (v & kHitAttackWater) {
        int dist = distance3(player().pos, actor.pos);
        if (dist > 0x100)
            dist = 0x100;
        else if (dist < 0x80)
            dist = 0x80;
        d += ((((0x100 - dist) * 0x340) >> 7) + 0x3c0) >> 3;
    }
    if (d == 0) return 0;
    if (data.has(DataFields::kCatchingFire)) return 1;
    actor.hitPoints = wrap16(actor.hitPoints - int16_t(d));
    if (actor.hitPoints < kOutHitPoints) {
        actor.hitPoints = 0;
        return -1;
    }
    return 1;
}
// exe 0x4e97c0: the hit entry 0x20 above him (turned by his rotation): touchable, or, burning, a burning body
void submitHit(const AnimatedActor& actor, DataFields& data) {
    data.hitBox.previousX = actor.prevPos.x.whole;
    data.hitBox.previousZ = actor.prevPos.z.whole;
    data.hitBox.result = 0;
    int32_t o[3];
    rotateVector(actor.rotation, 0, -0x200, 0, o);
    DataScratch& s = scratch();
    for (size_t k = 0; k < 3; ++k) s.offset[k] = o[k];
    const int16_t pos[3] = {wrap16((o[0] >> 16) + actor.pos.x.whole), wrap16((o[1] >> 16) + actor.pos.y.whole),
                            wrap16((o[2] >> 16) + actor.pos.z.whole)};
    s.walkPoint.x = pos[0];
    s.walkPoint.y = pos[1];
    s.walkPoint.z = pos[2];
    const bool burning = data.has(DataFields::kBurning);
    const uint32_t shape = burning ? hitGroup(kHitGroupBodies) : (hitGroup(kHitGroupPlayerAttacks) | kHitNoLockOn);
    const uint32_t attack = burning ? (kHitAttackThrown | kHitAttackSolid | 0x10) : kHitAttackByPlayer;
    hitListSubmit(pos, data.hitBox, hitOwner(actor.slot) | shape | hitRadius(0x20), attack | kHitAttackHarmless);
}
// exe 0x4e9920(actor, point, turn): one step toward the 16.16 point (turned toward it when asked); true = arrived
// (snapped onto it)
bool walkTo(AnimatedActor& actor, const int32_t p[3], bool turn) {
    if (actor.airborne) actor.velocity.y = wrap16(actor.velocity.y + actor.acceleration.y);
    uint32_t h, v;
    if (actor.rotation.x == 0) {
        h = uint32_t(int32_t(actor.velocity.z));
        if (actor.velocity.z < 0) h = 0u - h;
        v = uint32_t(int32_t(actor.velocity.y));
        if (actor.velocity.y < 0) v = 0u - v;
    } else {
        v = uint32_t(int32_t(actor.velocity.z));
        if (actor.velocity.z < 0) v = 0u - v;
        h = 0x20;
    }
    const int16_t hs = wrap16(int(h >> 5));
    auto&& margin = scratch().walkPoint;
    margin.x = wrap16(int16_t(uint32_t(p[0]) >> 16) - hs);
    margin.y = wrap16(int16_t(uint32_t(p[1]) >> 16) - wrap16(int(v >> 5)));
    margin.z = wrap16(int16_t(uint32_t(p[2]) >> 16) - hs);
    const int16_t px = margin.x, py = margin.y, pz = margin.z;
    bool away;
    if (actor.rotation.x == 0)
        away = (h >> 3) < uint32_t(int(actor.pos.x.whole) - px) || (h >> 3) < uint32_t(int(actor.pos.z.whole) - pz);
    else
        away = (h >> 3) < uint32_t(int(actor.pos.x.whole) - px) || (v >> 3) < uint32_t(int(actor.pos.y.whole) - py) ||
               (h >> 3) < uint32_t(int(actor.pos.z.whole) - pz);
    if (away) {
        if (turn) actor.rotation.y = int16_t(angleOf(scratchPoint(p), actor.pos));
        moveAlongRotation(actor.pos, actor.rotation, 0, actor.velocity.y, actor.velocity.z); // exe 0x5e37b0
        return false;
    }
    setPos(actor, p);
    return true;
}
// exe 0x4e9630(actor, mask): the ground -- on it: followed (a drop of more than 0x20 starts a fall, a step up pushes
// out); in the air: the push-out (landed: on the ground). 2 = a wall (its bits in wallHit), 1 = started falling
int followGround(const PlayerEnv& env, AnimatedActor& actor, uint32_t mask) {
    actor.wallHit = 0;
    if (actor.airborne == 0) {
        const int16_t p[3] = {actor.pos.x.whole, actor.pos.y.whole, actor.pos.z.whole};
        const int g = probeGroundAt(env, p, actor.collisionBox,
                                    int(actor.pos.y.whole) - int(actor.prevPos.y.whole), mask);
        if (uint32_t(g + (0x20 - int(actor.pos.y.whole))) > 0x40) {
            if (g < int(actor.pos.y.whole)) {
                const uint32_t r = actorPushOutMask(env, actor, mask);
                actor.wallHit = r;
                if (int32_t(r) < 0) actor.pos = actor.prevPos;
                return 2;
            }
            actor.airborne = kAirborneFalling;
            return 1;
        }
        actor.pos.y = int32_t(uint32_t(g) << 16);
    } else {
        const uint32_t r = actorPushOutMask(env, actor, mask);
        actor.wallHit = r;
        if (int32_t(r) < 0) {
            actor.pos = actor.prevPos;
            return 2;
        }
        if (r & kWallOnYPlus) {
            actor.airborne = 0;
            return 0;
        }
        if (r != 0) return 2;
    }
    return 0;
}
// the panic point (by the counter): kPanicRadius along a half turn from home, at height 0
void panicPoint(int32_t p[3]) {
    int32_t o[3];
    angleRadiusVector(kHalfTurn, kPanicRadius, o); // exe 0x5e4070
    auto&& offset = scratch().offset;
    for (size_t k = 0; k < 3; ++k) offset[k] = o[k];
    p[0] = int32_t(uint32_t(o[0]) + (uint32_t(int32_t(dataHome->x)) << 16));
    p[1] = 0;
    p[2] = int32_t((uint32_t(int32_t(dataHome->z)) << 16) + uint32_t(o[2]));
}
// the actor put `radius` units from the centre toward itself (x / z), through the normalised offset (exe 0x40c320)
void pullToRadius(AnimatedActor& actor, int16_t centreX, int16_t centreZ, uint32_t radius) {
    DataScratch& s = scratch();
    s.offset[0] = int32_t(actor.pos.x.whole) - int32_t(centreX);
    s.offset[1] = 0;
    s.offset[2] = int32_t(actor.pos.z.whole) - int32_t(centreZ);
    {
        const int32_t in[3] = {s.offset[0], s.offset[1], s.offset[2]};
        int32_t out[3];
        normalizeVector(in, out); // exe 0x40c320
        for (size_t i = 0; i < 3; ++i) s.unit[i] = out[i];
    }
    actor.pos.x = int32_t((uint32_t(int32_t(centreX)) << 16) + uint32_t(s.unit[0]) * radius);
    actor.pos.z = int32_t((uint32_t(int32_t(centreZ)) << 16) + uint32_t(s.unit[2]) * radius);
}

// +0xa of the sub-state Wander: the walk started, walking
enum class WanderStep : uint8_t { Start = 0, Walk = 1 };

// ---- the sub-states ------------------------------------------------------------------------------------------------
void wander(AnimatedActor& actor, DataFields& data) { // a cry every 0x78..0x97 ticks while not panicking
    static constexpr uint16_t kCries[4] = {0x157, 0x15a, 0x15c, 0x15d};
    if (WanderStep(actor.step) == WanderStep::Start) {
        actor.velocity.z = kWalkSpeed;
        setAnim(actor, DataAnim::Walk);
        const uint32_t r = rngStep();
        data.cryTimer = int16_t((r & 0x1f) + 0x78);
        ++actor.step;
    } else if (WanderStep(actor.step) != WanderStep::Walk) {
        return;
    }
    integrateVelocity(actor, 0, 0, actor.velocity.z);
    reflectRotationOffWalls(actor.rotation, keepInRoom(actor));
    const uint32_t r = rngStep();
    actor.rotation.y = wrap16(actor.rotation.y + (0x80 - int(r & 0xff)));
    if (fireMission().dataPanicking()) return;
    if (data.cryTimer == 0) {
        data.cryTimer = int16_t(((r >> 8) & 0x1f) + 0x78);
        soundPlayAtPos(kCries[(r >> 13) & 3], actor.pos);
        return;
    }
    --data.cryTimer;
}

// +0xa of CatchFire: his flame lit, until the animation ends
enum class CatchFireStep : uint8_t { Light = 0, Burn = 1 };

void catchFire(AnimatedActor& actor, DataFields& data) { // the flame on his head, one more fire
    FireMissionState& mission = fireMission();
    if (CatchFireStep(actor.step) == CatchFireStep::Light) {
        mission.setDataPanicking();
        data.setCatchingFire();
        setAnim(actor, DataAnim::CatchFire);
        actor.rotation.y = int16_t(angleOf(player().pos, actor.pos));
        const uint32_t e = allocActorA0(kActorStageClass); // exe 0x5e13a0
        data.flame = e;
        if (e != 0) {
            EffectActor& flame = effectActorAt(e);
            flame.flags = kActorLive;
            flame.type = kFireEffectType;
            flame.subtype = uint8_t(FireEffectKind::DataFlame);
            actor.hitPoints = actor.maxHitPoints;
            ++mission.firesLeft;
            data.cryTimer = 0x28;
            soundPlayAtPos(kCatchFireSound, actor.pos);
            ++actor.step;
        }
    } else if (CatchFireStep(actor.step) == CatchFireStep::Burn && actor.animEnded()) {
        setSub(actor, DataSub::RunToCounter);
    }
}

// +0xa of RunToCounter: the run started, running
enum class RunToCounterStep : uint8_t { Start = 0, Run = 1 };

// exe 0x4e8a50: runs to the panic point by the counter, then circles home; put out: back home
void runToCounter(AnimatedActor& actor, DataFields& data) {
    int32_t p[3];
    panicPoint(p);
    bool running = true;
    if (RunToCounterStep(actor.step) == RunToCounterStep::Start) {
        data.flags &= uint16_t(~DataFields::kCatchingFire);
        setAnim(actor, DataAnim::Run);
        actor.velocity.z = kRunSpeed;
        actor.rotation.y = int16_t(angleOf(scratchPoint(p), actor.pos));
        ++actor.step;
    } else if (RunToCounterStep(actor.step) != RunToCounterStep::Run) {
        running = false;
    }
    if (running && walkTo(actor, p, true)) setSub(actor, DataSub::Circle);
    if (!data.has(DataFields::kBurning)) setSub(actor, DataSub::PutOut);
    cry(actor, data);
}

// +0xa of Circle: the timer started, circling
enum class CircleStep : uint8_t { Start = 0, Circle = 1 };

void circle(AnimatedActor& actor, DataFields& data) { // around home (radius kCircleRadius) for kCircleTicks
    if (CircleStep(actor.step) == CircleStep::Start) {
        data.timer = 0;
        ++actor.step;
    }
    integrateVelocity(actor, 0, 0, actor.velocity.z);
    int32_t h[3];
    homePoint(h);
    const Fixed16Vec3 home = scratchPoint(h);
    actor.rotation.y = int16_t(angleOf(actor.pos, home) + kQuarterTurn);
    if (planarDistance(home, actor.pos) > kCircleRadius) pullToRadius(actor, dataHome->x, dataHome->z, kCirclePull);
    ++data.timer;
    if (uint16_t(data.timer) > uint16_t(kCircleTicks)) setSub(actor, DataSub::RunPath);
    if (!data.has(DataFields::kBurning)) setSub(actor, DataSub::PutOut);
    cry(actor, data);
}

// +0xa of PutOut: the walk (or the hop) home started, walking home
enum class PutOutStep : uint8_t { Start = 0, WalkHome = 1 };

void putOut(AnimatedActor& actor) { // back home (a hop when off the floor), the panic over
    int32_t h[3];
    homePoint(h);
    if (PutOutStep(actor.step) == PutOutStep::Start) {
        actor.rotation.x = 0;
        actor.velocity.z = kRunSpeed;
        setAnim(actor, DataAnim::Walk);
        if (actor.pos.y.whole != dataHome->y) {
            actor.velocity.y =
                wrap16(launchSpeed(actor.pos, scratchPoint(h), kHopSpeed, actor.acceleration.y, 0xffffffffu));
            actor.airborne = kAirborneFalling;
            ++actor.step;
            return;
        }
        actor.velocity.y = 0;
        actor.airborne = 0;
        ++actor.step;
        return;
    }
    if (PutOutStep(actor.step) != PutOutStep::WalkHome) return;
    if (walkTo(actor, h, true)) {
        actor.velocity.y = 0;
        actor.airborne = 0;
        setPos(actor, h);
        fireMission().dataFlags &= uint16_t(~FireMissionState::kDataPanicking);
        setSub(actor, DataSub::Wander);
    }
}

// the point of his path at its step
const DataPathPoint& pathPoint(const DataFields& data) {
    return dataPaths[uint32_t(data.pathStep) + uint32_t(data.path) * kPathPoints];
}
// +0xb of a path move: aimed at its point, moving to it
enum class PathMoveStep : uint8_t { Start = 0, Move = 1 };

// [0x896460]: the move to the path's point -- walk, hop or climb
void pathMove(AnimatedActor& actor, const DataFields& data) {
    const DataPathPoint e = pathPoint(data);
    const int32_t p[3] = {int32_t(uint32_t(int32_t(e.point.x)) << 16), int32_t(uint32_t(int32_t(e.point.y)) << 16),
                          int32_t(uint32_t(int32_t(e.point.z)) << 16)};
    const PathMove move = PathMove((uint16_t(e.control) >> 12) & 3);
    if (move == PathMove::Walk) { // exe 0x4e8ef0
        if (PathMoveStep(actor.stepCounter) == PathMoveStep::Start) {
            actor.rotation.x = 0;
            actor.rotation.y = int16_t(angleOf(scratchPoint(p), actor.pos));
            actor.velocity.z = kRunSpeed;
            ++actor.stepCounter;
        } else if (PathMoveStep(actor.stepCounter) == PathMoveStep::Move && walkTo(actor, p, true)) {
            actor.stepCounter = uint8_t(PathMoveStep::Start);
            ++actor.step;
        }
    } else if (move == PathMove::Hop) { // exe 0x4e8fc0
        if (PathMoveStep(actor.stepCounter) == PathMoveStep::Start) {
            actor.rotation.x = 0;
            actor.rotation.y = int16_t(angleOf(scratchPoint(p), actor.pos));
            actor.velocity.y = wrap16(
                launchSpeed(actor.pos, scratchPoint(p), -int(actor.velocity.z), actor.acceleration.y, 0xffffffffu));
            actor.airborne = kAirborneFalling;
            ++actor.stepCounter;
        } else if (PathMoveStep(actor.stepCounter) == PathMoveStep::Move) {
            if (walkTo(actor, p, true)) {
                actor.velocity.y = 0;
                actor.velocity.z = kRunSpeed;
                actor.airborne = 0;
                ++actor.step;
                actor.stepCounter = uint8_t(PathMoveStep::Start);
            }
            if (int32_t(actor.pos.y) >= 0) actor.pos.y = 0;
        }
    } else if (move == PathMove::Climb) { // exe 0x4e90c0
        if (PathMoveStep(actor.stepCounter) == PathMoveStep::Start) {
            if (p[1] < int32_t(actor.pos.y)) {
                actor.rotation.y = kQuarterTurn;
                actor.rotation.x = -kQuarterTurn;
            } else {
                actor.rotation.y = -kQuarterTurn;
                actor.rotation.x = kQuarterTurn;
            }
            actor.stepCounter = uint8_t(PathMoveStep::Move);
        } else if (PathMoveStep(actor.stepCounter) == PathMoveStep::Move && walkTo(actor, p, false)) {
            actor.stepCounter = uint8_t(PathMoveStep::Start);
            ++actor.step;
        }
    } else {
        notPorted(kDataArea, "path move 3 (table 0x896460 has no entry 3)");
    }
}

// +0xa of RunPath: the path picked, moving to its point, at the point
enum class RunPathStep : uint8_t { Pick = 0, Move = 1, Arrived = 2 };

void runPath(AnimatedActor& actor, DataFields& data) { // a random path (of four), 1..4 times
    const RunPathStep c = RunPathStep(actor.step);
    if (c == RunPathStep::Pick) {
        data.path = uint8_t(rngStep() & 3);
        data.pathStep = 0;
        data.timer = int16_t(rngStep() & 3);
        ++actor.step;
    } else if (c != RunPathStep::Move) {
        if (c == RunPathStep::Arrived) {
            if (!data.has(DataFields::kBurning)) {
                actor.rotation.x = 0;
                setSub(actor, DataSub::PutOut);
                cry(actor, data);
                return;
            }
            const DataPathPoint e = pathPoint(data);
            if (e.control < 0) {
                data.pathStep = uint8_t(uint16_t(e.control) & 0xf);
                if (data.timer != 0) {
                    --data.timer;
                    actor.step = uint8_t(RunPathStep::Move);
                    cry(actor, data);
                    return;
                }
                actor.rotation.x = 0;
                setSub(actor, DataSub::Spin);
                cry(actor, data);
                return;
            }
            ++data.pathStep;
            actor.step = uint8_t(RunPathStep::Move);
        }
        cry(actor, data);
        return;
    }
    pathMove(actor, data);
    cry(actor, data);
}

// +0xa of Spin: the walk home started, walking home, spinning
enum class SpinStep : uint8_t { Start = 0, WalkHome = 1, Spin = 2 };

void spin(AnimatedActor& actor, DataFields& data) { // back home, then spins for kSpinTicks
    int32_t h[3];
    homePoint(h);
    const SpinStep c = SpinStep(actor.step);
    bool walk = true;
    if (c == SpinStep::Start) {
        data.timer = kSpinTicks;
        actor.spin.y = kSpinTurn;
        actor.velocity.z = kRunSpeed;
        if (actor.pos.y.whole == dataHome->y) {
            actor.velocity.y = 0;
            actor.airborne = 0;
        } else {
            actor.velocity.y =
                wrap16(launchSpeed(actor.pos, scratchPoint(h), kHopSpeed, actor.acceleration.y, 0xffffffffu));
            actor.airborne = kAirborneFalling;
        }
        setAnim(actor, DataAnim::Run);
        ++actor.step;
    } else if (c != SpinStep::WalkHome) {
        walk = false;
        if (c == SpinStep::Spin) {
            actor.rotation.y = wrap16(actor.rotation.y + actor.spin.y);
            --data.timer;
            if (data.timer == 0) setSub(actor, DataSub::RunAtPlayer);
        }
    }
    if (walk && walkTo(actor, h, true)) {
        actor.velocity.y = 0;
        actor.airborne = 0;
        setPos(actor, h);
        ++actor.step;
    }
    if (!data.has(DataFields::kBurning) && actor.airborne == 0) setSub(actor, DataSub::PutOut);
    cry(actor, data);
}

// +0xa of RunAtPlayer: the cry, running at him, the jump, falling, back to the panic point
enum class RunAtStep : uint8_t { Cry = 0, Run = 1, Jump = 2, Fall = 3, Back = 4 };

void runAtPlayer(const PlayerEnv& env, AnimatedActor& actor, DataFields& data) { // jumps at him within 0x100
    static constexpr uint16_t kCries[16] = {0x156, 0x159, 0x15b, 0x156, 0x159, 0x15b, 0x156, 0x159,
                                            0x15b, 0x156, 0x159, 0x15b, 0x156, 0x159, 0x15b, 0x159};
    switch (RunAtStep(actor.step)) {
    case RunAtStep::Cry:
        actor.velocity.y = 0;
        soundPlayAtPos(kCries[rngStep() & 0xf], actor.pos);
        ++actor.step;
        [[fallthrough]];
    case RunAtStep::Run:
        actor.rotation.y = int16_t(angleOf(player().pos, actor.pos));
        integrateVelocity(actor, 0, 0, actor.velocity.z);
        if (planarDistance(player().pos, actor.pos) < kJumpAtPlayer) {
            actor.velocity.y = kRunSpeed;
            actor.airborne = kAirborneFalling;
            ++actor.step;
        }
        followGround(env, actor, kDataGroundMask);
        break;
    case RunAtStep::Jump:
        if (!(data.hitBox.result & kHitAttackBump)) {
            actor.velocity.y = wrap16(actor.velocity.y + actor.acceleration.y);
            integrateVelocity(actor, 0, actor.velocity.y, actor.velocity.z);
            const int r = followGround(env, actor, kDataGroundMask);
            if (actor.airborne == 0) {
                actor.velocity.y = 0;
                actor.step = uint8_t(actor.step + 2);
            } else if (r == 2) {
                reflectRotationOffWalls(actor.rotation, actor.wallHit);
                ++actor.step;
            }
        } else { // bounced off the player
            actor.rotation.y = wrap16(actor.rotation.y + kHalfTurn);
            ++actor.step;
        }
        break;
    case RunAtStep::Fall:
        actor.velocity.y = wrap16(actor.velocity.y + actor.acceleration.y);
        integrateVelocity(actor, 0, actor.velocity.y, actor.velocity.z);
        followGround(env, actor, kDataGroundMask);
        if (actor.airborne == 0) {
            actor.velocity.y = 0;
            ++actor.step;
        }
        break;
    case RunAtStep::Back: {
        int32_t p[3];
        panicPoint(p);
        if (!walkTo(actor, p, true)) {
            followGround(env, actor, kDataGroundMask);
            if (actor.airborne != 0) actor.step = uint8_t(RunAtStep::Fall);
        } else {
            setSub(actor, DataSub::Circle);
        }
        break;
    }
    default: break;
    }
    if (!data.has(DataFields::kBurning) && actor.airborne == 0) setSub(actor, DataSub::PutOut);
}

// +0xa of Stand: the walk home started, walking home, home, standing
enum class StandStep : uint8_t { Start = 0, WalkHome = 1, Arrived = 2, Stand = 3 };

void stand(const PlayerEnv& env, AnimatedActor& actor) { // walks home and stands
    int32_t h[3];
    homePoint(h);
    const StandStep c = StandStep(actor.step);
    if (c == StandStep::Start) {
        actor.velocity.z = kWalkSpeed;
        setAnim(actor, DataAnim::Walk);
        actor.step = uint8_t(StandStep::WalkHome);
    } else if (c != StandStep::WalkHome) {
        if (c != StandStep::Arrived) return;
        setAnim(actor, DataAnim::Stand);
        actor.velocity.z = 0;
        actor.step = uint8_t(StandStep::Stand);
        return;
    }
    if (walkTo(actor, h, (int(int8_t(actor.airborne)) ^ 1) != 0)) {
        actor.velocity.y = 0;
        actor.airborne = 0;
        setPos(actor, h);
        ++actor.step;
    }
    followGround(env, actor, kDataGroundMask);
}

void spawn(AnimatedActor& actor, DataFields& data) { // exe 0x4e85f0
    if (!bindActorModel(actor, 0)) {
        freeActor(actor);
        return;
    }
    actor.flags |= kActorDrawn;
    setAnim(actor, DataAnim::Walk);
    actor.prevAnim = kNoAnim;
    actor.maxHitPoints = int16_t(kFullHitPoints);
    actor.hitPoints = 0;
    actor.rotation.z = 0;
    actor.rotation.x = 0;
    actor.velocity.setZSpare(0, 0);
    actor.velocity.setXY(0, 0);
    actor.acceleration.y = kGravity;
    actor.spin.setXY(0, kSpinSpeed);
    actor.collisionBox.pointAt(dataBox.stateAddress());
    actor.actorHit = 0;
    actor.platform = nullptr;
    data.clear(); // exe 0x40dc40(+0x14c, +0x168)
    setTexturePageCell(actor.texturePage, 0);
    animSet(actor, actor.anim, 0); // exe 0x5e1c30
    animPose(actor);
    actor.enterState(uint8_t(DataState::Active));
}

void active(const PlayerEnv& env, AnimatedActor& actor, DataFields& data) { // exe 0x4e86a0
    FireMissionState& mission = fireMission();
    bool sprayed = false;
    if (!(data.flags & DataFields::kDone)) {
        if (data.flags & DataFields::kBurning) {
            const int r = takeWater(actor, data);
            if (r == -1) { // put out: his flame goes
                data.flags &= uint16_t(~DataFields::kBurning);
                effectActorAt(data.flame).state = uint8_t(FireEffectState::Done);
                data.flame = 0;
                --mission.firesLeft;
            } else if (r == 1) {
                sprayed = true;
            } else {
                const int16_t full = actor.maxHitPoints, hp = actor.hitPoints;
                if (hp < full) {
                    const int16_t n = wrap16(hp + kRegrowth);
                    actor.hitPoints = n;
                    if (n > full) actor.hitPoints = full;
                }
            }
        } else if (data.hitBox.result & kHitAttackStrong) { // a burning thing hit him
            data.flags |= DataFields::kBurning;
            actor.subState = uint8_t(DataSub::CatchFire);
            actor.restartStep();
        }
        if (mission.firesLeft == 0 && DataSub(actor.subState) == DataSub::Stand) {
            data.flags |= DataFields::kDone;
            actor.subState = uint8_t(DataSub::Stand);
            actor.restartStep();
        }
    }
    switch (DataSub(actor.subState)) {
    case DataSub::Wander: wander(actor, data); break;
    case DataSub::CatchFire: catchFire(actor, data); break;
    case DataSub::RunToCounter: runToCounter(actor, data); break;
    case DataSub::Circle: circle(actor, data); break;
    case DataSub::PutOut: putOut(actor); break;
    case DataSub::RunPath: runPath(actor, data); break;
    case DataSub::Spin: spin(actor, data); break;
    case DataSub::RunAtPlayer: runAtPlayer(env, actor, data); break;
    case DataSub::Stand: stand(env, actor); break;
    default: notPortedAt(kDataArea, "0x4e86a0 sub-state", actor.subState);
    }
    if (!data.has(DataFields::kDone)) submitHit(actor, data);
    if (actor.anim == actor.prevAnim) {
        animStep(actor); // exe 0x5e1e80
        animPose(actor);
    } else {
        setTexturePageCell(actor.texturePage, animCells[actor.anim]);
        animSet(actor, actor.anim, 0); // exe 0x5e1c30
        animPose(actor);
    }
    int16_t head[3];
    bonePointAhead(actor, kHeadBone, kHeadReach, head);
    // the exe copies 8 bytes from its 6-byte local: the last two (+0x16 of the puff / flame) are stack garbage and
    // not written here
    if (sprayed) {
        if (const uint32_t e = allocActorA0(kActorStageClass)) { // a smoke puff
            EffectActor& puff = effectActorAt(e);
            puff.flags = kActorLive;
            puff.type = kFireEffectType;
            puff.subtype = uint8_t(FireEffectKind::SmokePuff);
            puff.pos.x = head[0];
            puff.pos.y = head[1];
            puff.pos.z = head[2];
        }
    }
    if (data.has(DataFields::kBurning) && data.flame != 0) { // the flame on his head
        EffectActor& flame = effectActorAt(data.flame);
        flame.pos.x = head[0];
        flame.pos.y = head[1];
        flame.pos.z = head[2];
        DataFlameParams(flame).size = uint16_t(actor.hitPoints >> 6);
    }
    // exe 0x5f20f0(actor, 0x18, 0x100, 0) while drawn: a shadow list entry (render output)
}

// ---- the result scene ----------------------------------------------------------------------------------------------
// [0x8964c0]
enum class SceneState : uint8_t { Spawn = 0, Act = 1, Free = 2 }; // exe 0x4ea240 / 0x4ea2f0 / 0x468fa0
// the scene's sub-state routines by variant (exe data 0x8964d4: a pointer per variant to its table of routines, the
// named routines of engine/routines.csv)
struct SceneSubTables : DataRecord {
    static constexpr size_t kVariants = 2, kSubStates = 2;
    static constexpr uint32_t kBytes = kVariants * 4;
    std::array<RoutineTable<kSubStates>, kVariants> variants{};
    // the routine of the variant's sub-state (past a table: none)
    RoutineId idAt(size_t variant, size_t sub) const { return variant < kVariants ? variants[variant].idAt(sub) : RoutineId::None; }
    uint32_t pointerAt(size_t variant, size_t sub) const {
        return variant < kVariants ? variants[variant].pointerAt(sub) : 0;
    }
    static SceneSubTables load(const TableReader& r) {
        SceneSubTables t;
        for (size_t v = 0; v < kVariants; ++v)
            t.variants[v] = RoutineTable<kSubStates>::load(r.follow(v * 4, RoutineTable<kSubStates>::kBytes));
        return t;
    }
};
const DataTable<SceneSubTables> sceneSubTables{TableId::BurningDataSceneSubTables};

// +0xa of the result scene's hop: the first hop, hopping
enum class SceneHopStep : uint8_t { Start = 0, Hop = 1 };

void sceneHop(AnimatedActor& actor) { // exe 0x4ea360: hops around the player within kSceneRadius
    if (SceneHopStep(actor.step) == SceneHopStep::Start) {
        actor.airborne = kAirborneFalling;
        actor.velocity.y = kSceneHop;
        actor.step = uint8_t(SceneHopStep::Hop);
    } else if (SceneHopStep(actor.step) != SceneHopStep::Hop) {
        return;
    }
    if (actor.airborne != 0) actor.velocity.y = wrap16(actor.velocity.y + actor.acceleration.y);
    integrateVelocity(actor, 0, actor.velocity.y, actor.velocity.z);
    actor.rotation.y = int16_t(angleOf(actor.pos, player().pos) - kQuarterTurn);
    if (planarDistance(player().pos, actor.pos) > kSceneRadius)
        pullToRadius(actor, player().pos.x.whole, player().pos.z.whole, kSceneRadiusPull);
    if (actor.airborne == 0) {
        actor.velocity.y = kSceneHop;
        actor.airborne = kAirborneFalling;
        return;
    }
    if (int32_t(actor.pos.y) >= 0) {
        actor.pos.y = 0;
        actor.velocity.y = 0;
        actor.airborne = 0;
    }
}
void sceneBurnt(AnimatedActor& actor) { // exe 0x4ea470: burnt, a smoke puff every 8 frames
    actor.tint = kBurntTint;
    if (gameState().frame & 7) return;
    const uint32_t e = allocActorA0(kActorStageClass); // exe 0x5e13a0
    if (e == 0) return;
    EffectActor& puff = effectActorAt(e);
    puff.flags = kActorLive;
    puff.type = kFireEffectType;
    puff.subtype = uint8_t(FireEffectKind::SmokePuff);
    puff.variant = 3;
    const uint32_t r = rngStep();
    puff.pos.x = wrap16(actor.pos.x.whole - int(r & 0x1f) + 0x10);
    puff.pos.y = wrap16(actor.pos.y.whole - int((r >> 5) & 7));
    puff.pos.z = wrap16(actor.pos.z.whole - int((r >> 8) & 0x1f) + 0x10);
}

} // namespace

void burningDataUpdate(const PlayerEnv& env, uint32_t address) {
    AnimatedActor& actor = animatedActorAt(address);
    DataFields& data = classState<DataFields>(actor);
    FireMissionPointers& pointers = fireMissionPointers();
    pointers.missionBlock = kFireMissionBlock;
    pointers.dataBlock = slotIdentityWord(actor) + kDataBlock;
    actor.prevAnim = actor.anim;
    actor.savePreviousPose();
    actor.savePreviousContact();
    if (data.has(DataFields::kBurning)) { // his colour flickers while he burns
        const uint32_t r = rngStep();
        const uint32_t v = (((r & 0xf) >> 1) + 0x10) * 0x20;
        actor.tint = uint16_t(((v & 0xff00) | ((v & 0xff) | ((r & 0xf) + 0x10))) | 0x4000);
    } else {
        actor.tint = kTintNeutral;
    }
    switch (DataState(actor.state)) {
    case DataState::Spawn: spawn(actor, data); break;
    case DataState::Active: active(env, actor, data); break;
    case DataState::Free: freeActor(actor); break; // exe 0x468fa0 -> 0x5e1490
    default: notPortedAt(kDataArea, "0x4e8530 state", actor.state);
    }
}

void resultSceneDataUpdate(const PlayerEnv&, uint32_t address) {
    AnimatedActor& actor = animatedActorAt(address);
    fireMissionPointers().sceneDataBlock = slotIdentityWord(actor) + kDataBlock;
    actor.prevAnim = actor.anim;
    actor.tint = kTintNeutral;
    actor.savePreviousPose();
    actor.savePreviousContact();
    switch (SceneState(actor.state)) {
    case SceneState::Spawn: // exe 0x4ea240
        if (!bindActorModel(actor, 0)) {
            freeActor(actor);
            return;
        }
        actor.flags |= kActorDrawn;
        actor.velocity.setZSpare(0, 0);
        actor.rotation.z = 0;
        actor.rotation.x = 0;
        actor.velocity.setXY(0, 0);
        actor.acceleration.y = kGravity;
        actor.velocity.z = kWalkSpeed;
        actor.actorHit = 0;
        actor.platform = nullptr;
        classState<DataFields>(actor).clear();
        if (actor.variant == 0) { // the success: he hops
            setTexturePageCell(actor.texturePage, 3);
            setAnim(actor, DataAnim::Walk);
        } else { // the failure: burnt
            setTexturePageCell(actor.texturePage, 0);
            setAnim(actor, DataAnim::Burnt);
        }
        actor.prevAnim = kNoAnim;
        animSet(actor, actor.anim, 0);
        animPose(actor);
        actor.enterState(uint8_t(SceneState::Act));
        return;
    case SceneState::Act: { // exe 0x4ea2f0
        const RoutineId routine = sceneSubTables->idAt(actor.variant, actor.subState);
        if (routine == RoutineId::BurningDataSceneHop)
            sceneHop(actor);
        else if (routine == RoutineId::BurningDataSceneBurnt)
            sceneBurnt(actor);
        else
            notPortedAt(kDataArea, "0x4ea1d0 sub-state routine", sceneSubTables->pointerAt(actor.variant, actor.subState));
        if (actor.anim != actor.prevAnim)
            animSet(actor, actor.anim, 0);
        else
            animStep(actor);
        animPose(actor);
        // exe 0x5f20f0(actor, 0x18, 0x100, 0) while drawn: a shadow list entry (render output)
        return;
    }
    case SceneState::Free: freeActor(actor); return;
    default: notPortedAt(kDataArea, "0x4ea1d0 state", actor.state);
    }
}

} // namespace od::port
