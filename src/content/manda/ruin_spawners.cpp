// The Manda ruin's enemy spawners (pool 0xe0 type 0xa): ST12 exe 0x41b990, ST13 exe 0x41c070 (all variants), ST14 exe
// 0x41c910 (see ruin_spawners.hpp).
#include "content/manda/ruin_spawners.hpp"
#include "content/manda/ruin_spawners_state.hpp"

#include "content/manda/hopping_drone.hpp"
#include "content/manda/manda_guardian.hpp"
#include "content/manda/st14_enemy.hpp"
#include "engine/data_source.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/angles.hpp"
#include "game/encounter.hpp"
#include "game/event_state.hpp"
#include "game/vectors.hpp"
#include "game/game_state.hpp"
#include "game/global_class_types.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/script_state.hpp"
#include "game/story_flags.hpp"

namespace od::port {

using namespace ruin_spawners_state;

namespace {

constexpr const char* kEnemySpawnerPort = "enemy spawner port"; // the area of its port-gap errors

enum class SpawnerState : uint8_t {
    Init = 0, // the class bytes cleared (and ST13's set-up by variant)
    Run = 1,  // the variant's routine every tick
};


// the story flags the spawners read (their meaning as these spawners use them; hypotheses)
constexpr uint32_t kFlagRuinLatePhase = 0x5b6;  // the ruin's later visit: the respawns, the emitters, more drones
constexpr uint32_t kFlagSt12Retired = 0x3e4;    // ST12's first spawner no longer runs
constexpr uint32_t kFlagSt12FirstGroup = 0x5a0; // ST12's group 0 is spawned
constexpr uint32_t kFlagSt12GroupCleared = 7;   // ST12's zone group was cleared
constexpr uint32_t kFlagSt12DropDone = 0x5bd;   // ST12's drop around the player has run
constexpr uint32_t kFlagSt13DropDone = 0x5be;   // ST13's drop around the player has run
constexpr uint32_t kFlagSt13ClutHeld = 0x242;   // ST13 sub 4's colour animation not started
constexpr uint8_t kStoryLateRuinStages = 4;     // the story's progress up to which ST12 spawns its first group

// the actor types they spawn (pool 0x20)
constexpr uint8_t kWalkerType = 0x1b;     // content/manda/st12_14 (exe 0x47e430)
constexpr uint8_t kExamHopperType = 0x2a; // content/manda/exam_enemies_a (exe 0x48e750)
constexpr uint8_t kRespawnVariant = 1, kRespawnSpawnByte = 1;
constexpr uint8_t kDropHopperVariant = 3;
constexpr uint8_t kDropDroneVariant = 0x10;
constexpr uint8_t kEntryDroneBits = 0x61; // a lone drone's variant bits (with a partner and its flame)
constexpr uint8_t kPairDroneBit = 0x01;   // the survivors' variant bit

// A spawn point of the exe's data (8 bytes: s16 x, y, z, yaw)
struct SpawnPoint : DataRecord {
    int16_t x = 0;
    int16_t y = 0;
    int16_t z = 0;
    int16_t yaw = 0;
    static SpawnPoint load(const TableReader& r) {
        SpawnPoint rec;
        rec.x = r.s16(0);
        rec.y = r.s16(2);
        rec.z = r.s16(4);
        rec.yaw = r.s16(6);
        return rec;
    }
};

// "around the player" (exe 0x41bea0 / 0x41c740): two RNG steps; the point the distance -table[r & 0xf] along the yaw
// (r & 0x1ff) + the player's yaw - 0x100 from the player (exe 0x5e38d0), clamped to the box (16.16 x / z)
void aroundPlayer(const DataTable<ScalarTable<int16_t, 0x10>>& distances, int32_t xMin, int32_t xMax, int32_t zMin, int32_t zMax,
                  int32_t& x, int32_t& z) {
    const Player& p = player();
    int32_t px = p.pos.x, pz = p.pos.z;
    rngStep();
    const uint32_t r = rngStep();
    const int32_t d = -int32_t(distances[r & 0xf]);
    int32_t out[3];
    rotateByYaw(int32_t(r & 0x1ff) + int32_t(p.rotation.y) - 0x100, 0, 0, d, out);
    px += out[0];
    pz += out[2];
    if (px < xMin) px = xMin;
    else if (px > xMax) px = xMax;
    if (pz < zMin) pz = zMin;
    else if (pz > zMax) pz = zMax;
    x = px;
    z = pz;
}

// exe 0x41bbe0(point, id) / exe 0x41ca30(point, id): a hovering reaverbot (pool 0x20 type 3, variant 1, spawn byte 1)
// at the point's x / z and the height y, its id un-defeated and tracked in the script slot
void spawnHoverReaverbot(const SpawnPoint& point, uint32_t id, uint8_t reverseCircle, uint8_t range, int16_t y) {
    const uint32_t address = allocActor20();
    if (address == 0) return;
    AnimatedActor& a = animatedActorAt(address);
    auto&& params = HoverReaverbotParams(a);
    a.variant = kRespawnVariant;
    a.spawnByte = kRespawnSpawnByte;
    a.flags = kActorLiveDrawn;
    a.loopFlags |= kActorSweepGroup2;
    params.reverseCircle = reverseCircle;
    params.range = int8_t(range);
    a.type = kHoverReaverbotType;
    a.subtype = 0;
    a.pos.x.whole = point.x;
    a.pos.y.whole = y;
    a.pos.z.whole = point.z;
    enemyClearDefeated(id);
    encounterTrackInScriptSlot(a, uint16_t(id));
}

// exe 0x41bdd0(point): a walker (pool 0x20 type 0x1b, variant 0) at the point, its yaw the point's
void spawnWalker(const SpawnPoint& point) {
    const uint32_t address = allocActor20();
    if (address == 0) return;
    AnimatedActor& a = animatedActorAt(address);
    a.subtype = 0;
    a.variant = 0;
    a.flags = kActorLiveDrawn;
    a.loopFlags |= kActorSweepGroup2;
    a.type = kWalkerType;
    a.pos.x = int32_t(uint32_t(int32_t(point.x)) << 16);
    a.pos.y = int32_t(uint32_t(int32_t(point.y)) << 16);
    a.pos.z = int32_t(uint32_t(int32_t(point.z)) << 16);
    a.rotation.y = point.yaw;
}

// ---- ST12 (exe 0x41b990) -------------------------------------------------------------------------------------------
// its variants [0x88a5b8 + variant * 4]
enum class St12Variant : uint8_t {
    Respawns = 0, // exe 0x41ba00
    Emitters = 1, // exe 0x41bc50
    None2 = 2,    // exe 0x52ef00 (the empty routine)
    Drop = 3,     // exe 0x41be20
    None4 = 4,
    None5 = 5,
};
// variant 0's sub-states [0x88a5d0]
enum class RespawnSub : uint8_t {
    Start = 0,     // exe 0x41ba20
    WaitGroup = 1, // exe 0x41bac0
    Respawn = 2,   // exe 0x41bae0
};
const DataTable<RecordTable<SpawnPoint, 4, 8>> st12RespawnPoints{TableId::RuinSpawnersSt12RespawnPoints};
constexpr int16_t kSt12RespawnHeight = -0xa00;

// variant 0: the zone groups by the story, then (the later visit) ids 1..3 respawned at one of 4 points, twice
void st12Respawns(ControllerActor& c) {
    auto&& f = classState<RespawnFields>(c);
    switch (RespawnSub(c.subState)) {
    case RespawnSub::Start:
        if (!storyFlag(kFlagRuinLatePhase) && gameState().story <= kStoryLateRuinStages) {
            if (storyFlag(kFlagSt12Retired)) {
                freeActor(c);
                return;
            }
            if (storyFlag(kFlagSt12FirstGroup)) encounterSpawnGroupZoneSlot(0);
            if (storyFlag(kFlagSt12GroupCleared)) {
                freeActor(c);
                return;
            }
            c.subState = uint8_t(RespawnSub::WaitGroup);
            return;
        }
        enemyClearDefeated(1);
        enemyClearDefeated(2);
        enemyClearDefeated(3);
        encounterSpawnGroupZoneSlot(4);
        f.respawnsLeft = 2;
        c.subState = uint8_t(RespawnSub::Respawn);
        return;
    case RespawnSub::WaitGroup: // the zone slot's group gone: the flag, freed
        if (encounterZoneAlive() == 0) {
            setStoryFlag(kFlagSt12GroupCleared);
            freeActor(c);
        }
        return;
    case RespawnSub::Respawn:
        if (scriptState().flags != 0 || eventState().active != 0) return;
        for (uint32_t id = 1; id <= 3; ++id) {
            if (!enemyDefeated(id)) continue;
            const uint32_t r = rngStep();
            spawnHoverReaverbot(st12RespawnPoints[r & 3], id, 0, 2, kSt12RespawnHeight);
            const uint8_t n = uint8_t(f.respawnsLeft - 1u);
            f.respawnsLeft = n;
            if (n == 0) {
                freeActor(c);
                return;
            }
        }
        return;
    default: notPortedAt(kEnemySpawnerPort, "ST12 spawner variant 0 sub", c.subState);
    }
}

enum class EmitterSub : uint8_t { Start = 0, Run = 1 };
const DataTable<RecordTable<SpawnPoint, 12, 8>> emitterPoints{TableId::RuinSpawnersEmitterPoints}; // emitter e's 4 points from e * 4 (half + side * 2)
constexpr uint8_t kEmitterTicks = 0x1e;
constexpr int16_t kEmitterSideX = 0x300;
constexpr uint8_t kEmitterGroupStarted = 1; // the step: the encounter group spawned instead of emitter 0

// one tick of emitter e: its timer down; at 0 a walker, the timer re-armed, the other half next
void emitterTick(EmitterFields& f, size_t e) {
    const uint8_t t = uint8_t(f.timers[e] - 1u);
    f.timers[e] = t;
    if (t != 0) return;
    spawnWalker(emitterPoints[e * 4 + uint32_t(f.halves[e]) + uint32_t(f.side) * 2]);
    f.timers[e] = kEmitterTicks;
    f.halves[e] = uint8_t(f.halves[e] ^ 1u);
}
// variant 1: the emitters (the first only on the later visit; before it, encounter group 5 once instead)
void st12Emitters(ControllerActor& c) {
    auto&& f = classState<EmitterFields>(c);
    if (scriptState().flags != 0) return;
    const uint8_t sub = c.subState;
    if (sub == uint8_t(EmitterSub::Start)) {
        f.side = player().pos.x.whole > kEmitterSideX ? 0 : 1;
        f.timers[0] = kEmitterTicks;
        f.timers[1] = kEmitterTicks;
        f.timers[2] = kEmitterTicks;
        f.halves[0] = uint8_t(rngStep() & 1);
        f.halves[1] = uint8_t(rngStep() & 1);
        f.halves[2] = uint8_t(rngStep() & 1);
        c.subState = uint8_t(EmitterSub::Run);
        c.restartStep();
    }
    if (sub == uint8_t(EmitterSub::Start) || sub == uint8_t(EmitterSub::Run)) {
        if (!storyFlag(kFlagRuinLatePhase)) {
            if (c.step == 0) {
                c.step = kEmitterGroupStarted;
                encounterSpawnGroupZoneSlot(5);
            }
        } else {
            emitterTick(f, 0);
        }
        emitterTick(f, 2);
    }
    emitterTick(f, 1);
}

enum class DropSub : uint8_t {
    Start = 0, // exe 0x41be40
    WaitZ = 1, // exe 0x41be80
    Drop = 2,  // exe 0x41bea0
};
enum class DropStep : uint8_t { Start = 0, Spawn = 1, Wait = 2, Done = 3 };
const DataTable<ScalarTable<int16_t, 0x10>> st12DropDistances{TableId::RuinSpawnersSt12DropDistances};
constexpr uint8_t kSt12DropCount = 8;
constexpr uint32_t kSt12DropFirstId = 0x28; // the ids 0x20..0x27 (0x28 - the count left)
constexpr int32_t kSt12DropY = int32_t(0xfa800000u);
constexpr int16_t kSt12DropTriggerZ = 0x280;

// exe 0x41be20 (variant 3): group 2, then 8 exam hoppers dropped around the player once it is below z 0x280; the later
// visits group 3 only
void st12Drop(ControllerActor& c) {
    auto&& f = classState<DropFields>(c);
    switch (DropSub(c.subState)) {
    case DropSub::Start:
        if (!storyFlag(kFlagSt12DropDone)) {
            encounterSpawnGroupZoneSlot(2);
            c.subState = uint8_t(DropSub::WaitZ);
        } else {
            encounterSpawnGroupZoneSlot(3);
            freeActor(c);
        }
        return;
    case DropSub::WaitZ:
        if (player().pos.z.whole < kSt12DropTriggerZ) {
            c.subState = uint8_t(DropSub::Drop);
            c.restartStep();
        }
        return;
    case DropSub::Drop: {
        const uint8_t s = c.step;
        if (s == uint8_t(DropStep::Start)) {
            f.count = kSt12DropCount;
            f.anchorX = player().pos.x.whole;
            f.anchorZ = 0;
            c.enterStep(uint8_t(DropStep::Spawn));
            setStoryFlag(kFlagSt12DropDone);
        }
        if (s == uint8_t(DropStep::Start) || s == uint8_t(DropStep::Spawn)) {
            int32_t x, z;
            aroundPlayer(st12DropDistances, int32_t(0xfe000000u), 0x2000000, int32_t(0xfe000000u), 0x2000000, x, z);
            if (const uint32_t address = allocActor20()) {
                AnimatedActor& e = animatedActorAt(address);
                e.flags = kActorLiveDrawn;
                e.loopFlags |= kActorSweepGroup2;
                e.type = kExamHopperType;
                e.subtype = 0;
                e.variant = kDropHopperVariant;
                e.pos.x = x;
                e.pos.y = kSt12DropY;
                e.pos.z = z;
                e.rotation.y = int16_t(rngStep() & kAngleMask);
                encounterTrackInScriptSlot(e, uint16_t(kSt12DropFirstId - f.count));
            }
            f.delay = uint8_t((rngStep() & 1) + 1u);
            c.enterStep(uint8_t(DropStep::Wait));
            const uint8_t n = uint8_t(f.count - 1u);
            f.count = n;
            if (n == 0) {
                freeActor(c);
                return;
            }
        } else if (s != uint8_t(DropStep::Wait)) {
            return;
        }
        const uint8_t t = uint8_t(f.delay - 1u); // exe 0x41c051
        f.delay = t;
        if (t == 0) c.enterStep(uint8_t(DropStep::Spawn));
        return;
    }
    default: notPortedAt(kEnemySpawnerPort, "ST12 spawner variant 3 sub", c.subState);
    }
}

// the variant of ST12's state 1 (exe 0x41b9e0)
void st12Variant(ControllerActor& c) {
    switch (St12Variant(c.variant)) {
    case St12Variant::Respawns: st12Respawns(c); return;
    case St12Variant::Emitters: st12Emitters(c); return;
    case St12Variant::Drop: st12Drop(c); return;
    case St12Variant::None2:
    case St12Variant::None4:
    case St12Variant::None5: return; // exe 0x52ef00
    default: notPortedAt(kEnemySpawnerPort, "ST12 spawner variant", c.variant);
    }
}

// exe 0x41b990: states [0x88a5b0]: 0 = 0x41b9b0 (the class bytes cleared, state 1, the variant at once), 1 = 0x41b9e0
void st12Spawner(ControllerActor& c) {
    switch (SpawnerState(c.state)) {
    case SpawnerState::Init:
        c.clearClassBlock();
        c.enterState(uint8_t(SpawnerState::Run));
        st12Variant(c);
        return;
    case SpawnerState::Run: st12Variant(c); return;
    default: notPortedAt(kEnemySpawnerPort, "ST12 spawner 0x41b990 state", c.state);
    }
}

// ---- ST13 (exe 0x41c070) -------------------------------------------------------------------------------------------
enum class St13Variant : uint8_t {
    Hoppers = 0,   // exe 0x41c240: the guardian's hoppers
    Drones = 1,    // exe 0x41c460: the drones' partners tracked
    ZoneDrop = 2,  // exe 0x41c650
    Group = 3,     // freed at its init (its state 1: exe 0x52ef00)
    ClutRoom = 4,  // freed at its init
    LateGroup = 5, // freed at its init
};

// A drone entry of the exe's data (0x10 bytes): the point and yaw, the survivors' offset, the hop range, the variant,
// the two drones' ids
struct DroneEntry : DataRecord {
    int16_t x = 0, y = 0, z = 0, yaw = 0;
    int16_t dx = 0, dz = 0;
    int8_t hopRange = 0;
    uint8_t variant = 0;
    uint8_t firstId = 0;
    uint8_t secondId = 0;
    static DroneEntry load(const TableReader& r) {
        DroneEntry rec;
        rec.x = r.s16(0);
        rec.y = r.s16(2);
        rec.z = r.s16(4);
        rec.yaw = r.s16(6);
        rec.dx = r.s16(8);
        rec.dz = r.s16(0xa);
        rec.hopRange = r.s8(0xc);
        rec.variant = r.u8(0xd);
        rec.firstId = r.u8(0xe);
        rec.secondId = r.u8(0xf);
        return rec;
    }
};
const DataTable<RecordTable<DroneEntry, 2, 0x10>> earlyDroneEntries{TableId::RuinSpawnersEarlyDroneEntries};
const DataTable<RecordTable<DroneEntry, 3, 0x10>> lateDroneEntries{TableId::RuinSpawnersLateDroneEntries};
uint32_t droneEntryCount(bool late) { return late ? 3 : 2; }
DroneEntry droneEntry(bool late, uint32_t i) { return late ? lateDroneEntries[i] : earlyDroneEntries[i]; }

// exe 0x41c5e0(point, variant, hop range, id): a hopping drone at the point (x, y, z, yaw), tracked with the id
AnimatedActor* spawnDrone(const int16_t point[4], uint8_t variant, int16_t hopRange, uint16_t id) {
    const uint32_t address = allocActor20();
    if (!address) return nullptr;
    AnimatedActor& a = animatedActorAt(address);
    a.flags = kActorLiveDrawn;
    a.variant = variant;
    a.loopFlags |= kActorSweepGroup2;
    a.type = kHoppingDroneType;
    a.subtype = kHoppingDroneSub;
    a.pos.x.whole = point[0];
    a.pos.y.whole = point[1];
    a.pos.z.whole = point[2];
    a.rotation.y = point[3];
    HoppingDroneParams(a).hopRange = hopRange;
    encounterTrackInScriptSlot(a, id);
    return &a;
}

// exe 0x41c510(entry): neither drone defeated: one drone (it spawns its partner), its slot; else the survivors one by
// one at the point + the offset, 0
uint8_t spawnDroneEntry(const DroneEntry& e) {
    const uint8_t v = e.variant;
    const bool secondGone = enemyDefeated(e.secondId), firstGone = enemyDefeated(e.firstId);
    if (!firstGone && !secondGone) {
        const int16_t point[4] = {e.x, e.y, e.z, e.yaw};
        const AnimatedActor* d = spawnDrone(point, uint8_t(v | kEntryDroneBits), int16_t(e.hopRange), e.secondId);
        if (!d) notPorted(kEnemySpawnerPort, "exe 0x41c510 with a full pool 0x20 (the exe reads address 2)");
        return d->slot;
    }
    const int16_t point[4] = {int16_t(uint16_t(e.x + e.dx)), e.y, int16_t(uint16_t(e.z + e.dz)), e.yaw};
    if (!firstGone) spawnDrone(point, uint8_t(v | kPairDroneBit), int16_t(e.hopRange), e.firstId);
    if (!secondGone) spawnDrone(point, uint8_t(v | kPairDroneBit), int16_t(e.hopRange), e.secondId);
    return 0;
}

enum class HopperSetSub : uint8_t {
    Watch = 0,       // exe 0x41c327
    RefillStart = 1, // exe 0x41c293
    Refill = 2,      // exe 0x41c29f
};
// A hopper's point (s16 x / z), four per set
struct HopperPoint : DataRecord {
    int16_t x = 0;
    int16_t z = 0;
    static HopperPoint load(const TableReader& r) {
        HopperPoint rec;
        rec.x = r.s16(0);
        rec.z = r.s16(2);
        return rec;
    }
};
const DataTable<RecordTable<HopperPoint, 4, 4>> hopperPoints{TableId::RuinSpawnersHopperPoints};
const DataTable<ScalarTable<uint8_t, 0x10>> hopperRefillDelays{TableId::RuinSpawnersHopperRefillDelays};
constexpr uint8_t kSlotBias = 0x1f;            // a slot byte holds the slot id - 0x1f (the index + 1)
constexpr uint16_t kGroupHoppersCalled = 0x20; // the guardian's group mark: its hoppers are called
constexpr uint16_t kGroupHoppersGone = 0x40;   // ... marked by the spawner: none is left
constexpr int16_t kHopperY = -0x480;
constexpr uint8_t kRefillFirstCorner = 3;      // the refill one by one: corners 3..0
constexpr uint8_t kGuardianClass = 0x20, kGuardianType = 0x24;

// exe 0x41c380(x, z, guardian, corner): an untracked guardian hopper at x / -0x480 / z, a random yaw (the RNG steps
// only when the pool had room)
AnimatedActor* spawnGuardianHopper(int16_t x, int16_t z, uint8_t guardian, uint8_t corner) {
    const uint32_t address = allocActor20();
    if (address == 0) return nullptr;
    AnimatedActor& a = animatedActorAt(address);
    auto&& params = MandaHopperParams(a);
    a.subtype = 0;
    a.variant = 0;
    a.loopFlags |= kActorSweepGroup2;
    params.corner = int8_t(corner);
    a.flags = kActorLive;
    params.guardian = int8_t(guardian);
    a.type = kMandaHopperType;
    a.pos.x.whole = x;
    a.pos.y.whole = kHopperY;
    a.pos.z.whole = z;
    a.rotation.y = int16_t((rngStep() & 0x1f) << 7);
    return &a;
}
// a hopper's slot byte (its slot id - 0x1f)
uint8_t hopperSlotByte(const AnimatedActor& h) { return uint8_t(h.slot - kSlotBias); }

// exe 0x41c3f0(set, guardian): for k = 3..0 without a hopper alive, a hopper at point 3 - k of the set
void refillHoppers(HopperSetFields& f, uint8_t guardian) {
    uint32_t point = uint32_t(f.pointSet) * 4;
    const uint32_t mask = f.aliveMask;
    for (uint32_t k = 3;; --k) {
        if (!((mask >> k) & 1)) {
            const HopperPoint p = hopperPoints[point];
            if (const AnimatedActor* h = spawnGuardianHopper(p.x, p.z, guardian, uint8_t(k)))
                f.slots[k] = hopperSlotByte(*h);
        }
        ++point;
        if (k == 0) break;
    }
}

// exe 0x41c240 (variant 0): the guardian's four hoppers -- this tick's alive mask (the animated pool's live mask);
// while the guardian calls them, the missing refilled (none alive: the refill one by one); none alive and not called:
// the group marked
void st13Hoppers(ControllerActor& c) {
    auto&& f = classState<HopperSetFields>(c);
    uint32_t mask = 0, count = 0;
    const uint32_t live = actorSystem().animatedLive;
    for (uint32_t k = 3;; --k) {
        const uint8_t s = f.slots[k];
        if (s != 0 && int32_t(live << ((uint32_t(s) - 1u) & 31)) < 0) {
            mask |= 1u << k;
            ++count;
        }
        if (k == 0) break;
    }
    switch (HopperSetSub(c.subState)) {
    case HopperSetSub::Watch:
        f.aliveMask = uint8_t(mask);
        f.aliveCount = uint8_t(count);
        if (enemyGroupMarks(int32_t(f.group), EnemyGroupMarks::Either) & kGroupHoppersCalled) {
            if (count == 0) {
                c.subState = uint8_t(HopperSetSub::RefillStart);
                return;
            }
            if (count < 4) refillHoppers(f, uint8_t(f.guardian));
        } else if (count == 0) {
            enemyGroupMark(int32_t(f.group), kGroupHoppersGone);
        }
        return;
    case HopperSetSub::RefillStart:
        f.delay = 1;
        c.subState = uint8_t(HopperSetSub::Refill);
        c.step = kRefillFirstCorner; // the step: the corner refilled next
        [[fallthrough]];
    case HopperSetSub::Refill: {
        const uint8_t t = uint8_t(f.delay - 1u);
        f.delay = t;
        if (t != 0) return;
        const uint32_t k = c.step;
        const HopperPoint p = hopperPoints[k + uint32_t(f.pointSet) * 4];
        if (const AnimatedActor* h = spawnGuardianHopper(p.x, p.z, uint8_t(f.guardian), uint8_t(k)))
            f.slots[k] = hopperSlotByte(*h);
        if (c.step == 0) c.subState = uint8_t(HopperSetSub::Watch);
        else --c.step;
        f.delay = hopperRefillDelays[rngStep() & 0xf];
        return;
    }
    default: return;
    }
}

// exe 0x41c460's unchecked partner read: the address 0xa6e3a0 + (the signed byte at drone +0xf) * 0x16c, handed to exe 0x5e6500 (which
// reads its slot byte +2). The exe checks no range: the drone writes +0xf only when its partner was allocated, so the
// byte can be whatever the slot's last actor left, and for an index outside 0..0x1f the address lies before or past
// the animated pool (the slot bytes 0xa62da2..0xa79836, inside the exe's .data: always mapped), whose bytes are read as an
// actor header as they are. The closed set of those header-layout views, one per s8 value -128..127.
constexpr int32_t kPartnerIndexBias = 128;
using DronePartnerRead =
    ActorPoolOf<ActorHeader, 256, kAnimatedPool - uint32_t(kPartnerIndexBias) * AnimatedPool::stride(),
                AnimatedPool::stride()>;
const ActorHeader& dronePartnerRead(int8_t index) {
    static DronePartnerRead views;
    return views[size_t(int32_t(index) + kPartnerIndexBias)];
}

// exe 0x41c460 (variant 1): the lone drones' partners tracked with the entries' second ids, then freed
void st13Drones(ControllerActor& c) {
    auto&& f = classState<DroneSetFields>(c);
    const bool late = storyFlag(kFlagRuinLatePhase);
    const uint32_t n = droneEntryCount(late);
    for (int i = int(n) - 1; i >= 0; --i) {
        const uint8_t s = f.drones[size_t(i)];
        if (s == 0) continue;
        const AnimatedActor& d = animatedPool()[size_t(s) - kAnimatedClass];
        // the partner (the drone's +0xf: its animated pool index, unchecked) is read only with a script slot, where
        // exe 0x5e6500 reads it
        if ((d.flags & kActorLive) && d.type == kHoppingDroneType && encounterHasScriptSlot())
            encounterTrackInScriptSlot(dronePartnerRead(HoppingDroneParams(d).partner),
                                       droneEntry(late, uint32_t(i)).secondId);
    }
    freeActor(c);
}

// variant 2's sub-states [0x88a730]
enum class ZoneDropSub : uint8_t {
    WaitZone = 0, // exe 0x41c670
    Group = 1,    // exe 0x41c6c0
    WaitX = 2,    // exe 0x41c720
    Drop = 3,     // exe 0x41c740
};
const DataTable<ScalarTable<int16_t, 0x10>> st13DropDistances{TableId::RuinSpawnersSt13DropDistances};
constexpr uint8_t kSt13DropCount = 6;
constexpr uint32_t kSt13DropFirstId = 0x3f; // the ids 0x39..0x3e (0x3f - the count left)
constexpr int32_t kSt13DropY = int32_t(0xfb800000u);
constexpr int16_t kSt13DropTriggerX = -0x300;
constexpr uint8_t kZoneGroupSpawned = 1; // the step: the later group spawned, waiting for it to end

// exe 0x41c650 (variant 2): the player in one of the two zones -> group 3 (then, past x -0x300, 6 drones dropped around
// the player, once) or, after the drop, group 4; back to the zones when the zone slot's group ends
void st13ZoneDrop(ControllerActor& c) {
    auto&& f = classState<DropFields>(c);
    switch (ZoneDropSub(c.subState)) {
    case ZoneDropSub::WaitZone: {
        if (scriptState().flags != 0 || eventState().active != 0) return;
        const Fixed16Vec3& pos = player().pos;
        if (encounterInZone(pos, stateAddress(TableId::RuinSpawnersSt13DropZoneA), 0) || encounterInZone(pos, stateAddress(TableId::RuinSpawnersSt13DropZoneB), 0)) {
            c.subState = uint8_t(ZoneDropSub::Group);
            c.restartStep();
        }
        return;
    }
    case ZoneDropSub::Group:
        if (c.step == 0) {
            if (!storyFlag(kFlagSt13DropDone)) {
                encounterSpawnGroupZoneSlot(3);
                c.subState = uint8_t(ZoneDropSub::WaitX);
                c.restartStep();
            } else {
                encounterSpawnGroupZoneSlot(4);
                c.enterStep(kZoneGroupSpawned);
            }
        } else if (encounterZoneGroup() < 0) {
            c.subState = uint8_t(ZoneDropSub::WaitZone);
            c.restartStep();
        }
        return;
    case ZoneDropSub::WaitX:
        if (player().pos.x.whole > kSt13DropTriggerX) {
            c.subState = uint8_t(ZoneDropSub::Drop);
            c.restartStep();
        }
        return;
    case ZoneDropSub::Drop: {
        const uint8_t s = c.step;
        if (s == uint8_t(DropStep::Start)) {
            f.count = kSt13DropCount;
            c.enterStep(uint8_t(DropStep::Spawn));
            setStoryFlag(kFlagSt13DropDone);
        }
        if (s == uint8_t(DropStep::Start) || s == uint8_t(DropStep::Spawn)) {
            int32_t x, z;
            aroundPlayer(st13DropDistances, int32_t(0xfd000000u), 0x5000000, int32_t(0xfc000000u), 0x4000000, x, z);
            if (const uint32_t address = allocActor20()) {
                AnimatedActor& e = animatedActorAt(address);
                e.flags = kActorLiveDrawn;
                e.loopFlags |= kActorSweepGroup2;
                e.type = kHoppingDroneType;
                e.subtype = kHoppingDroneSub;
                e.variant = kDropDroneVariant;
                e.pos.x = x;
                e.pos.y = kSt13DropY;
                e.pos.z = z;
                e.rotation.y = int16_t(rngStep() & kAngleMask);
                encounterTrackInScriptSlot(e, uint16_t(kSt13DropFirstId - f.count));
            }
            f.delay = uint8_t((rngStep() & 1) + 1u);
            c.enterStep(uint8_t(DropStep::Wait));
            const uint8_t n = uint8_t(f.count - 1u);
            f.count = n;
            if (n == 0) {
                c.enterStep(uint8_t(DropStep::Done));
                return;
            }
        }
        if (s <= uint8_t(DropStep::Wait)) { // exe 0x41c8de
            const uint8_t t = uint8_t(f.delay - 1u);
            f.delay = t;
            if (t == 0) c.enterStep(uint8_t(DropStep::Spawn));
        }
        if (encounterZoneGroup() < 0) { // exe 0x41c8ee
            c.subState = uint8_t(ZoneDropSub::WaitZone);
            c.restartStep();
        }
        return;
    }
    default: notPortedAt(kEnemySpawnerPort, "ST13 spawner variant 2 sub", c.subState);
    }
}

// the CLUT animation of ST13 sub 4's later visit (pool 0xa0 global type 2): its sequence
constexpr uint8_t kSt13ClutSequence = 4;

// exe 0x41c090 (state 0): the class bytes cleared, the variant's set-up (jump table 0x41c200), state 1
void st13Init(ControllerActor& c) {
    c.clearClassBlock();
    switch (St13Variant(c.variant)) {
    case St13Variant::Hoppers: {
        auto&& f = classState<HopperSetFields>(c);
        f.group = uint8_t(enemyGroupFind(kGuardianClass, kGuardianType, 0));
        f.pointSet = 0;
        break;
    }
    case St13Variant::Drones: {
        auto&& f = classState<DroneSetFields>(c);
        const bool late = storyFlag(kFlagRuinLatePhase);
        const uint32_t n = droneEntryCount(late);
        for (uint32_t i = 0; i < n; ++i) f.drones[i] = spawnDroneEntry(droneEntry(late, i));
        break;
    }
    case St13Variant::Group:
        if (storyFlag(kFlagRuinLatePhase)) encounterSpawnGroupZoneSlot(0);
        freeActor(c);
        break;
    case St13Variant::ClutRoom:
        if (storyFlag(kFlagRuinLatePhase)) {
            encounterSpawnGroupZoneSlot(7);
            clearStoryFlag(kFlagSt13ClutHeld);
            if (const uint32_t address = allocActorA0(0)) { // exe 0x5e13b0
                EffectActor& e = effectActorAt(address);
                e.flags = kActorLive;
                e.type = typeByte(GlobalEffectType::ClutAnimation);
                e.loopFlags |= kActorSweepGroup2;
                e.variant = 0;
                e.params[0] = kSt13ClutSequence;
            }
        } else {
            encounterSpawnGroupZoneSlot(6);
            setStoryFlag(kFlagSt13ClutHeld);
        }
        freeActor(c);
        break;
    case St13Variant::LateGroup:
        encounterSpawnGroupZoneSlot(storyFlag(kFlagRuinLatePhase) ? 1 : 9);
        freeActor(c);
        break;
    default: break; // variant 2 and past 5: exe 0x41c11f
    }
    c.enterState(uint8_t(SpawnerState::Run));
}

// exe 0x41c070
void st13Spawner(ControllerActor& c) {
    switch (SpawnerState(c.state)) {
    case SpawnerState::Init: st13Init(c); return;
    case SpawnerState::Run: // exe 0x41c220 -> [0x88a710 + variant * 4]
        switch (St13Variant(c.variant)) {
        case St13Variant::Hoppers: st13Hoppers(c); return;
        case St13Variant::Drones: st13Drones(c); return;
        case St13Variant::ZoneDrop: st13ZoneDrop(c); return;
        case St13Variant::Group: return; // exe 0x52ef00 (variant 3 freed itself at its init)
        default: notPortedAt(kEnemySpawnerPort, "ST13 spawner variant", c.variant);
        }
    default: notPortedAt(kEnemySpawnerPort, "ST13 spawner 0x41c070 state", c.state);
    }
}

// ---- ST14 (exe 0x41c910) -------------------------------------------------------------------------------------------
enum class St14Variant : uint8_t {
    Respawns = 0, // exe 0x41c990
    None = 1,
};
const DataTable<RecordTable<SpawnPoint, 4, 8>> st14RespawnPointsA{TableId::RuinSpawnersSt14RespawnPointsA}; // id 8's
const DataTable<RecordTable<SpawnPoint, 4, 8>> st14RespawnPointsB{TableId::RuinSpawnersSt14RespawnPointsB}; // id 9's
constexpr int16_t kSt14RespawnHeight = -0x560;

// exe 0x41c910: states [0x88a768]: 0 = 0x41c930 (ids 8 / 9 un-defeated, group 1 in the script slot, 2 respawns),
// 1 = variants [0x88a770]: 0 = 0x41c990 (each defeated id 8 / 9 respawned at one of its 4 points; freed after the
// respawns), 1 = the empty routine
void st14Spawner(ControllerActor& c) {
    auto&& f = classState<St14RespawnFields>(c);
    switch (SpawnerState(c.state)) {
    case SpawnerState::Init:
        c.clearClassBlock();
        enemyClearDefeated(8);
        enemyClearDefeated(9);
        encounterSpawnGroupScriptSlot(1);
        f.respawnsLeft = 2;
        c.enterState(uint8_t(SpawnerState::Run));
        return;
    case SpawnerState::Run: {
        const St14Variant v = St14Variant(c.variant);
        if (v == St14Variant::None) return;
        if (v != St14Variant::Respawns) notPortedAt(kEnemySpawnerPort, "ST14 spawner variant", c.variant);
        if (enemyDefeated(8)) {
            const uint32_t r = rngStep();
            spawnHoverReaverbot(st14RespawnPointsA[r & 3], 8, 1, 6, kSt14RespawnHeight);
            const uint8_t n = uint8_t(f.respawnsLeft - 1u);
            f.respawnsLeft = n;
            if (n == 0) {
                freeActor(c);
                return;
            }
        }
        if (enemyDefeated(9)) {
            const uint32_t r = rngStep();
            spawnHoverReaverbot(st14RespawnPointsB[r & 3], 9, 1, 6, kSt14RespawnHeight);
            const uint8_t n = uint8_t(f.respawnsLeft - 1u);
            f.respawnsLeft = n;
            if (n == 0) freeActor(c);
        }
        return;
    }
    default: notPortedAt(kEnemySpawnerPort, "ST14 spawner 0x41c910 state", c.state);
    }
}

} // namespace

void spawnerSt12Update(const PlayerEnv&, uint32_t actor) { st12Spawner(controllerActorAt(actor)); }
void spawnerSt13Update(const PlayerEnv&, uint32_t actor) { st13Spawner(controllerActorAt(actor)); }
void spawnerSt14Update(const PlayerEnv&, uint32_t actor) { st14Spawner(controllerActorAt(actor)); }

} // namespace od::port
