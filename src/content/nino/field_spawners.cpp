// Nino Island's field enemy spawners (pool 0xe0 type 0xa): ST15 exe 0x41ca90, ST16 exe 0x41d320, ST17 exe 0x41dd70,
// ST18 exe 0x41e7e0, and the ST18 repair gauge's picture (see field_spawners.hpp). One routine per exe routine.
#include "content/nino/field_spawners.hpp"
#include "content/nino/stage_timer.hpp"
#include "content/nino/field_spawners_state.hpp"
#include "engine/data_source.hpp"
#include "game/loaded_data.hpp"
#include "game/actor_state.hpp"
#include "game/stage_smoke.hpp"

#include "content/nino/carrier_boat.hpp"
#include "content/nino/field_enemies.hpp"
#include "content/nino/mission_gunships.hpp"
#include "content/nino/st15_cls.hpp"
#include "content/nino/st16_crane.hpp"
#include "content/nino/st18_gunships.hpp"

#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actors.hpp"
#include "game/breakable_walls.hpp"
#include "game/distances.hpp"
#include "game/encounter.hpp"
#include "game/explosions.hpp"
#include "game/game_state.hpp"
#include "game/global_class_types.hpp"
#include "game/hud.hpp"
#include "game/lift_box.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/spawn.hpp"
#include "game/state_array.hpp"
#include "game/story_flags.hpp"
#include "game/visibility.hpp"

namespace od::port {

namespace {

using namespace field_spawners_state;

constexpr const char* kEnemySpawnerPort = "enemy spawner port"; // the area of its port-gap errors

// [0x88a898] / [0x88a9e8] / [0x88ab50] / [0x88abc8]
enum class SpawnerState : uint8_t { Init = 0, Run = 1 };

// ---- the spawned actors' parameters --------------------------------------------------------------------------------

// A walker / Birdbot (pool 0x20 type 0x2d sub 1, the stage's class): its mode, count, its owner's slot, its tracked id,
// its point
struct WalkerParams {
    explicit WalkerParams(const AnimatedActor& actor) : WalkerParams(const_cast<AnimatedActor&>(actor)) {}
    explicit WalkerParams(AnimatedActor& actor) : mode(actor.params[0]), count(actor.params[1]), owner(actor.params[2]), trackId(actor.params[3]), pointX(actor.velocity.spare), pointZ(actor.acceleration.spare) {}
    uint8_t& mode;
    uint8_t& count;
    uint8_t& owner;
    uint8_t& trackId;
    int16_t& pointX;
    int16_t& pointZ;
};
constexpr uint8_t kWalkerType = 0x2d, kWalkerSub = 1;
constexpr int kWalkerGone = 4; // the walker's state that frees it
// the lever (static type 1 variant 2, content/nino/bars_and_gate): its spawner's slot
struct LeverParams {
    explicit LeverParams(const ActorHeader& actor) : LeverParams(const_cast<ActorHeader&>(actor)) {}
    explicit LeverParams(ActorHeader& actor) : spawner(actor.params[0]) {}
    uint8_t& spawner;
};
constexpr uint8_t kLeverType = 1, kLeverVariant = 2;

// A spawn point of the exe's tables (8 bytes): x, y, z, yaw
struct SpawnPoint : DataRecord {
    int16_t x = 0;
    int16_t y = 0;
    int16_t z = 0;
    int16_t yaw = 0;
    static SpawnPoint load(const TableReader& r) {
        SpawnPoint rec;
        rec.x = r.s16(0x0);
        rec.y = r.s16(0x2);
        rec.z = r.s16(0x4);
        rec.yaw = r.s16(0x6);
        return rec;
    }
};
// A target record (exe data): its x / z
struct TargetRecord : DataRecord {
    static constexpr uint32_t kBytes = 0x6;
    int16_t x = 0; // +0x0
    int16_t z = 0; // +0x4
    template <class R> static TargetRecord load(const R& r) {
        TargetRecord v;
        v.x = r.template read<int16_t>(0x0);
        v.z = r.template read<int16_t>(0x4);
        return v;
    }
};
// An encounter record (exe data, 0x18 bytes): the spawn record and its tracked id
struct EncounterRecord : DataRecord {
    static constexpr uint32_t kBytes = 0x18;
    SpawnRecord record;
    uint8_t trackId = 0; // +0x14
    template <class R> static EncounterRecord load(const R& r) { return {{}, SpawnRecord::load(r), r.template read<uint8_t>(0x14)}; }
};

// the slot of an actor just spawned (the callers store it without a null test: the exe would read address 2; a port gap
// instead)
uint8_t spawnedSlot(uint32_t actor, const char* who) {
    if (actor == 0) notPorted(kEnemySpawnerPort, who);
    return actorHeaderAt(actor).slot;
}
// the head of the animated / static slot of a slot id by the exe's unchecked index arithmetic (a slot 0 reads below the
// pool, as in the exe)
AnimatedActor& animatedSlotHead(uint32_t slot) {
    return animatedPool().at(kAnimatedPool + (slot - kAnimatedClass) * AnimatedPool::stride());
}
StaticActor& staticSlotAt(uint32_t slot) { return staticPool().at(kStaticPool + (slot - kStaticClass) * StaticPool::stride()); }

constexpr uint8_t kTimerType = 0x1b;
void spawnTimer(uint16_t c) { // (the same writes appear inline in 0x41d190 / 0x41e6d0)
    const uint32_t e = allocActorE0();
    if (e == 0) return;
    ControllerActor& timer = controllerActorAt(e);
    auto&& params = StageTimerParams(controllerActorAt(e));
    timer.flags = kActorLive;
    timer.type = kTimerType;
    timer.variant = 0;
    params.value = c;
    params.limit = 0x7f;
    params.flag = -1;
}

// exe 0x41d290(point, variant, mode, count, row, target, id): a mission gunship (static type 0x1b) at the point, its
// target the record's, tracked
uint32_t spawnGunship(const SpawnPoint& point, uint8_t variant, uint8_t mode, uint8_t count, uint8_t row,
                      const TargetRecord& target, uint32_t id) {
    const uint32_t s = allocActor60(kActorStageClass);
    if (s == 0) return 0;
    StaticActor& ship = staticActorAt(s);
    auto&& params = MissionGunshipParams(staticActorAt(s));
    ship.variant = variant;
    ship.loopFlags |= kActorSweepGroup2;
    params.reinforcementRow = row;
    ship.flags = kActorLiveDrawn;
    ship.type = kMissionGunshipType;
    ship.subtype = 0;
    params.mode = mode;
    params.count = count;
    ship.pos.x.whole = point.x;
    ship.pos.y.whole = point.y;
    ship.pos.z.whole = point.z;
    ship.rotation.y = point.yaw;
    params.pointX = target.x;
    params.pointZ = target.z;
    encounterTrackInScriptSlot(ship, uint16_t(id));
    return s;
}

// exe 0x41cbb0 (ST15 variant 0) / exe 0x41d610 (ST16 variant 0): for each member k (its slot in `members`): id k + 1
// defeated -> bit k of the dead mask; alive with a last hit's kind -> bit (kind * 4 - 4) << k of the point mask. The
// group's marks |= dead << 9; the holder (type 0x3d, alive) gets the point mask.
void markGroup(const StateArray<uint8_t, 3>& members, uint32_t n, uint8_t holderSlot, uint8_t group) {
    const AnimatedActor& holder = animatedSlotHead(holderSlot);
    uint32_t dead = 0, m = 0;
    for (uint32_t k = n - 1;; --k) {
        const AnimatedActor& member = animatedSlotHead(members[k]);
        if (!enemyDefeated(k + 1)) {
            const uint8_t f = GroupMemberParams(member).lastHitKind;
            if (f != 0) m |= (1u << ((uint32_t(f) * 4u - 4u) & 31)) << k;
        } else {
            dead |= 1u << k;
        }
        if (k == 0) break;
    }
    enemyGroupMark(int32_t(group), uint16_t(dead << 9));
    if ((holder.flags & kActorLive) && holder.type == kGroupMemberType) GroupMemberParams(holder).pointMask = int8_t(m);
}

// ==== ST15 (exe 0x41ca90) ===========================================================================================
// a wave's row (exe data, 8 bytes): each ship's target record index (0: the player), the next wave's ticks
struct WaveRow : DataRecord {
    static constexpr uint32_t kBytes = 0x6;
    std::array<uint8_t, 3> targets{}; // +0x0
    uint16_t ticks = 0; // +0x4
    template <class R> static WaveRow load(const R& r) {
        WaveRow v;
        for (size_t i = 0; i < v.targets.size(); ++i) v.targets[i] = r.template read<uint8_t>(0x0 + i * sizeof(uint8_t));
        v.ticks = r.template read<uint16_t>(0x4);
        return v;
    }
};
WaveRow waveRow(uint32_t wave) { return dataRecord<WaveRow>(stateAddress(TableId::FieldSpawnersWaveRows) + wave * 8, WaveRow::kBytes); }
const DataTable<RecordTable<SpawnPoint, 10, 8>> wavePoints{TableId::FieldSpawnersWavePoints}; // exe data 0x88a870
const SpawnPoint& wavePoint(uint32_t k) { return wavePoints[k]; }
TargetRecord waveTarget(uint32_t t) { return dataRecord<TargetRecord>(stateAddress(TableId::FieldSpawnersWaveTargets) + t * 0x18, TargetRecord::kBytes); }
constexpr uint32_t kFlagGunshipsOut = 0x680, kFlagGunshipsBack = 0x681, kFlagSt15Won = 0x28;
constexpr uint32_t kFirstWaveId = 8;
constexpr uint8_t kWaves = 5;
constexpr uint8_t kEscortVariant = 0x11;
constexpr uint32_t kEscortId = 0xb;
const DataTable<ScalarTable<uint8_t, 0x20>> escortTargets{TableId::FieldSpawnersEscortTargets}; // by the row (* 4) and a random pick

// exe 0x41ccc0(spawner, n): the gunship wave of ids 8 .. 8 + n - 1: the defeated ones respawn (un-defeated) at the 4
// points 0x88a870 from a random start, with the targets of the wave's row (0: the player's point, x >= -0x100); the wave
// timer counts down, then the next wave (1..4) retargets every live one; the last one following the player gets its
// point each tick
void gunshipWave(St15WaveFields& spawner, uint32_t n) {
    const Player& p = player();
    int16_t px = p.pos.x.whole;
    const int16_t pz = p.pos.z.whole;
    if (px < -0x100) px = -0x100;
    // the exe passes a stack point {x, y, z} to exe 0x41d290; read here as values
    const uint32_t r = rngStep();
    uint32_t k = r & 3;
    WaveRow row = waveRow(spawner.wave);
    for (uint32_t i = n; i-- > 0;) {
        const uint32_t id = i + kFirstWaveId;
        if (!enemyDefeated(id)) continue;
        const uint8_t t = row.targets[i];
        uint32_t s;
        if (t == 0) {
            const SpawnPoint point = wavePoint(k);
            s = allocActor60(kActorStageClass);
            if (s) { // exe 0x41d290(point, 0, 2, 2, 0, {px, py, pz}, id)
                StaticActor& ship = staticActorAt(s);
                auto&& params = MissionGunshipParams(staticActorAt(s));
                ship.variant = 0;
                ship.loopFlags |= kActorSweepGroup2;
                params.reinforcementRow = 0;
                ship.flags = kActorLiveDrawn;
                ship.type = kMissionGunshipType;
                ship.subtype = 0;
                params.mode = kGunshipShootsPlayer;
                params.count = 2;
                ship.pos.x.whole = point.x;
                ship.pos.y.whole = point.y;
                ship.pos.z.whole = point.z;
                ship.rotation.y = point.yaw;
                params.pointX = px;
                params.pointZ = pz;
                encounterTrackInScriptSlot(ship, uint16_t(id));
            }
            spawner.follower = uint8_t(i + 1);
        } else {
            s = spawnGunship(wavePoint(k), 0, kGunshipFixedPoint, 4, 0, waveTarget(t), id);
        }
        if (s) {
            spawner.shipSlots[i] = staticActorAt(s).slot;
            enemyClearDefeated(id);
            k = (k + 1) & 3;
        } else {
            spawner.shipSlots[i] = 0;
        }
    }
    const uint16_t timer = uint16_t(spawner.waveTimer - 1u);
    spawner.waveTimer = timer;
    if (timer == 0) {
        const uint32_t cur = spawner.wave;
        uint32_t next = cur + 1;
        if (int32_t(next) >= kWaves) next = 1;
        const WaveRow current = waveRow(cur);
        spawner.follower = 0;
        for (uint32_t i = n; i-- > 0;) {
            const uint8_t s = spawner.shipSlots[i];
            if (s == 0) continue;
            auto&& ship = MissionGunshipParams(staticSlotAt(s));
            const uint8_t t = current.targets[i];
            if (t == 0) {
                ship.mode = kGunshipShootsPlayer;
                spawner.follower = uint8_t(i + 1);
            } else {
                ship.mode = kGunshipFixedPoint;
                ship.pointX = waveTarget(t).x;
                ship.pointZ = waveTarget(t).z;
            }
        }
        spawner.wave = uint8_t(next);
        spawner.waveTimer = current.ticks;
    }
    const uint8_t f = spawner.follower;
    if (f != 0) {
        const uint8_t s = spawner.shipSlots[uint32_t(f) - 1u];
        if (s != 0) {
            auto&& ship = MissionGunshipParams(staticSlotAt(s));
            ship.pointX = px;
            ship.pointZ = pz;
        }
    }
}

// exe 0x41cab0
void st15Init(ControllerActor& actor) {
    switch (actor.variant) {
    case 0: { // exe 0x41cb34: the group's records x 3 (the members), the 4th (the holder), the group (0x20, 0x2d, 1)
        St15GroupFields& spawner = classState<St15GroupFields>(actor);
        spawner.clearTail();
        for (uint32_t i = 0; i < 3; ++i)
            spawner.members[i] = spawnedSlot(encounterSpawnRecord(stateAddress(TableId::FieldSpawnersSt15GroupRecords) + i * 0x18),
                                             "ST15 init with a full pool 0x20");
        spawner.holder = spawnedSlot(encounterSpawnRecord(stateAddress(TableId::FieldSpawnersSt15GroupRecords) + 3 * 0x18), "ST15 init with a full pool 0x20");
        spawner.group = uint8_t(enemyGroupFind(0x20, 0x2d, 1));
        break;
    }
    case 1: { // exe 0x41cb0d
        St15WaveFields& spawner = classState<St15WaveFields>(actor);
        spawner.clearTail();
        enemyMarkDefeated(8);
        enemyMarkDefeated(9);
        enemyMarkDefeated(0xa);
        spawner.waveTimer = 0x96;
        break;
    }
    case 2: // exe 0x41cad6
        classState<St15WaveFields>(actor).clearTail();
        enemyMarkDefeated(0x10);
        enemyMarkDefeated(0x11);
        enemyMarkDefeated(0x12);
        enemyMarkDefeated(0x18);
        enemyMarkDefeated(0x19);
        enemyMarkDefeated(0x1a);
        break;
    case 3: classState<St15ClockFields>(actor).clearTail(); break;
    default: actor.rider = nullptr; break; // exe 0x40dc40(+0x18, +0x1c)
    }
    actor.enterState(uint8_t(SpawnerState::Run));
}

// variant 1 = exe 0x41cc70, sub-states [0x88a8b0]: the wave until flag 0x680; then ids 9 / 0xa told to leave, both
// gone -> flag 0x681; then a wave of one
enum class St15WaveSub : uint8_t { Wave = 0, Leave = 1, Last = 2 };
void st15Wave(ControllerActor& actor, St15WaveFields& spawner) {
    switch (St15WaveSub(actor.subState)) {
    case St15WaveSub::Wave: // exe 0x41cc90
        gunshipWave(spawner, 3);
        if (storyFlag(kFlagGunshipsOut)) {
            actor.subState = uint8_t(St15WaveSub::Leave);
            actor.enterStep(0);
        }
        return;
    case St15WaveSub::Leave: // exe 0x41cef0: the live ones of ids 9 / 0xa told to leave; both gone -> flag 0x681
        if (actor.step == 0) {
            if (!enemyDefeated(9)) MissionGunshipParams(staticSlotAt(spawner.shipSlots[1])).mode |= kGunshipLeave;
            if (!enemyDefeated(0xa)) MissionGunshipParams(staticSlotAt(spawner.shipSlots[2])).mode |= kGunshipLeave;
            actor.enterStep(1);
        }
        if (enemyDefeated(9) && enemyDefeated(0xa)) {
            spawner.shipSlots[1] = 0;
            spawner.shipSlots[2] = 0;
            setStoryFlag(kFlagGunshipsBack);
            actor.subState = uint8_t(St15WaveSub::Last);
            actor.enterStep(0);
        }
        return;
    case St15WaveSub::Last: gunshipWave(spawner, 1); return; // exe 0x41cfb0
    default: notPortedAt(kEnemySpawnerPort, "ST15 spawner variant 1 sub", actor.subState);
    }
}

// variant 2 = exe 0x41cfc0: after flag 0x681, the side with more ids defeated (0x10..0x12 vs 0x18..0x1a) gets one
// escort (variant 0x11, id 0xb) at a random point with a random target of its row; the next after id 0xb is defeated
// (the row + 1, at most 7)
enum class St15EscortSub : uint8_t { Wait = 0, Spawn = 1, Watch = 2 };
void st15Escorts(ControllerActor& actor, St15WaveFields& spawner) {
    switch (St15EscortSub(actor.subState)) {
    case St15EscortSub::Wait:
        if (!storyFlag(kFlagGunshipsBack)) return;
        actor.subState = uint8_t(St15EscortSub::Spawn);
        [[fallthrough]];
    case St15EscortSub::Spawn: {
        const uint32_t left =
            uint32_t(enemyDefeated(0x12)) + uint32_t(enemyDefeated(0x11)) + uint32_t(enemyDefeated(0x10));
        const uint32_t right =
            uint32_t(enemyDefeated(0x1a)) + uint32_t(enemyDefeated(0x19)) + uint32_t(enemyDefeated(0x18));
        if (left != 0 && int32_t(left) >= int32_t(right)) {
            const uint32_t t = escortTargets[(rngStep() & 3) + uint32_t(spawner.targetRow) * 4];
            const uint32_t r = rngStep();
            spawnGunship(wavePoint(r & 3), kEscortVariant, kGunshipFixedPoint, 3, 0, dataRecord<TargetRecord>(stateAddress(TableId::FieldSpawnersEscortTargetRecords) + t * 8, TargetRecord::kBytes),
                         kEscortId);
        } else if (right != 0) {
            const uint32_t t = escortTargets[(rngStep() & 3) + uint32_t(spawner.targetRow) * 4];
            uint8_t row = 4;
            if (!enemyDefeated(3)) row = 3;
            if (!enemyDefeated(2)) row = 2;
            if (!enemyDefeated(1)) row = 1;
            const uint32_t r = rngStep();
            spawnGunship(wavePoint(r & 3), kEscortVariant, kGunshipFixedPoint, 3, row,
                         dataRecord<TargetRecord>(stateAddress(TableId::FieldSpawnersEscortTargetRecords) + t * 8, TargetRecord::kBytes), kEscortId);
        } else {
            return;
        }
        enemyClearDefeated(kEscortId);
        actor.subState = uint8_t(St15EscortSub::Watch);
        return;
    }
    case St15EscortSub::Watch:
        if (enemyDefeated(kEscortId)) {
            actor.subState = uint8_t(St15EscortSub::Spawn);
            const uint8_t n = uint8_t(spawner.targetRow + 1u);
            spawner.targetRow = n;
            if (n >= 8) spawner.targetRow = 7;
        }
        return;
    default: return;
    }
}

// variant 3 = exe 0x41d190 (jump table 0x41d278 by the sub-state): 0x41a ticks -> flag 0x680; flag 0x681 -> 0xa8c ticks
// (at 0x1c2 left a timer 0x11, at 0 one 0x13) -> 90 ticks -> flag 0x28
enum class St15ClockSub : uint8_t { Start = 0, ToGunships = 1, WaitBack = 2, Fight = 3, Win = 4, Done = 5 };
void st15Clock(ControllerActor& actor, St15ClockFields& spawner) {
    switch (St15ClockSub(actor.subState)) {
    case St15ClockSub::Start:
        spawner.timer = 0x41a;
        actor.subState = uint8_t(St15ClockSub::ToGunships);
        [[fallthrough]];
    case St15ClockSub::ToGunships: {
        const uint32_t t = spawner.timer - 1u;
        spawner.timer = t;
        if (t == 0) {
            setStoryFlag(kFlagGunshipsOut);
            actor.subState = uint8_t(St15ClockSub::WaitBack);
        }
        return;
    }
    case St15ClockSub::WaitBack:
        if (storyFlag(kFlagGunshipsBack)) {
            spawner.timer = 0xa8c;
            actor.subState = uint8_t(St15ClockSub::Fight);
        }
        return;
    case St15ClockSub::Fight: {
        if (spawner.timer == 0x1c2) spawnTimer(0x11);
        const uint32_t t = spawner.timer - 1u;
        spawner.timer = t;
        if (t == 0) {
            spawnTimer(0x13);
            spawner.timer = 0x5a;
            actor.subState = uint8_t(St15ClockSub::Win);
        }
        return;
    }
    case St15ClockSub::Win: {
        const uint32_t t = spawner.timer - 1u;
        spawner.timer = t;
        if (t == 0) {
            setStoryFlag(kFlagSt15Won);
            actor.subState = uint8_t(St15ClockSub::Done);
        }
        return;
    }
    default: return;
    }
}

// states [0x88a898]: 0 = 0x41cab0, 1 = 0x41cb90 by the variant [0x88a8a0]
void st15Spawner(ControllerActor& actor) {
    switch (SpawnerState(actor.state)) {
    case SpawnerState::Init: st15Init(actor); return;
    case SpawnerState::Run:
        switch (actor.variant) {
        case 0: { // exe 0x41cbb0
            St15GroupFields& spawner = classState<St15GroupFields>(actor);
            markGroup(spawner.members, 3, spawner.holder, spawner.group);
            return;
        }
        case 1: st15Wave(actor, classState<St15WaveFields>(actor)); return;
        case 2: st15Escorts(actor, classState<St15WaveFields>(actor)); return;
        case 3: st15Clock(actor, classState<St15ClockFields>(actor)); return;
        default: notPortedAt(kEnemySpawnerPort, "ST15 spawner variant", actor.variant);
        }
    default: notPortedAt(kEnemySpawnerPort, "ST15 spawner 0x41ca90 state", actor.state);
    }
}

// ==== ST16 (exe 0x41d320) ===========================================================================================
// a carrier entry (exe data, 4 bytes): its slot index, variant, riders' row and id
struct CarrierEntry : DataRecord {
    static constexpr uint32_t kBytes = 0x4;
    uint8_t index = 0; // +0x0
    uint8_t variant = 0; // +0x1
    uint8_t riderRow = 0; // +0x2
    uint8_t id = 0; // +0x3
    template <class R> static CarrierEntry load(const R& r) {
        CarrierEntry v;
        v.index = r.template read<uint8_t>(0x0);
        v.variant = r.template read<uint8_t>(0x1);
        v.riderRow = r.template read<uint8_t>(0x2);
        v.id = r.template read<uint8_t>(0x3);
        return v;
    }
};
constexpr TableId kCarrierEntries[4] = {TableId::FieldSpawnersCarrierEntry0, TableId::FieldSpawnersCarrierEntry1, TableId::FieldSpawnersCarrierEntry2, TableId::FieldSpawnersCarrierEntry3};
const DataTable<RecordTable<SpawnPoint, 4, 8>> carrierPoints{TableId::FieldSpawnersCarrierPoints}; // exe data 0x88a978
const SpawnPoint& carrierPoint(uint32_t k) { return carrierPoints[k]; }
TargetRecord carrierTarget(uint32_t k) { return dataRecord<TargetRecord>(stateAddress(TableId::FieldSpawnersCarrierTargets) + k * 8, TargetRecord::kBytes); }
const DataTable<ScalarTable<uint8_t, 3>> st16PreDefeated{TableId::FieldSpawnersSt16PreDefeated};
const DataTable<RecordTable<SpawnPoint, 4, 8>> boxPoints{TableId::FieldSpawnersBoxPoints};
constexpr uint8_t kSpawnerType = 0xa;
constexpr uint32_t kFlagCarriersOut = 0x680, kFlagCarriersBack = 0x681, kFlagSt16Alarm = 0x682, kFlagBoxesGone = 0x683;
constexpr uint32_t kFlagMissionFailed = 0x2f;
constexpr int16_t kCarrierTargetX = 0x640, kCarrierTargetZ = 0x200;

// exe 0x41daa0(spawner): the point index += 1 or 2 (random), the target index += 1 (both & 3)
void nextCarrierPoints(St16CarrierFields& spawner) {
    const uint32_t r = rngStep();
    spawner.pointIndex = uint8_t(((r & 1) + spawner.pointIndex + 1u) & 3u);
    spawner.targetIndex = uint8_t((spawner.targetIndex + 1u) & 3u);
}

// exe 0x41da50(spawner, entry, v): the entry's id alive and its carrier slot set -> that carrier's variant = v
void setCarrierVariant(St16CarrierFields& spawner, const CarrierEntry& entry, uint8_t v) {
    if (enemyDefeated(entry.id)) return;
    const uint8_t s = spawner.carriers[entry.index];
    if (s != 0) animatedPool()[size_t(s - kAnimatedClass)].variant = v;
}

// exe 0x41d980(spawner, point, target, entry): the entry's id defeated -> its slot cleared and (pool room) a box carrier
// (pool 0x20 type 0x49) at the point, its target the record's, the box spawner's slot; un-defeated and tracked; its slot
// stored. 1 = spawned
int spawnCarrier(St16CarrierFields& spawner, const SpawnPoint& point, const TargetRecord& target,
                 const CarrierEntry& entry) {
    if (!enemyDefeated(entry.id)) return 0;
    spawner.carriers[entry.index] = 0;
    const uint32_t s = allocActor20();
    if (s == 0) return 0;
    AnimatedActor& carrier = animatedActorAt(s);
    auto&& params = BoxCarrierParams(animatedActorAt(s));
    carrier.flags = kActorLive;
    carrier.loopFlags |= kActorSweepGroup2;
    carrier.type = kBoxCarrierType;
    carrier.subtype = 0;
    carrier.variant = entry.variant;
    params.riderRow = int8_t(entry.riderRow);
    params.goHome = 0;
    params.boxSpawner = spawner.boxSpawner;
    carrier.pos.x.whole = point.x;
    carrier.pos.y.whole = point.y;
    carrier.pos.z.whole = point.z;
    carrier.rotation.y = point.yaw;
    params.targetX = target.x;
    params.targetZ = target.z;
    enemyClearDefeated(entry.id);
    encounterTrackInScriptSlot(carrier, entry.id);
    spawner.carriers[entry.index] = carrier.slot;
    return 1;
}

// exe 0x41dae0(spawner, k): the carrier of entry k alive and not sent home -> sent home
void sendCarrierHome(St16CarrierFields& spawner, uint32_t k) {
    const uint8_t s = spawner.carriers[k];
    if (s == 0) return;
    AnimatedActor& carrier = animatedPool()[size_t(s - kAnimatedClass)];
    auto&& params = BoxCarrierParams(carrier);
    if ((carrier.flags & kActorLive) && params.goHome == 0) params.goHome = 1;
}

// exe 0x41d340
void st16Init(ControllerActor& actor) {
    switch (actor.variant) {
    case 0: {
        St16GroupFields& spawner = classState<St16GroupFields>(actor);
        spawner.clearTail(); // exe 0x41d576: the group's records x 2 (the members), the holder's; a carrier spawner (variant 1)
        for (uint32_t i = 0; i < 2; ++i)
            spawner.members[i] = spawnedSlot(encounterSpawnRecord(stateAddress(TableId::FieldSpawnersSt16GroupRecords) + i * 0x18),
                                             "ST16 init with a full pool 0x20");
        spawner.holder = spawnedSlot(encounterSpawnRecord(stateAddress(TableId::FieldSpawnersSt16HolderRecord)), "ST16 init with a full pool 0x20");
        const uint32_t e = allocActorE0();
        if (e) {
            ControllerActor& carriers = controllerActorAt(e);
            carriers.flags = kActorLive;
            carriers.loopFlags |= kActorSweepGroup2;
            carriers.type = kSpawnerType;
            carriers.variant = 1;
            St16CarrierParams(carriers).carrierHolder = int8_t(spawner.holder);
        }
        break;
    }
    case 1: { // exe 0x41d491: a box spawner (variant 2; its slot to the holder's group), random points, ids 6 / 5 / 4
        // defeated, the carriers' records x 4 targeted at (0x640, 0x200)
        St16CarrierFields& spawner = classState<St16CarrierFields>(actor);
        spawner.clearTail();
        spawner.boxSpawner = 0;
        const uint32_t e = allocActorE0();
        if (e) {
            ControllerActor& boxes = controllerActorAt(e);
            boxes.flags = kActorLive;
            boxes.loopFlags |= kActorSweepGroup2;
            boxes.type = kSpawnerType;
            boxes.variant = 2;
            spawner.boxSpawner = boxes.slot;
            GroupMemberParams(animatedSlotHead(uint32_t(int32_t(spawner.carrierHolder.get())))).group = boxes.slot;
        }
        spawner.pointIndex = uint8_t(rngStep() & 3);
        spawner.targetIndex = uint8_t(rngStep() & 3);
        for (uint32_t i = 3; i-- > 0;) enemyMarkDefeated(st16PreDefeated[i]);
        for (uint32_t i = 0; i < 4; ++i) {
            const uint32_t recAt = stateAddress(TableId::FieldSpawnersSt16CarrierRecords) + i * EncounterRecord::kBytes;
            const EncounterRecord rec = dataRecord<EncounterRecord>(recAt, EncounterRecord::kBytes);
            const uint32_t s = encounterSpawnRecord(recAt);
            if (s) {
                auto&& params = BoxCarrierParams(animatedActorAt(s));
                params.targetX = kCarrierTargetX;
                params.targetZ = kCarrierTargetZ;
                params.boxSpawner = rec.trackId;
            }
        }
        break;
    }
    case 2: { // exe 0x41d374: 4 lift boxes at 0x88a9b8 (their slots listed), the crane and the lever linked both ways
        auto&& boxes = classState<BoxSpawnerFields>(actor);
        boxes.clearBoxesAndTail();
        for (uint32_t i = 0; i < 4; ++i) {
            const SpawnPoint point = boxPoints[i];
            const uint32_t s = allocActor60(kActorStageClass);
            if (s) {
                StaticActor& box = staticActorAt(s);
                box.flags = kActorLiveDrawn;
                box.type = kLiftBoxType;
                box.subtype = 0;
                box.variant = 0;
                box.setParamsWord(0);
                box.loopFlags |= kActorSweepGroup2;
                box.pos.x.whole = point.x;
                box.pos.y.whole = point.y;
                box.pos.z.whole = point.z;
                box.rotation.y = point.yaw;
                boxes.boxes[i] = box.slot;
            }
        }
        const uint32_t b = allocActor20();
        if (b) {
            AnimatedActor& crane = animatedActorAt(b);
            crane.flags = kActorLive;
            crane.loopFlags |= kActorSweepGroup2;
            crane.type = kCraneType;
            crane.subtype = 0;
            crane.variant = 0;
            crane.pos.x.whole = 0x700;
            crane.pos.y.whole = -0xb80;
            crane.pos.z.whole = 0x540;
            crane.rotation.y = 0x400;
            CraneParams(animatedActorAt(b)).controller = actor.slot;
            boxes.crane = int8_t(crane.slot);
        } else {
            boxes.crane = 0;
        }
        const uint32_t c = allocActor60(kActorStageClass);
        if (c) {
            StaticActor& lever = staticActorAt(c);
            lever.flags = kActorLiveDrawn;
            lever.loopFlags |= kActorSweepGroup2;
            lever.type = kLeverType;
            lever.subtype = 0;
            lever.variant = kLeverVariant;
            lever.pos.x.whole = 0x5c0;
            lever.pos.y.whole = -0x968;
            lever.pos.z.whole = 0x690;
            lever.rotation.y = 0x400;
            LeverParams(actorHeaderAt(c)).spawner = actor.slot;
            boxes.lever = lever.slot;
        } else {
            boxes.lever = 0;
        }
        break;
    }
    case 3: classState<St16ClockFields>(actor).clearTail(); break;
    default: actor.rider = nullptr; break; // exe 0x40dc40(+0x18, +0x1c)
    }
    actor.enterState(uint8_t(SpawnerState::Run));
}

// variant 1 = exe 0x41d6d0: sub-states [0x88aa0c]: the box carriers' respawns
enum class St16CarrierSub : uint8_t { Wait = 0, Out = 1, Back = 2 };
uint32_t defeatedCarriers() {
    return uint32_t(enemyDefeated(0x13)) + uint32_t(enemyDefeated(0x12)) + uint32_t(enemyDefeated(0x11)) +
           uint32_t(enemyDefeated(0x10));
}
void st16Carriers(ControllerActor& actor, St16CarrierFields& spawner) {
    auto spawnAt = [&](uint32_t entry) {
        return spawnCarrier(spawner, carrierPoint(spawner.pointIndex), carrierTarget(spawner.targetIndex),
                            dataRecord<CarrierEntry>(entry, CarrierEntry::kBytes));
    };
    switch (St16CarrierSub(actor.subState)) {
    case St16CarrierSub::Wait: { // exe 0x41d6f0: three of the first ones down (or flag 0x680)
        if (int32_t(defeatedCarriers()) >= 3 || storyFlag(kFlagCarriersOut)) {
            setStoryFlag(kFlagCarriersOut);
            actor.subState = uint8_t(St16CarrierSub::Out);
            actor.enterStep(0);
        }
        return;
    }
    case St16CarrierSub::Out: { // exe 0x41d750
        if (spawnAt(stateAddress(kCarrierEntries[0]))) nextCarrierPoints(spawner);
        if (actor.step != 0) {
            if (defeatedCarriers() == 4) {
                sendCarrierHome(spawner, 0);
                if (spawnAt(stateAddress(kCarrierEntries[2]))) nextCarrierPoints(spawner);
            }
        } else {
            actor.enterStep(1);
            if (spawnAt(stateAddress(kCarrierEntries[2]))) nextCarrierPoints(spawner);
        }
        if (storyFlag(kFlagCarriersBack)) {
            actor.subState = uint8_t(St16CarrierSub::Back);
            actor.enterStep(0);
        }
        return;
    }
    case St16CarrierSub::Back: { // exe 0x41d860
        bool done = false;
        if (actor.step == 0) {
            setCarrierVariant(spawner, dataRecord<CarrierEntry>(stateAddress(kCarrierEntries[0]), CarrierEntry::kBytes), 2);
            actor.enterStep(1);
        }
        if (spawnAt(stateAddress(kCarrierEntries[3]))) {
            nextCarrierPoints(spawner);
            spawner.lastCarrier = 1;
            done = true;
        }
        if (enemyDefeated(0x10) && enemyDefeated(0x11) && !done) {
            if (spawnAt(stateAddress(kCarrierEntries[1]))) nextCarrierPoints(spawner);
            return;
        }
        if (enemyDefeated(0x12) && enemyDefeated(0x13)) {
            if (spawnAt(stateAddress(kCarrierEntries[2]))) nextCarrierPoints(spawner);
        }
        return;
    }
    default: notPortedAt(kEnemySpawnerPort, "ST16 spawner variant 1 sub", actor.subState);
    }
}

// variant 2 = exe 0x41db20: the boxes' slots dropped when gone; all gone (flag 0x683 clear) -> a timer 0x10, flag
// 0x683; the crane called on request; some boxes missing while the crane is idle
void st16Boxes(ControllerActor& actor) {
    auto&& boxes = classState<BoxSpawnerFields>(actor);
    uint32_t alive = 0;
    for (uint32_t i = 0; i < 4; ++i) {
        const uint16_t s = boxes.boxes[i];
        if (s == 0) continue;
        if (!(staticSlotAt(s).flags & kActorLive)) boxes.boxes[i] = 0;
        else ++alive;
    }
    boxes.boxesAlive = uint8_t(alive);
    if (!storyFlag(kFlagBoxesGone) && alive == 0) {
        spawnTimer(0x10);
        setStoryFlag(kFlagBoxesGone);
    }
    auto&& crane = CraneParams(animatedSlotHead(uint32_t(int32_t(boxes.crane))));
    if (boxes.callCrane != 0 && crane.called == 0) {
        crane.called = 1;
        boxes.callCrane = 0;
    }
    boxes.boxesMissing = (int32_t(alive) < 4 && crane.called == 0) ? 1 : 0;
}

// variant 3 = exe 0x41dbe0: (the player alive, the mission not failed) a tick counter: flag 0x680 (or 0x384 ticks) ->
// sub-state 1; then at 0xa8c flag 0x681, 0xc4e a timer 0x14, 0xe10 flag 0x682 + a timer 0x11, 0x1194 a timer 0x12
enum class St16ClockSub : uint8_t { Start = 0, Running = 1, Done = 2 };
void st16Clock(ControllerActor& actor, St16ClockFields& spawner) {
    if (player().hitPoints < 0 || storyFlag(kFlagMissionFailed)) return;
    switch (St16ClockSub(actor.subState)) {
    case St16ClockSub::Start:
        if (storyFlag(kFlagCarriersOut)) {
            spawner.ticks = 0x384;
            actor.subState = uint8_t(St16ClockSub::Running);
        } else if (spawner.ticks >= 0x384) {
            setStoryFlag(kFlagCarriersOut);
            actor.subState = uint8_t(St16ClockSub::Running);
        }
        break;
    case St16ClockSub::Running: {
        const int32_t t = spawner.ticks;
        if (t > 0xe10) {
            if (t == 0x1194) {
                actor.subState = uint8_t(St16ClockSub::Done);
                spawnTimer(0x12);
            }
        } else if (t == 0xe10) {
            setStoryFlag(kFlagSt16Alarm);
            spawnTimer(0x11);
        } else if (t == 0xa8c) {
            setStoryFlag(kFlagCarriersBack);
        } else if (t == 0xc4e) {
            spawnTimer(0x14);
        }
        break;
    }
    default: return;
    }
    spawner.ticks += 1u;
}

// states [0x88a9e8]: 0 = 0x41d340, 1 = 0x41d5f0 by the variant [0x88a9fc]
void st16Spawner(ControllerActor& actor) {
    switch (SpawnerState(actor.state)) {
    case SpawnerState::Init: st16Init(actor); return;
    case SpawnerState::Run:
        switch (actor.variant) {
        case 0: { // exe 0x41d610
            St16GroupFields& spawner = classState<St16GroupFields>(actor);
            markGroup(spawner.members, 2, spawner.holder, spawner.group);
            return;
        }
        case 1: st16Carriers(actor, classState<St16CarrierFields>(actor)); return;
        case 2: st16Boxes(actor); return;
        case 3: st16Clock(actor, classState<St16ClockFields>(actor)); return;
        default: notPortedAt(kEnemySpawnerPort, "ST16 spawner variant", actor.variant);
        }
    default: notPortedAt(kEnemySpawnerPort, "ST16 spawner 0x41d320 state", actor.state);
    }
}

// ==== ST17 (exe 0x41dd70) ===========================================================================================
enum class WalkerEntryState : uint8_t { Out = 0, Waiting = 1, OnRoute = 2 };
// a variant's walkers (exe data, 4 bytes): their count, first id, mode bits, mode
struct WalkerSet : DataRecord {
    uint8_t count = 0, firstId = 0, modeBits = 0, mode = 0;
    static WalkerSet load(const TableReader& r) { return {{}, r.u8(0), r.u8(1), r.u8(2), r.u8(3)}; }
};
const DataTable<RecordTable<WalkerSet, 2, 4>> walkerSets{TableId::FieldSpawnersWalkerSets}; // by variant (exe 0x88ab18)
const WalkerSet& walkerSet(uint32_t v) { return walkerSets[v]; }
const DataTable<RecordPointerTable<SpawnPoint, 2, 8>> walkerStarts{TableId::FieldSpawnersWalkerStarts}; // the variant's start point (exe data 0x88ab48: pointers to s16 x / y / z / yaw)
// a route step (exe data, 4 bytes): its point's index, the mode bits it sets, its end (0 go on, 1 stop, 2 stop and the
// mode cleared), the yaw / 16
struct RouteStep : DataRecord {
    static constexpr uint32_t kBytes = 4;
    uint8_t point = 0, modeBits = 0, end = 0, yaw = 0;
    static RouteStep load(const TableReader& r) { return {{}, r.u8(0), r.u8(1), r.u8(2), r.u8(3)}; }
};
// the route sets' step lists (exe table 0x88aad8: a pointer per route to its steps)
struct WalkerRoutes : DataRecord {
    static constexpr size_t kRoutes = 0x20, kMaxSteps = 0x40;
    static constexpr uint32_t kBytes = kRoutes * 4;
    std::array<TableReader, kRoutes> routes;
    RouteStep step(size_t route, size_t i) const {
        if (route >= kRoutes) tableIndexError(route, kRoutes);
        return RouteStep::load(routes[route].sub(i * RouteStep::kBytes, RouteStep::kBytes));
    }
    static WalkerRoutes load(const TableReader& r) {
        WalkerRoutes t;
        for (size_t k = 0; k < kRoutes; ++k) t.routes[k] = r.follow(k * 4, kMaxSteps * RouteStep::kBytes);
        return t;
    }
};
const DataTable<WalkerRoutes> walkerRoutes{TableId::FieldSpawnersWalkerRoutes};
// a route point (exe data 0x88aa18: s16 x / z)
struct RoutePoint : DataRecord {
    int16_t x = 0, z = 0;
    static RoutePoint load(const TableReader& r) { return {{}, r.s16(0), r.s16(2)}; }
};
const DataTable<RecordTable<RoutePoint, 10, 4>> routePoints{TableId::FieldSpawnersRoutePoints};
const RoutePoint& routePoint(uint32_t k) { return routePoints[k]; }
constexpr uint32_t kRetreatPoint = 9; // the point everyone retreats to (0x88aa3c)
constexpr uint8_t kWalkerOutVariant = 4;
constexpr uint32_t kFlagCellAOpen = 0x32, kFlagCellBOpen = 0x33, kFlagCellCOpen = 0x34, kFlagRetreat = 0x684;
constexpr uint32_t kFlagSt17Alarm = 0x682, kFlagSt17Rise = 0x683, kFlagSt17Won = 0x29;
constexpr uint32_t kFlagBoatIn = 0x680, kFlagBoatOut = 0x681; // the carrier boat's trips (content/nino/carrier_boat)
constexpr uint32_t kIdBoatIn = 9, kIdRetreatGuard = 0xd;

// the walker drifted off: z >= 0x400, y >= -0x680, |x| <= 0x200 and near the camera (exe 0x5e59a0(pos, 0x80)) -> freed
void despawnIfOff(AnimatedActor& walker) {
    if (walker.pos.z < 0x4000000 || walker.pos.y < int32_t(0xf9800000u)) return;
    if (uint32_t(walker.pos.x) + 0x2000000u > 0x4000000u) return;
    if (pointVisibleOrNear(walker.pos, 0x80) != 0) walker.enterState(kWalkerGone);
}
// the walker of an entry while it is one (alive, type 0x2d, sub-type set)
AnimatedActor* entryWalker(uint8_t slot) {
    AnimatedActor& walker = animatedPool()[size_t(slot - kAnimatedClass)];
    if (!(walker.flags & kActorLive) || walker.type != kWalkerType || walker.subtype == 0) return nullptr;
    return &walker;
}

// exe 0x41dd90
void st17Init(ControllerActor& actor) {
    const uint32_t v = actor.variant;
    switch (v) {
    case 0: { // exe 0x41ddc0: the walkers all at the variant's start point, their ids tracked
        St17WalkerFields& spawner = classState<St17WalkerFields>(actor);
        spawner.clearEntriesAndTail(); // (after the tail's clear)
        const WalkerSet set = walkerSet(v);
        const uint32_t n = set.count;
        const SpawnPoint& start = walkerStarts[v];
        for (uint32_t i = 0; i < n; ++i) {
            auto&& e = spawner.entries[i];
            const uint32_t id = uint32_t(set.firstId) + i;
            e.step = 0;
            e.slot = 0;
            const uint32_t s = allocActor20();
            if (s == 0) continue;
            AnimatedActor& walker = animatedActorAt(s);
            auto&& params = WalkerParams(animatedActorAt(s));
            walker.flags = kActorLiveDrawn;
            walker.loopFlags |= kActorSweepGroup2;
            walker.type = kWalkerType;
            walker.subtype = kWalkerSub;
            walker.variant = 0;
            params.count = 0;
            params.mode = set.mode;
            params.owner = uint8_t(spawner.boat.get());
            params.trackId = uint8_t(id);
            walker.pos.x = int32_t(uint32_t(int32_t(start.x)) << 16);
            walker.pos.y = int32_t(uint32_t(int32_t(start.y)) << 16);
            walker.pos.z = int32_t(uint32_t(int32_t(start.z)) << 16);
            params.pointX = routePoint(0).x;
            params.pointZ = routePoint(0).z;
            walker.rotation.y = start.yaw;
            e.slot = walker.slot;
            encounterTrackInScriptSlot(walker, uint16_t(id));
            enemyClearDefeated(id);
            e.state = uint8_t(WalkerEntryState::Out);
        }
        break;
    }
    case 1: { // exe 0x41ded6: the entries cleared, their ids defeated (they come out later)
        St17WalkerFields& spawner = classState<St17WalkerFields>(actor);
        spawner.clearEntriesAndTail(); // (after the tail's clear)
        const WalkerSet set = walkerSet(v);
        const uint32_t n = set.count;
        for (uint32_t i = 0; i < n; ++i) {
            auto&& e = spawner.entries[i];
            e.step = 0;
            e.slot = 0;
            enemyMarkDefeated(uint32_t(set.firstId) + i);
            e.state = uint8_t(WalkerEntryState::Out);
        }
        break;
    }
    case 2: // exe 0x41df3e
        actor.rider = nullptr; // exe 0x40dc40(+0x18, +0x1c)
        St17CellParams(actor).group = int8_t(enemyGroupFind(0x20, 0x2d, 1));
        break;
    case 3: { // exe 0x41df5e
        St17ClockFields& spawner = classState<St17ClockFields>(actor);
        spawner.clearTail();
        spawner.countdown = 0xa8c;
        break;
    }
    default: actor.rider = nullptr; break; // exe 0x40dc40(+0x18, +0x1c)
    }
    actor.enterState(uint8_t(SpawnerState::Run));
}

// variants 0 / 1 = exe 0x41dfb0: the walkers on their routes (the route set: flag 0x681 -> 3, 0x680 -> 2, else 0, +
// the variant * 8); the waiting ones come back one by one while the boat is idle; flag 0x33: the first route's walkers
// near the start switch to route 1; flag 0x684 (id 0xd alive): everyone retreats
void st17Walkers(ControllerActor& actor, St17WalkerFields& spawner) {
    const uint32_t v = actor.variant;
    const WalkerSet set = walkerSet(v);
    const auto&& boat = CarrierBoatParams(staticSlotAt(uint32_t(int32_t(spawner.boat.get()))));
    const uint32_t mode = storyFlag(kFlagBoatOut) ? 3u : (storyFlag(kFlagBoatIn) ? 2u : 0u);
    const uint32_t routeSet = mode + v * 8;
    const uint32_t n = set.count;
    for (uint32_t i = 0; i < n; ++i) {
        auto&& e = spawner.entries[i];
        const uint32_t id = uint32_t(set.firstId) + i;
        if (enemyDefeated(id)) e.state = uint8_t(WalkerEntryState::Waiting);
        switch (WalkerEntryState(e.state)) {
        case WalkerEntryState::Out: { // exe 0x41e391
            const uint8_t s = e.slot;
            if (s == 0) break;
            if (AnimatedActor* walker = entryWalker(s)) despawnIfOff(*walker);
            break;
        }
        case WalkerEntryState::Waiting: { // exe 0x41e2a5: every 64 ticks in its own 8-tick slot, back on the route set
            if (boat.busy != 0 || enemyDefeated(kIdBoatIn)) break;
            const uint32_t fr = uint8_t(gameState().frame);
            if (fr & 7) break;
            if ((fr & 0x38) != (i + v * 4) * 8) break;
            const RouteStep step = walkerRoutes->step(routeSet, 0);
            e.route = uint8_t(routeSet);
            e.step = 0;
            e.slot = 0;
            const RoutePoint pt = routePoint(step.point);
            const uint32_t s = allocActor20();
            if (s == 0) break;
            AnimatedActor& walker = animatedActorAt(s);
            auto&& params = WalkerParams(animatedActorAt(s));
            walker.flags = kActorLiveDrawn;
            walker.loopFlags |= kActorSweepGroup2;
            walker.type = kWalkerType;
            walker.subtype = kWalkerSub;
            walker.variant = kWalkerOutVariant;
            params.count = 0;
            params.mode = uint8_t(step.modeBits | set.modeBits);
            params.owner = uint8_t(spawner.boat.get());
            params.trackId = uint8_t(id);
            params.pointX = pt.x;
            params.pointZ = pt.z;
            walker.rotation.y = int16_t(uint32_t(step.yaw) << 4);
            e.slot = walker.slot;
            encounterTrackInScriptSlot(walker, uint16_t(id));
            enemyClearDefeated(id);
            e.state = uint8_t(step.end != 0 ? WalkerEntryState::Out : WalkerEntryState::OnRoute);
            break;
        }
        case WalkerEntryState::OnRoute: { // exe 0x41e076: the next route step within 0x80 of the current target
            const uint8_t s = e.slot;
            if (s == 0) break;
            AnimatedActor* walker = entryWalker(s);
            if (!walker) {
                e.slot = 0;
                break;
            }
            const RoutePoint pt = routePoint(walkerRoutes->step(e.route, e.step).point);
            // exe 0x5e3b00(the walker's position, {x << 16, ?, z << 16})
            const int32_t dx = int16_t(uint16_t(uint32_t(int32_t(pt.x)) - uint16_t(walker->pos.x.whole)));
            const int32_t dz = int16_t(uint16_t(uint32_t(int32_t(pt.z)) - uint16_t(walker->pos.z.whole)));
            const int d = isqrt(int32_t(uint32_t(dx) * uint32_t(dx) + uint32_t(dz) * uint32_t(dz)));
            if (d < 0x80) {
                const uint8_t next = uint8_t(e.step + 1u);
                e.step = next;
                const RouteStep step = walkerRoutes->step(e.route, next);
                const RoutePoint np = routePoint(step.point);
                WalkerParams params(*walker);
                params.pointX = np.x;
                params.pointZ = np.z;
                params.mode = uint8_t(step.modeBits | (params.mode & 0x3fu));
                if (step.end != 0) {
                    e.state = uint8_t(WalkerEntryState::Out);
                    if (step.end == 2) params.mode = 0;
                }
            }
            despawnIfOff(*walker);
            break;
        }
        default: break;
        }
    }
    if (spawner.routesSwitched == 0 && storyFlag(kFlagCellBOpen)) { // route 0 walkers still near the start: route 1
        for (uint32_t i = 0; i < n; ++i) {
            auto&& e = spawner.entries[i];
            if (WalkerEntryState(e.state) == WalkerEntryState::OnRoute && e.route == 0 && e.step < 3) e.route = 1;
        }
        spawner.routesSwitched = 1;
    }
    // everyone sent to the retreat point, mode 2, count 1
    if (spawner.retreatSent == 0 && !enemyDefeated(kIdRetreatGuard) && storyFlag(kFlagRetreat)) {
        for (uint32_t i = 0; i < n; ++i) {
            auto&& e = spawner.entries[i];
            const WalkerEntryState st = WalkerEntryState(e.state);
            if (st == WalkerEntryState::Out) {
                const uint8_t s = e.slot;
                if (s == 0) continue;
                WalkerParams params(animatedPool()[size_t(s - kAnimatedClass)]);
                params.pointX = routePoint(kRetreatPoint).x;
                params.pointZ = routePoint(kRetreatPoint).z;
                params.mode = 2;
                params.count = 1;
            } else if (st == WalkerEntryState::OnRoute) {
                e.route += 4u;
                const uint8_t s = e.slot;
                if (s == 0) continue;
                WalkerParams params(animatedPool()[size_t(s - kAnimatedClass)]);
                params.mode = 2;
                params.count = 1;
            }
        }
        spawner.retreatSent = 1;
    }
}

// variant 2 = exe 0x41e480: the grid cells (0x40, 0x3a) / (0x40, 0x44) broken open -> flags 0x32 / 0x33; id 0xd
// defeated -> the group's mark 0x200; the cell (0x44, 0x44) open (flag 0x34 clear): two blasts, a debris shot (pool 0x40
// type 3 variant 0x82) and two smokes, flag 0x34
void st17Cells(const St17CellParams& spawner) {
    if (!storyFlag(kFlagCellAOpen) && gridCellSegmentBroken(0x40, 0x3a)) setStoryFlag(kFlagCellAOpen);
    if (!storyFlag(kFlagCellBOpen) && gridCellSegmentBroken(0x40, 0x44)) setStoryFlag(kFlagCellBOpen);
    if (enemyDefeated(kIdRetreatGuard)) enemyGroupMark(int32_t(spawner.group), 0x200);
    if (storyFlag(kFlagCellCOpen) || !gridCellSegmentBroken(0x44, 0x44)) return;
    auto blast = [](int32_t x, int32_t y, int32_t z, uint16_t size) {
        const uint32_t s = allocActor60(0); // exe 0x5e1300
        if (!s) return;
        StaticActor& b = staticActorAt(s);
        auto&& params = BlastParams(actorHeaderAt(s));
        b.flags = kActorLive;
        b.loopFlags |= kActorSweepGroup2;
        b.type = typeByte(GlobalStaticType::Blast);
        b.subtype = 0;
        b.variant = 0;
        b.pos.x = x;
        b.pos.y = y;
        b.pos.z = z;
        params.size = size;
        params.attack = 4;
    };
    auto smoke = [](int32_t x, int32_t y, int32_t z) {
        const uint32_t s = allocActor60(kActorStageClass); // exe 0x5e12f0
        if (!s) return;
        StaticActor& b = staticActorAt(s);
        b.flags = kActorLive;
        b.type = kSmokeType;
        b.subtype = 0;
        b.variant = 1;
        b.pos.x = x;
        b.pos.y = y;
        b.pos.z = z;
        auto&& params = SmokeParams(actorHeaderAt(s));
        params.drift = 0;
        params.life = 0x1e;
    };
    blast(0xa000000, int32_t(0xf7000000u), 0xa000000, 0x200);
    if (const uint32_t s = allocActor40(kActorStageClass)) { // exe 0x5e1280
        ProjectileActor& debris = projectileActorAt(s);
        debris.flags = kActorLive;
        debris.loopFlags |= kActorSweepGroup2;
        debris.type = 3;
        debris.variant = 0x82;
        debris.params[0] = 2;
        debris.params[1] = 0;
        debris.pos.x = 0xa000000;
        debris.pos.y = int32_t(0xf8000000u);
        debris.pos.z = 0xa000000;
    }
    smoke(0xa000000, int32_t(0xf8000000u), 0xa000000);
    blast(0x7000000, int32_t(0xf8000000u), 0xb000000, 0x100);
    if (const uint32_t s = allocActor60(kActorStageClass)) {
        StaticActor& b = staticActorAt(s);
        b.flags = kActorLive;
        b.type = kSmokeType;
        b.subtype = 0;
        b.variant = 0;
        b.pos.x = 0x7000000;
        b.pos.y = int32_t(0xf8000000u);
        b.pos.z = 0xb000000;
        auto&& params = SmokeParams(actorHeaderAt(s));
        params.drift = 0;
        params.life = 0x1e;
    }
    setStoryFlag(kFlagCellCOpen);
}

// variant 3 = exe 0x41e6d0: sub-state 0 (jump table 0x41e7c8 by the step): 0x384 ticks -> flag 0x682; flag 0x680 ->
// 0x1c2 ticks -> flag 0x683; id 9 defeated -> sub-state 1: 0x2d ticks -> flag 0x29. Always: the countdown to a timer
// 0x24.
enum class St17ClockStep : uint8_t { Start = 0, ToAlarm = 1, WaitOut = 2, StartRise = 3, ToRise = 4, Done = 5 };
void st17Clock(ControllerActor& actor, St17ClockFields& spawner) {
    if (actor.subState == 0) {
        switch (St17ClockStep(actor.step)) {
        case St17ClockStep::Start:
            spawner.timer = 0x384;
            actor.step = uint8_t(St17ClockStep::ToAlarm);
            [[fallthrough]];
        case St17ClockStep::ToAlarm: {
            const uint32_t t = spawner.timer - 1u;
            spawner.timer = t;
            if (t == 0) {
                setStoryFlag(kFlagSt17Alarm);
                actor.step = uint8_t(St17ClockStep::WaitOut);
            }
            [[fallthrough]];
        }
        case St17ClockStep::WaitOut:
            if (storyFlag(kFlagBoatIn)) actor.step = uint8_t(St17ClockStep::StartRise);
            break;
        case St17ClockStep::StartRise:
            spawner.timer = 0x1c2;
            actor.step = uint8_t(St17ClockStep::ToRise);
            [[fallthrough]];
        case St17ClockStep::ToRise: {
            const uint32_t t = spawner.timer - 1u;
            spawner.timer = t;
            if (t == 0) {
                setStoryFlag(kFlagSt17Rise);
                actor.step = uint8_t(St17ClockStep::Done);
            }
            break;
        }
        default: break;
        }
        if (enemyDefeated(kIdBoatIn)) {
            actor.subState = 1;
            actor.enterStep(0);
        }
    } else {
        if (actor.step == 0) {
            spawner.timer = 0x2d;
            actor.step = 1;
        }
        const uint32_t t = spawner.timer - 1u;
        spawner.timer = t;
        if (t == 0) setStoryFlag(kFlagSt17Won);
    }
    const uint32_t c = spawner.countdown;
    if (c != 0) {
        spawner.countdown = c - 1u;
        if (c - 1u == 0) spawnTimer(0x24);
    }
}

// states [0x88ab50]: 0 = 0x41dd90, 1 = 0x41df90 by the variant [0x88ab58] (0 / 1 = 0x41dfb0, 2 = 0x41e480, 3 =
// 0x41e6d0)
void st17Spawner(ControllerActor& actor) {
    switch (SpawnerState(actor.state)) {
    case SpawnerState::Init: st17Init(actor); return;
    case SpawnerState::Run:
        switch (actor.variant) {
        case 0:
        case 1: st17Walkers(actor, classState<St17WalkerFields>(actor)); return;
        case 2: st17Cells(St17CellParams(actor)); return;
        case 3: st17Clock(actor, classState<St17ClockFields>(actor)); return;
        default: notPortedAt(kEnemySpawnerPort, "ST17 spawner variant", actor.variant);
        }
    default: notPortedAt(kEnemySpawnerPort, "ST17 spawner 0x41dd70 state", actor.state);
    }
}

// ==== ST18 (exe 0x41e7e0) ===========================================================================================
const DataTable<RecordTable<SpawnPoint, 4, 8>> st18FirstBirdbots{TableId::FieldSpawnersSt18FirstBirdbots};
// the fight gunships' points (exe data 0x88ab88; the calls read up to the sixth from a random first one)
const DataTable<RecordTable<SpawnPoint, 6, 8>> st18GunshipPoints{TableId::FieldSpawnersSt18GunshipPoints};
const DataTable<ScalarTable<uint8_t, 2>> st18GunshipVariants{TableId::FieldSpawnersSt18GunshipVariants};
const DataTable<RecordTable<SpawnPoint, 4, 8>> st18ReturnPoints{TableId::FieldSpawnersSt18ReturnPoints};
constexpr size_t kSt18ReturnPoint = 2; // the returning gunship's point (exe 0x88abb8): st18ReturnPoints' third
const DataTable<ScalarTable<int16_t, 3>> repairRates{TableId::FieldSpawnersRepairRates}; // by the machine's hit state
constexpr int32_t kRepairMax = 0xf423f;
constexpr uint32_t kFlagRepaired = 0x2e, kFlagSt18Timer = 0x2a;
constexpr uint32_t kIdFirstGunship = 0x10, kIdReturningGunship = 0x14, kIdFirstDropped = 0x18;

// exe 0x41ec60(variant, orders, point, id): a fight gunship (static type 0x1b) at the point (<< 16), tracked
uint32_t spawnFightGunship(uint8_t variant, uint8_t orders, const SpawnPoint& point, uint32_t id) {
    const uint32_t s = allocActor60(kActorStageClass);
    if (s == 0) return 0;
    StaticActor& ship = staticActorAt(s);
    auto&& params = ShipFightGunshipParams(staticActorAt(s));
    ship.variant = variant;
    ship.subtype = 0;
    params.shielded = 0;
    ship.flags = kActorLiveDrawn;
    ship.type = kShipFightGunshipType;
    ship.loopFlags |= kActorSweepGroup2;
    params.orders = int8_t(orders);
    ship.pos.x = int32_t(uint32_t(int32_t(point.x)) << 16);
    ship.pos.y = int32_t(uint32_t(int32_t(point.y)) << 16);
    ship.pos.z = int32_t(uint32_t(int32_t(point.z)) << 16);
    encounterTrackInScriptSlot(ship, uint16_t(id));
    return s;
}

// state 0 = exe 0x41e800 (jump table 0x41e960 by the variant)
void st18Init(ControllerActor& actor) {
    switch (actor.variant) {
    case 0: {
        St18FightFields& spawner = classState<St18FightFields>(actor);
        spawner.clearTail(); // exe 0x41e82c: the fight block cleared; ids 4..1: Birdbots (variant 1) at 0x88ab68; 4 to keep; the two
        // fight gunships (variants [0x88abd0 + k], ids 0x11 / 0x10) at 0x88ab88 / 0x88ab90
        ShipFightBlock& block = shipFightBlock();
        block.freeIds = 0;
        block.dropped = 0;
        block.spare = 0;
        uint32_t k = 0;
        for (uint32_t id = 4; id != 0; --id, ++k) {
            const SpawnPoint point = st18FirstBirdbots[k];
            const uint32_t s = allocActor20();
            if (s == 0) continue;
            AnimatedActor& bot = animatedActorAt(s);
            bot.flags = kActorLiveDrawn;
            bot.loopFlags |= kActorSweepGroup2;
            bot.type = kWalkerType;
            bot.subtype = kWalkerSub;
            bot.variant = 1;
            WalkerParams(animatedActorAt(s)).trackId = uint8_t(id);
            bot.pos.x = int32_t(uint32_t(int32_t(point.x)) << 16);
            bot.pos.y = int32_t(uint32_t(int32_t(point.y)) << 16);
            bot.pos.z = int32_t(uint32_t(int32_t(point.z)) << 16);
            bot.rotation.y = point.yaw;
            encounterTrackInScriptSlot(bot, uint16_t(id));
        }
        spawner.birdbots = 4;
        size_t pos = 0;
        for (uint32_t g = 2; g-- > 0; ++pos) {
            const uint32_t s = spawnFightGunship(st18GunshipVariants[g], 0, st18GunshipPoints[pos], kIdFirstGunship + g);
            if (s) spawner.gunships[g] = staticActorAt(s).slot;
        }
        break;
    }
    case 1: { // exe 0x41e8ff
        St18TimerFields& spawner = classState<St18TimerFields>(actor);
        spawner.clearTail();
        spawner.timer = 0x960;
        break;
    }
    case 2: { // exe 0x41e911
        St18ReturnFields& spawner = classState<St18ReturnFields>(actor);
        spawner.clearTail();
        const uint32_t s = spawnFightGunship(0, 0, st18ReturnPoints[kSt18ReturnPoint], kIdReturningGunship);
        if (s) spawner.gunship = staticActorAt(s).slot;
        break;
    }
    case 3: { // exe 0x41e938: the machine of the parameter's slot
        St18RepairFields& spawner = classState<St18RepairFields>(actor);
        spawner.clearTail();
        spawner.machine = &animatedSlotHead(uint32_t(int32_t(int8_t(actor.params[0]))));
        break;
    }
    default: actor.rider = nullptr; break; // exe 0x40dc40(+0x18, +0x1c)
    }
    actor.enterState(uint8_t(SpawnerState::Run));
}

// variant 0 = exe 0x41e990: ids 1..(the Birdbots to keep) defeated -> the free ids mask; the first drop (once): ids 5 /
// 6 defeated, 6 to keep; while not stopped: fewer alive than to keep -> the first gunship (id 0x10) called to the square
// (exe 0x41eae0); defeated gunships 0x11 / 0x10 respawn (variant 1 / 2) at consecutive points 0x88ab88 from a random
// start
void st18Fight(St18FightFields& spawner) {
    const int32_t n = spawner.birdbots;
    uint32_t mask = 0;
    int32_t alive = 0;
    if (n != 0) {
        for (int32_t k = n - 1;; --k) {
            const uint32_t d = enemyDefeated(uint32_t(k + 1)) ? 1u : 0u;
            mask |= d << (uint32_t(k) & 31);
            alive += int32_t(d ^ 1u);
            if (k == 0) break;
        }
    }
    ShipFightBlock& block = shipFightBlock();
    block.freeIds = mask;
    if (block.dropped == 1 && spawner.secondWave == 0) {
        enemyMarkDefeated(5);
        enemyMarkDefeated(6);
        spawner.birdbots = 6;
        spawner.secondWave = 1;
    }
    if (spawner.callsStopped != 0) return;
    if (alive < int32_t(spawner.birdbots)) { // exe 0x41eae0(spawner, block, 0)
        if (!enemyDefeated(kIdFirstGunship)) {
            auto&& ship = ShipFightGunshipParams(staticSlotAt(spawner.gunships[0]));
            if (ship.orders == 0) ship.orders = 1;
        }
    }
    size_t pos = rngStep() & 3;
    for (uint32_t k = 2; k-- > 0;) {
        const uint32_t id = k + kIdFirstGunship;
        if (!enemyDefeated(id)) continue;
        const uint32_t s = allocActor60(kActorStageClass);
        if (s == 0) continue;
        StaticActor& ship = staticActorAt(s);
        auto&& params = ShipFightGunshipParams(staticActorAt(s));
        const SpawnPoint& point = st18GunshipPoints[pos];
        ship.flags = kActorLiveDrawn;
        ship.type = kShipFightGunshipType;
        ship.subtype = 0;
        ship.variant = 1;
        params.orders = 0;
        if (k == 0) {
            ship.variant = 2;
            params.orders = 1;
        }
        ship.pos.x = int32_t(uint32_t(int32_t(point.x)) << 16);
        ship.pos.y = int32_t(uint32_t(int32_t(point.y)) << 16);
        ship.pos.z = int32_t(uint32_t(int32_t(point.z)) << 16);
        enemyClearDefeated(id);
        encounterTrackInScriptSlot(ship, uint16_t(id));
        ++pos;
        spawner.gunships[k] = ship.slot;
    }
}

// variant 2 = exe 0x41eb60: sub-state 0: id 0x14 defeated -> 30 ticks (sub-state 1) -> sub-state 2: the next of 4
// points 0x88aba8 not in view -> id 0x14 back, sub-state 0; else the Birdbots alive of ids 0x18..0x1f shield the gunship
enum class St18ReturnSub : uint8_t { Watch = 0, Wait = 1, Return = 2 };
void st18Return(ControllerActor& actor, St18ReturnFields& spawner) {
    switch (St18ReturnSub(actor.subState)) {
    case St18ReturnSub::Watch: {
        if (enemyDefeated(kIdReturningGunship)) {
            spawner.shortTimer = 0x1e;
            actor.subState = uint8_t(St18ReturnSub::Wait);
            return;
        }
        uint32_t n = 0;
        for (uint32_t k = 8; k-- > 0;) n += enemyDefeated(k + kIdFirstDropped) ? 0u : 1u;
        ShipFightGunshipParams(staticSlotAt(spawner.gunship)).shielded = uint8_t(n);
        return;
    }
    case St18ReturnSub::Wait: {
        const uint16_t t = uint16_t(spawner.shortTimer - 1u);
        spawner.shortTimer = t;
        if (t == 0) actor.subState = uint8_t(St18ReturnSub::Return);
        return;
    }
    case St18ReturnSub::Return: {
        const SpawnPoint point = st18ReturnPoints[spawner.pointIndex];
        const int16_t v[3] = {point.x, point.y, point.z};
        if (pointVisibleOrNear(v, 0) != 0) {
            enemyClearDefeated(kIdReturningGunship);
            spawnFightGunship(0, 0, point, kIdReturningGunship);
            actor.subState = uint8_t(St18ReturnSub::Watch);
        }
        spawner.pointIndex = uint8_t((spawner.pointIndex + 1u) & 3u);
        return;
    }
    default: return;
    }
}

// variant 3 = exe 0x41ecd0: the repair gauge += the rate of the machine's hit state, capped at 999999 with flag 0x2e;
// drawn as GPU packets from this state by ninoGaugeDraw
void st18Repair(St18RepairFields& spawner) {
    const int8_t hit = int8_t(spawner.machine->params[1]); // the machine's hit state
    const int32_t g = spawner.gauge + int32_t(repairRates[size_t(int32_t(hit))]);
    spawner.gauge = g;
    if (g > kRepairMax) {
        spawner.gauge = kRepairMax;
        setStoryFlag(kFlagRepaired);
    }
}

// states [0x88abc8]: 0 = 0x41e800, 1 = 0x41e970 by the variant [0x88abd4]
void st18Spawner(ControllerActor& actor) {
    switch (SpawnerState(actor.state)) {
    case SpawnerState::Init: st18Init(actor); return;
    case SpawnerState::Run:
        switch (actor.variant) {
        case 0: st18Fight(classState<St18FightFields>(actor)); return;
        case 1: // exe 0x41eb30
            if (actor.subState == 0) {
                St18TimerFields& spawner = classState<St18TimerFields>(actor);
                const uint32_t t = spawner.timer - 1u;
                spawner.timer = t;
                if (t == 0) {
                    setStoryFlag(kFlagSt18Timer);
                    actor.subState = 1;
                }
            }
            return;
        case 2: st18Return(actor, classState<St18ReturnFields>(actor)); return;
        case 3: st18Repair(classState<St18RepairFields>(actor)); return;
        default: notPortedAt(kEnemySpawnerPort, "ST18 spawner variant", actor.variant);
        }
    default: notPortedAt(kEnemySpawnerPort, "ST18 spawner 0x41e7e0 state", actor.state);
    }
}

} // namespace

void spawnerSt15Update(const PlayerEnv&, uint32_t actor) { st15Spawner(controllerActorAt(actor)); }
void spawnerSt16Update(const PlayerEnv&, uint32_t actor) { st16Spawner(controllerActorAt(actor)); }
void spawnerSt17Update(const PlayerEnv&, uint32_t actor) { st17Spawner(controllerActorAt(actor)); }
void spawnerSt18Update(const PlayerEnv&, uint32_t actor) { st18Spawner(controllerActorAt(actor)); }

// ---- the ST18 spawner's picture (the host draws it from the state the spawner left this tick; into the OT
// [0xa67840] + 0x78: the last added drawn first, emitted here in that order) ------------------------------------------
namespace {

constexpr RoutineId kSt18Spawner = RoutineId::SpawnerSt18Update;
constexpr uint8_t kRunning = 1; // the spawner's state 1
constexpr uint8_t kRepairGaugeVariant = 3;
const DataTable<RecordTable<hud::PictureQuad, 3, 0x10>> repairPlates{TableId::FieldSpawnersRepairPlates};
const DataTable<ScalarTable<uint16_t, 4>> repairDigitX{TableId::FieldSpawnersRepairDigitX};
constexpr float kRepairDigitY = 0x22;
constexpr uint32_t kHitColour = 0x2020f0;
constexpr uint8_t kMachineHitSlow = 1, kMachineHitFast = 2; // the machine's +0xd: blinks every 0x10 / 4 frames

} // namespace

std::vector<hud::Prim> ninoGaugeDraw() {
    std::vector<hud::Prim> out;
    if (!repairPlates.available()) return out;
    for (ControllerActor* c : controllersRunning(kSt18Spawner)) {
        if (c->state != kRunning || c->variant != kRepairGaugeVariant) continue;
        const St18RepairFields& f = classState<St18RepairFields>(*c);
        if (!f.machine) continue;
        uint32_t colour = 0x808080;
        const int8_t hit = int8_t(f.machine->params[1]);
        const uint16_t frame = gameState().frame;
        if ((hit == kMachineHitSlow && (frame & 0x10)) || (hit == kMachineHitFast && (frame & 4))) colour = kHitColour;
        std::vector<hud::Prim> added; // the exe's insertion order
        for (size_t k = 0; k < repairPlates.size(); ++k) added.push_back(hud::pictureQuad(repairPlates[k], colour));
        int32_t val = f.gauge, div = 100000;
        for (size_t k = 0; k < repairDigitX.size(); ++k) {
            const int32_t d = val / div;
            val %= div;
            div /= 10;
            added.push_back(hud::pictureDigit(float(repairDigitX[k]), kRepairDigitY, uint32_t(d), colour));
        }
        out.insert(out.end(), added.rbegin(), added.rend());
    }
    return out;
}

} // namespace od::port
