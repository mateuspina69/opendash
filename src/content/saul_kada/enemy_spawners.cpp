// Saul Kada's enemy spawners (pool 0xe0 type 0xa; hypothesis names): ST26 exe 0x420ac0, ST27 exe 0x420e80, ST28 exe
// 0x421030, ST2B exe 0x421070, ST34 exe 0x421210, ST3B exe 0x421e40. State 0 initialises, state 1 (2 once flag 0x56 is
// set, for the stages that have it) runs the variant: most spawn an encounter group once (their sub-state the "done"
// byte); ST26 places the giant at the window, ST2B waits for its wall to break, ST3B times the flame walls or keeps a
// breakable floor block.
#include "content/saul_kada/enemy_spawners.hpp"
#include "content/saul_kada/enemy_spawners_state.hpp"

#include "content/saul_kada/st3b_flame_wall.hpp"
#include "content/saul_kada/window_giant.hpp"

#include "game/loaded_data.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/breakable_walls.hpp"
#include "game/encounter.hpp"
#include "game/explosions.hpp"
#include "game/field_alias.hpp"
#include "game/vectors.hpp"
#include "game/global_class_types.hpp"
#include "game/hit_list.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/room_grid.hpp"
#include "game/room_records.hpp"
#include "game/shockwave.hpp"
#include "game/story_flags.hpp"

namespace od::port {

using namespace enemy_spawners_state;

namespace {

constexpr const char* kEnemySpawnerPort = "enemy spawner port"; // the area of its port-gap errors

enum class SpawnerState : uint8_t { Init = 0, Run = 1, Late = 2 };
// a point of the exe's tables: s16 x / y / z and its yaw (the giant's) / its blast size (ST2B's)
struct SpawnPoint : DataRecord {
    static constexpr uint32_t kBytes = 0x8;
    int16_t x = 0; // +0x0
    int16_t y = 0; // +0x2
    int16_t z = 0; // +0x4
    int16_t yaw = 0; // +0x6
    uint16_t size = 0; // +0x6
    template <class R> static SpawnPoint load(const R& r) {
        SpawnPoint v;
        v.x = r.template read<int16_t>(0x0);
        v.y = r.template read<int16_t>(0x2);
        v.z = r.template read<int16_t>(0x4);
        v.yaw = r.template read<int16_t>(0x6);
        v.size = r.template read<uint16_t>(0x6);
        return v;
    }
};
constexpr uint32_t kFlagLate = 0x56; // the stages' second phase (their state 2)

// exe 0x420d20 (group 0) / 0x421010 (1) / 0x420d40 (3) / 0x420d60 (5) and the like: the group spawned once
void groupOnce(ControllerActor& actor, int group) {
    if (actor.subState != 0) return;
    encounterSpawnGroupZoneSlot(group);
    actor.subState = 1;
}
// the start of ST26 (exe 0x420ae0) / ST27 (0x420ea0) / ST34 (0x421230): the stage's flag kept; state 1, or 2 once
// flag 0x56 is set
void initByFlag(ControllerActor& actor, SpawnerFields& spawner, uint32_t flag) {
    spawner.clearBytes();
    spawner.flagSet = storyFlag(flag) ? 1 : 0;
    actor.state = uint8_t(storyFlag(kFlagLate) ? SpawnerState::Late : SpawnerState::Run);
    actor.subState = 0;
    actor.clearStepBytes();
}

// ---- ST26 (states [0x88b2b0]) --------------------------------------------------------------------------------------
constexpr uint32_t kFlagSt26 = 0x3ed;
enum class GiantWatch : uint8_t { Start = 0, WaitPlayer = 1, Done = 2 };

// exe 0x420dc0(point): the giant at the window (variant 1) at the point, its yaw (exe 0x50fd60(point, 0): the position
// and the rotation, both current and previous)
void spawnGiant(const SpawnPoint& point) {
    const uint32_t slot = allocActor20();
    if (slot == 0) return;
    AnimatedActor& giant = animatedActorAt(slot);
    giant.subtype = kWindowGiantSubtype;
    giant.flags = kActorLive;
    giant.type = kWindowGiantType;
    giant.loopFlags |= kActorSweepGroup2;
    const int32_t x = int32_t(uint32_t(int32_t(point.x)) << 16);
    const int32_t y = int32_t(uint32_t(int32_t(point.y)) << 16);
    const int32_t z = int32_t(uint32_t(int32_t(point.z)) << 16);
    giant.prevPos.x = x;
    giant.pos.x = x;
    giant.prevPos.y = y;
    giant.pos.y = y;
    giant.prevPos.z = z;
    giant.pos.z = z;
    giant.variant = 1;
    giant.prevRotation.x = 0;
    giant.rotation.x = 0;
    giant.prevRotation.z = 0;
    giant.rotation.z = 0;
    giant.prevRotation.y = point.yaw;
    giant.rotation.y = point.yaw;
}
void st26(ControllerActor& actor) {
    SpawnerFields& spawner = classState<SpawnerFields>(actor);
    const int16_t px = player().pos.x.whole;
    switch (SpawnerState(actor.state)) {
    case SpawnerState::Init: initByFlag(actor, spawner, kFlagSt26); return; // exe 0x420ae0
    case SpawnerState::Run:                                                  // exe 0x420b40 ([0x88b2bc])
        switch (actor.variant) {
        case 0: // exe 0x420b60
            if (actor.subState != 0) return;
            if (spawner.flagSet != 0) encounterSpawnGroupZoneSlot(player().pos.z.whole < 0xa00 ? 1 : 2);
            actor.subState = 1;
            return;
        case 1: // exe 0x420ba0
            if (actor.subState != 0) return;
            encounterSpawnGroupZoneSlot(spawner.flagSet != 0 ? 4 : 3);
            actor.subState = 1;
            return;
        case 2: // exe 0x420be0
            if (actor.subState != 0) return;
            if (storyFlag(0x42) && !storyFlag(0x47)) encounterSpawnGroupZoneSlot(5);
            actor.subState = 1;
            return;
        case 3: // exe 0x420c20: the giant's watch ([0x88b2cc])
            switch (GiantWatch(actor.subState)) {
            case GiantWatch::Start: // exe 0x420c40: the player's side (x <= 0x2000); groups 8 / 9 with the flag, else 6 / 7
                if (spawner.flagSet != 0) {
                    actor.subState = uint8_t(GiantWatch::Done);
                    spawner.side = px > 0x2000 ? 0 : 1;
                    encounterSpawnGroupZoneSlot(px > 0x2000 ? 8 : 9);
                } else {
                    actor.subState = uint8_t(GiantWatch::WaitPlayer);
                    spawner.side = px > 0x2000 ? 0 : 1;
                    encounterSpawnGroupZoneSlot(px > 0x2000 ? 6 : 7);
                }
                return;
            case GiantWatch::WaitPlayer: // exe 0x420cb0: the player in his side's box -> the giant, once
                if (actor.step != 0) return;
                // exe 0x420e30: the exe's second copy of the cell-box test (exe 0x5e5af0)
                if (!pointInCellBox(player().pos, CellBox(stateAddress(TableId::EnemySpawnersGiantBoxes) + uint32_t(spawner.side) * 4))) return;
                spawnGiant(dataRecord<SpawnPoint>(stateAddress(TableId::EnemySpawnersGiantPoints) + uint32_t(spawner.side) * 8, SpawnPoint::kBytes));
                ++actor.step;
                return;
            case GiantWatch::Done: return; // exe 0x52ef00
            default: notPortedAt(kEnemySpawnerPort, "ST26 spawner variant 3 sub", actor.subState);
            }
        default: notPortedAt(kEnemySpawnerPort, "ST26 spawner state 1 variant", actor.variant);
        }
    case SpawnerState::Late: // exe 0x420d00 ([0x88b2f0])
        switch (actor.variant) {
        case 0: groupOnce(actor, 0); return;
        case 1: groupOnce(actor, 3); return;
        case 2: groupOnce(actor, 5); return;
        case 3: groupOnce(actor, px > 0x2000 ? 6 : 7); return; // exe 0x420d80
        default: notPortedAt(kEnemySpawnerPort, "ST26 spawner state 2 variant", actor.variant);
        }
    default: notPortedAt(kEnemySpawnerPort, "ST26 spawner 0x420ac0 state", actor.state);
    }
}

// ---- ST27 (states [0x88b300]) --------------------------------------------------------------------------------------
constexpr uint32_t kFlagSt27 = 0x43;
void st27(ControllerActor& actor) {
    SpawnerFields& spawner = classState<SpawnerFields>(actor);
    switch (SpawnerState(actor.state)) {
    case SpawnerState::Init: initByFlag(actor, spawner, kFlagSt27); return; // exe 0x420ea0
    case SpawnerState::Run: { // exe 0x420f00 ([0x88b30c]): none once flags 0x44 and 0x45 are both set
        const uint8_t variant = actor.variant;
        if (variant > 2) notPortedAt(kEnemySpawnerPort, "ST27 spawner state 1 variant", variant);
        if (storyFlag(0x44) && storyFlag(0x45)) return;
        if (actor.subState != 0) return;
        if (variant == 0) {
            encounterSpawnGroupZoneSlot(0); // exe 0x420f20
        } else if (variant == 1) {
            if (spawner.flagSet != 0) encounterSpawnGroupZoneSlot(2); // exe 0x420f60
        } else {
            encounterSpawnGroupZoneSlot(spawner.flagSet != 0 ? 4 : 3); // exe 0x420fa0
        }
        actor.subState = 1;
        return;
    }
    case SpawnerState::Late: // exe 0x420ff0 ([0x88b318])
        switch (actor.variant) {
        case 0: groupOnce(actor, 0); return;
        case 1: groupOnce(actor, 1); return;
        case 2: groupOnce(actor, 3); return;
        default: notPortedAt(kEnemySpawnerPort, "ST27 spawner state 2 variant", actor.variant);
        }
    default: notPortedAt(kEnemySpawnerPort, "ST27 spawner 0x420e80 state", actor.state);
    }
}

// ---- ST28 (states [0x88b324]) --------------------------------------------------------------------------------------
void st28(ControllerActor& actor) {
    SpawnerFields& spawner = classState<SpawnerFields>(actor);
    switch (SpawnerState(actor.state)) {
    case SpawnerState::Init: // exe 0x422cd0
        spawner.clearBytes();
        actor.enterState(uint8_t(SpawnerState::Run));
        return;
    case SpawnerState::Run: // exe 0x421050 ([0x88b32c]: 0 the group once, 1 nothing)
        if (actor.variant == 0) groupOnce(actor, 0);
        else if (actor.variant != 1) notPortedAt(kEnemySpawnerPort, "ST28 spawner variant", actor.variant);
        return;
    default: notPortedAt(kEnemySpawnerPort, "ST28 spawner 0x421030 state", actor.state);
    }
}

// ---- ST2B (states [0x88b338]) --------------------------------------------------------------------------------------
constexpr uint32_t kFlagSt2bWall = 0x58a;
constexpr uint32_t kWallColumn = 0x48, kWallRow = 0x40;
constexpr TableId kWallBlasts[3] = {TableId::EnemySpawnersFirstWallBlast, TableId::EnemySpawnersSecondWallBlast, TableId::EnemySpawnersThirdWallBlast};
enum class WallWatch : uint8_t { Wait = 0, SecondBlast = 1, LastBlast = 2 };

// exe 0x4211b0(point): a blast (the global static type 7) at the point, its size
void wallBlast(const SpawnPoint& point) {
    const uint32_t slot = allocActor60(0); // exe 0x5e1300
    if (slot == 0) return;
    StaticActor& blast = staticActorAt(slot);
    blast.loopFlags |= kActorSweepGroup2;
    blast.flags = kActorLive;
    blast.type = typeByte(GlobalStaticType::Blast);
    blast.subtype = 0;
    blast.variant = 0;
    blast.pos.x = int32_t(uint32_t(int32_t(point.x)) << 16);
    blast.pos.y = int32_t(uint32_t(int32_t(point.y)) << 16);
    blast.pos.z = int32_t(uint32_t(int32_t(point.z)) << 16);
    BlastParams params(actorHeaderAt(slot));
    params.size = point.size;
    params.attack = 0;
}
void st2b(ControllerActor& actor) {
    SpawnerFields& spawner = classState<SpawnerFields>(actor);
    switch (SpawnerState(actor.state)) {
    case SpawnerState::Init: // exe 0x421090: variant 1 with its wall already broken: freed
        spawner.clearBytes();
        if (actor.variant == 1 && storyFlag(kFlagSt2bWall)) {
            freeActor(actor);
            return;
        }
        actor.enterState(uint8_t(SpawnerState::Run));
        return;
    case SpawnerState::Run: // exe 0x4210e0 ([0x88b340]: 0 nothing, 1 exe 0x421100 the wall)
        if (actor.variant == 0) return;
        if (actor.variant != 1) notPortedAt(kEnemySpawnerPort, "ST2B spawner variant", actor.variant);
        switch (WallWatch(actor.subState)) {
        case WallWatch::Wait:
            if (!gridCellSegmentBroken(kWallColumn, kWallRow)) return;
            setStoryFlag(kFlagSt2bWall);
            wallBlast(dataRecord<SpawnPoint>(stateAddress(kWallBlasts[0]), SpawnPoint::kBytes));
            encounterSpawnGroupZoneSlot(1);
            ++actor.subState;
            return;
        case WallWatch::SecondBlast:
            wallBlast(dataRecord<SpawnPoint>(stateAddress(kWallBlasts[1]), SpawnPoint::kBytes));
            ++actor.subState;
            return;
        case WallWatch::LastBlast:
            wallBlast(dataRecord<SpawnPoint>(stateAddress(kWallBlasts[2]), SpawnPoint::kBytes));
            freeActor(actor);
            return;
        default: return;
        }
    default: notPortedAt(kEnemySpawnerPort, "ST2B spawner 0x421070 state", actor.state);
    }
}

// ---- ST34 (states [0x88b3b0]) --------------------------------------------------------------------------------------
constexpr uint32_t kFlagSt34 = 0x45;
void st34(ControllerActor& actor) {
    SpawnerFields& spawner = classState<SpawnerFields>(actor);
    const int16_t px = player().pos.x.whole;
    switch (SpawnerState(actor.state)) {
    case SpawnerState::Init: initByFlag(actor, spawner, kFlagSt34); return; // exe 0x421230
    case SpawnerState::Run:                                                  // exe 0x421290 ([0x88b3bc])
        switch (actor.variant) {
        case 0: // exe 0x4212b0
            if (spawner.flagSet == 0) groupOnce(actor, px > 0x800 ? 0 : 1);
            else groupOnce(actor, px > 0x800 ? 2 : 3);
            return;
        case 1: groupOnce(actor, 4); return;                             // exe 0x421320
        case 2: groupOnce(actor, 5); return;                             // exe 0x420d60
        case 3: groupOnce(actor, spawner.flagSet != 0 ? 7 : 6); return; // exe 0x421340
        default: notPortedAt(kEnemySpawnerPort, "ST34 spawner state 1 variant", actor.variant);
        }
    case SpawnerState::Late: // exe 0x421380 ([0x88b3cc])
        switch (actor.variant) {
        case 0: groupOnce(actor, px > 0x800 ? 0 : 1); return; // exe 0x4213a0
        case 1: groupOnce(actor, 4); return;
        case 2: groupOnce(actor, 5); return;
        case 3: groupOnce(actor, 6); return; // exe 0x422b80
        default: notPortedAt(kEnemySpawnerPort, "ST34 spawner state 2 variant", actor.variant);
        }
    default: notPortedAt(kEnemySpawnerPort, "ST34 spawner 0x421210 state", actor.state);
    }
}

// ---- ST3B (states [0x88ba88]) --------------------------------------------------------------------------------------
constexpr uint32_t kFlagSt3bFlames = 0x59f;
constexpr int16_t kFlamePeriod = 0x78, kFlameOffset = 0x3c;
constexpr uint16_t kFlameLife = 0x1e;
constexpr int16_t kFlameYaw = 0xc00;
constexpr int8_t kBlockOpen = 1, kBlockBroken = 2; // the block's parameter: its floor opened / broken at the start
constexpr int32_t kBlockSegment = 9;
// the stage's statics the block's break and its marker spawn (hypothesis names)
constexpr uint8_t kBlockDustType = 0x11, kBlockMarkerType = 0xf;
constexpr uint8_t kBlockShockVariant = 8;
// The block's dust cloud's parameters (static type 0x11; hypothesis names)
struct BlockDustParams {
    explicit BlockDustParams(ActorHeader& actor) : spread(actor.params, 0), shade(actor.params[2]), puffs(actor.params[3]) {}
    BytesField<uint16_t, 4> spread;
    uint8_t& shade;
    uint8_t& puffs;
};
// ... its marker's (static type 0xf): the block it marks
struct BlockMarkerParams {
    explicit BlockMarkerParams(const ActorHeader& actor) : BlockMarkerParams(const_cast<ActorHeader&>(actor)) {}
    explicit BlockMarkerParams(ActorHeader& actor) : block(actor.params, 0) {}
    BytesField<uint32_t, 4> block; // the block's identity word
};
enum class FlameTimer : uint8_t { Start = 0, Count = 1 };

// the centre of the 512-unit cell of an integer coordinate
int16_t cellCentre(uint16_t v) { return int16_t(uint16_t((v & 0xfe00u) + 0x100u)); }
// exe 0x422200(x, z): a floor cell at segment 0 -> segment 9 (the floor opened)
void openFloor(int32_t x, int32_t z) {
    const uint32_t cell = gridCellAtPoint(x, z); // exe 0x40f120
    if (cell == 0) return;
    const uint16_t word = GridCell(cell).word;
    if ((word & (kCellCollides | kCellFloor)) != (kCellCollides | kCellFloor) || (word & kCellSegmentNumberMask) != 0)
        return;
    cellSetSegment(uint32_t((x + 0x8000) >> kCellShift), uint32_t((z + 0x8000) >> kCellShift), kBlockSegment,
                   -1); // exe 0x600f80
}
// exe 0x422130: the block broken: a dust cloud at its point, a shockwave at the cloud, its floor opened
void breakBlock(const SpawnerFields& spawner) {
    const int16_t x = cellCentre(uint16_t(spawner.x)), z = cellCentre(uint16_t(spawner.z));
    const uint32_t slot = allocActor60(kActorStageClass);
    if (slot == 0) return;
    StaticActor& dust = staticActorAt(slot);
    dust.flags = kActorLive;
    dust.loopFlags |= kActorSweepGroup2 | kActorLateLoop;
    dust.type = kBlockDustType;
    dust.variant = 0;
    dust.pos.x.whole = spawner.x;
    dust.pos.y.whole = spawner.y;
    dust.pos.z.whole = spawner.z;
    BlockDustParams dustParams(dust);
    dustParams.spread = 0x100;
    dustParams.shade = 0x80;
    dustParams.puffs = 8;
    if (const uint32_t shockSlot = allocActor40(kActorStageClass)) { // a shockwave at the cloud
        ProjectileActor& shock = projectileActorAt(shockSlot);
        shock.flags = kActorLive;
        shock.loopFlags |= kActorSweepGroup2;
        shock.type = kShockwaveStageType;
        shock.variant = kBlockShockVariant;
        ShockwaveParams shockParams(projectileActorAt(shockSlot));
        shockParams.hitMode = kShockHitEdge;
        shockParams.downward = 0;
        shock.pos = dust.pos;
    }
    openFloor(x, z);
}
// exe 0x421f70(point, life): a flame wall at the point, yaw 0xc00, its life
void spawnFlameWall(uint32_t point, uint16_t life) {
    const uint32_t slot = allocActor60(kActorStageClass);
    if (slot == 0) return;
    StaticActor& flame = staticActorAt(slot);
    const ShortVec3 where = dataShortVec3(point);
    flame.subtype = 0;
    flame.variant = 0;
    flame.flags = kActorLive;
    flame.loopFlags |= kActorLateLoop;
    flame.type = kFlameWallType;
    flame.pos.x.whole = where.x;
    flame.pos.y.whole = where.y;
    flame.pos.z.whole = where.z;
    flame.rotation.y = kFlameYaw;
    FlameWallParams(staticActorAt(slot)).life = life;
}
// exe 0x421fc0: the breakable block: the player within its 3 x 3 cells -> its marker (once while near); the player
// standing in its cell above it, or a body (group 2) / a strong push of the player's attacks (group 1) in its cell
// reaching its height -> broken (that entry's box told it hit a solid), freed
void floorBlock(ControllerActor& actor, SpawnerFields& spawner) {
    const Player& p = player();
    const int32_t px = int32_t(p.pos.x.whole) >> kCellShift, pz = int32_t(p.pos.z.whole) >> kCellShift;
    const int32_t bx = int32_t(spawner.x) >> kCellShift, bz = int32_t(spawner.z) >> kCellShift;
    const bool near = uint32_t(px - bx + 1) <= 2 && uint32_t(pz - bz + 1) <= 2;
    if (spawner.markerShown == 0) {
        if (near) {
            if (const uint32_t slot = allocActor60(kActorStageClass)) {
                StaticActor& marker = staticActorAt(slot);
                marker.flags = kActorLive;
                marker.loopFlags |= kActorSweepGroup2;
                marker.type = kBlockMarkerType;
                marker.variant = 0;
                marker.pos.y.whole = 0;
                marker.pos.x.whole = spawner.x;
                marker.pos.z.whole = spawner.z;
                BlockMarkerParams(actorHeaderAt(slot)).block = slotIdentityWord(actor);
                spawner.markerShown = 1;
            }
        }
    } else if (!near) {
        spawner.markerShown = 0;
    }
    if (px == bx && pz == bz && p.airborne == 0 && int32_t(p.pos.y.whole) > int32_t(spawner.y) - 0x10) {
        breakBlock(spawner);
        freeActor(actor);
        return;
    }
    ActorSystemState& system = actorSystem();
    uint32_t n = system.hitCount;
    for (size_t k = 0; n != 0; --n, ++k) {
        const HitEntry& entry = system.hitList[k];
        const uint32_t group = entry.owner & kHitGroupsAll;
        if (!(group == hitGroup(kHitGroupBodies) ||
              (group == hitGroup(kHitGroupPlayerAttacks) && (entry.attack & kHitAttackStrongPush))))
            continue;
        if ((int32_t(entry.pos.x) >> kCellShift) != bx || (int32_t(entry.pos.z) >> kCellShift) != bz) continue;
        const int32_t radius = int32_t(uint16_t(entry.shape()) & kHitRadiusMask) << kHitRadiusScale;
        if (int32_t(entry.pos.y) + radius < int32_t(spawner.y)) continue;
        breakBlock(spawner);
        entry.box->result |= kHitAttackSolid;
        freeActor(actor);
        return;
    }
}
void st3b(ControllerActor& actor) {
    SpawnerFields& spawner = classState<SpawnerFields>(actor);
    switch (SpawnerState(actor.state)) {
    case SpawnerState::Init: // exe 0x421e60
        spawner.clearBytes();
        if (actor.variant == 0) {
            if (storyFlag(kFlagSt3bFlames)) {
                freeActor(actor);
                return;
            }
        } else if (actor.variant == 1) { // the block's point snapped to its cell's centre; its floor opened / broken
            const int16_t x = cellCentre(uint16_t(spawner.x)), z = cellCentre(uint16_t(spawner.z));
            spawner.x = x;
            spawner.z = z;
            const int8_t start = int8_t(actor.params[0]);
            if (start == kBlockOpen) {
                openFloor(x, z);
            } else if (start == kBlockBroken) {
                breakBlock(spawner);
                freeActor(actor);
                return;
            }
        }
        actor.enterState(uint8_t(SpawnerState::Run));
        return;
    case SpawnerState::Run: // exe 0x421f00 ([0x88ba90])
        switch (actor.variant) {
        case 0: { // exe 0x421f20: every 120 ticks the two flame walls, 60 ticks apart
            if (FlameTimer(actor.subState) == FlameTimer::Start) {
                actor.subState = uint8_t(FlameTimer::Count);
                spawner.countdown = kFlamePeriod;
            }
            const int32_t left = spawner.countdown;
            if (left == kFlameOffset) spawnFlameWall(stateAddress(TableId::EnemySpawnersFlamePointB), kFlameLife);
            else if (left == kFlamePeriod) spawnFlameWall(stateAddress(TableId::EnemySpawnersFlamePointA), kFlameLife);
            spawner.countdown = int16_t(uint16_t(spawner.countdown) - 1u);
            if (spawner.countdown == 0) actor.subState = uint8_t(FlameTimer::Start);
            return;
        }
        case 1: floorBlock(actor, spawner); return;
        default: notPortedAt(kEnemySpawnerPort, "ST3B spawner variant", actor.variant);
        }
    default: notPortedAt(kEnemySpawnerPort, "ST3B spawner 0x421e40 state", actor.state);
    }
}

} // namespace

void spawnerSt26Update(const PlayerEnv&, uint32_t actor) { st26(controllerActorAt(actor)); }
void spawnerSt27Update(const PlayerEnv&, uint32_t actor) { st27(controllerActorAt(actor)); }
void spawnerSt28Update(const PlayerEnv&, uint32_t actor) { st28(controllerActorAt(actor)); }
void spawnerSt2bUpdate(const PlayerEnv&, uint32_t actor) { st2b(controllerActorAt(actor)); }
void spawnerSt34Update(const PlayerEnv&, uint32_t actor) { st34(controllerActorAt(actor)); }
void spawnerSt3bUpdate(const PlayerEnv&, uint32_t actor) { st3b(controllerActorAt(actor)); }

} // namespace od::port
