// ST55 licence exam actor classes, batch B (see exam_enemies_b.hpp). Left out (render output): the shadows (exe
// 0x5f20f0) and the marker's GT4 band of OT packets.
#include "content/manda/exam_enemies_b.hpp"
#include "content/manda/exam_enemies_b_state.hpp"

#include <cstdlib>

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
#include "game/camera_state.hpp"
#include "game/carry.hpp"
#include "game/distances.hpp"
#include "game/draw_commands.hpp"
#include "game/encounter.hpp"
#include "game/enemy_motion.hpp"
#include "game/enemy_neighbours.hpp"
#include "game/explosions.hpp"
#include "game/field_alias.hpp"
#include "game/vectors.hpp"
#include "game/game_state.hpp"
#include "game/global_class_types.hpp"
#include "game/glow_trails.hpp"
#include "game/hit_list.hpp"
#include "game/hit_points.hpp"
#include "game/hud.hpp"
#include "game/matrix.hpp"
#include "game/pickups.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/room_grid.hpp"
#include "game/script_state.hpp"
#include "game/story.hpp"
#include "game/territory.hpp"
#include "game/visibility.hpp"

using namespace od::port::exam_enemies_b_state;

namespace od::port {

using namespace exam_enemies_b_state;

namespace {

constexpr uint16_t kTintHurtBlink = 0x4214;
constexpr uint16_t kSoundHurt = 0x8d;

// the copies at the top of the classes' updates (the previous pose, the airborne / platform bytes)
void savePrevious(Actor& a) {
    a.savePreviousPose();
    a.savePreviousContact();
}
// bone k's point (exe 0x5e1810) as an s16 x / y / z
void bonePointOf(const AnimatedActor& a, uint32_t k, int16_t out[3]) {
    const ShortVec4& p = bonePoint(a, k, kStoryPort);
    out[0] = p.x;
    out[1] = p.y;
    out[2] = p.z;
}
// a death explosion (the global static type 8) at the point
void explosionAt(int16_t x, int16_t y, int16_t z, uint16_t spread, uint16_t flashes) {
    const uint32_t address = allocActor60(0); // exe 0x5e1300
    if (!address) return;
    StaticActor& e = staticActorAt(address);
    e.flags = kActorLive;
    e.loopFlags |= kActorSweepGroup2;
    e.type = typeByte(GlobalStaticType::Explosion);
    e.variant = 0;
    e.pos.x.whole = x;
    e.pos.y.whole = y;
    e.pos.z.whole = z;
    ExplosionParams params(e);
    params.spread = spread;
    params.flashes = flashes;
}

// ==== the exam drone (exe 0x5aa920: pool 0x60 type 0x2b, states [0x8cf2d4]) ========================================
enum class DroneState : uint8_t {
    Init = 0,  // exe 0x5aa970
    Fly = 1,   // exe 0x5aaa00 (sub-states [0x8cf2e4])
    Burst = 2, // exe 0x5aaca0
    Gone = 3,  // exe 0x5aad20
};
enum class DroneSub : uint8_t { Rise = 0, Drift = 1, Chase = 2, Idle = 3 }; // 0x5aaa20 / 0x5aaa60 / 0x5aab20 / 0x52ef00

const DataTable<CollisionBoxRecord> droneBox{TableId::ExamEnemiesBDroneBox};
constexpr uint8_t kExamDroneType = 0x2b;
constexpr int16_t kMaxDrones = 0x10;

// exe 0x5aa970: the model (any variant) or freed; above the hatch, shown, its box, still
void droneInit(StaticActor& a, DroneFields& f) {
    if (!bindActorModel(a, kAnyModelVariant)) { // exe 0x5e0bc0(actor, 0)
        freeActor(a);                           // exe 0x5e1540
        return;
    }
    a.pos.y.whole = int16_t(uint16_t(a.pos.y.whole) + 0x64u);
    a.pos.z.whole = int16_t(uint16_t(a.pos.z.whole) + 0x32u);
    a.flags |= kActorDrawn;
    a.enterState(uint8_t(DroneState::Fly));
    a.acceleration.y = 1;
    a.collisionBox.pointAt(droneBox.stateAddress());
    a.velocity.z = 0;
    a.velocity.y = 0;
    a.velocity.x = 0;
    a.airborne = 0;
    a.onPlatform = 0;
    a.platform = nullptr;
    a.actorHit = 0;
    a.clearClassBlock(); // exe 0x40dc40
    f.hitBox.previousX = a.pos.x.whole;
    f.hitBox.previousZ = a.pos.z.whole;
    f.hitBox.result = 0;
}
// the sub-state's timer down; at 0 the next sub-state
void droneTimedSub(StaticActor& a, DroneFields& f) {
    --f.timer;
    if (f.timer == 0) {
        a.step = 0;
        ++a.subState;
    }
}
// exe 0x5aaa20: 6 ticks rising out of the hatch (forward 0x80), then the drift
void droneRise(StaticActor& a, DroneFields& f) {
    if (a.step == 0) {
        a.step = 1;
        f.timer = 6;
    }
    droneTimedSub(a, f);
    integrateVelocity(a, 0, 0, -0x80); // exe 0x5e3690
}
// exe 0x5aaa60: 16 ticks drifting along a random direction (rand & 0x9ff + 0x300) at 200 units, then the chase
void droneDrift(StaticActor& a, DroneFields& f) {
    if (a.step == 0) {
        a.step = 1;
        f.timer = 0x10;
        const int angle = int(((crtRand() & 0x9ff) + 0x300) & kAngleMask);
        auto scaled = [](int v) { return int16_t((v * 200 / 16) >> 8); };
        a.velocity.x = scaled(fixedSin(angle));
        a.velocity.y = scaled(fixedCos(angle));
        a.velocity.z = 0; // the spare lane: the exe stores an uninitialised stack word (left unchanged)
    }
    droneTimedSub(a, f);
    integrateVelocity(a, a.velocity.x, a.velocity.y, 0);
}
// exe 0x5aac40: blocked: pushed out (gave up: back to the previous position); free: the territory's push
void droneCollide(const PlayerEnv& env, StaticActor& a) {
    if (int32_t(actorTestMove(env, a)) >= 0) { // exe 0x40f570
        if (const int rect = territoryViolated(a.pos)) territoryPushBack(a, rect); // exe 0x5e6ab0 / 0x5e6b40
        return;
    }
    const uint32_t r = actorPushOut(env, a); // exe 0x410960
    if (int32_t(a.actorHit | r) < 0) a.pos = a.prevPos;
}
// exe 0x5aabe0: its hit entry (0x14 up) and the mover queued
void droneHit(StaticActor& a, DroneFields& f) {
    const int16_t p[3] = {a.pos.x.whole, int16_t(uint16_t(a.pos.y.whole) - 0x14u), a.pos.z.whole};
    hitListSubmit(p, f.hitBox, hitOwner(a.slot) | hitGroup(kHitGroupBodies) | kHitCompareHeights | hitRadius(0x28),
                  kHitAttackSolid); // exe 0x5e4120
    actorCollisionSubmitMover(a);   // exe 0x4140b0
}
// exe 0x5aaca0 (state 2, hit): hidden, a blast at it; 3 ticks -> gone
void droneBurst(StaticActor& a, DroneFields& f) {
    if (a.subState == 0) {
        a.subState = 1;
        a.flags ^= kActorDrawn;
        f.timer = 3;
        if (const uint32_t address = allocActor60(0)) { // exe 0x5e1300
            StaticActor& e = staticActorAt(address);
            e.flags = kActorLive;
            e.loopFlags |= kActorSweepGroup2;
            e.type = typeByte(GlobalStaticType::Blast);
            e.variant = 0;
            e.pos.x.whole = a.pos.x.whole;
            e.pos.y.whole = int16_t(uint16_t(a.pos.y.whole) - 0x14u);
            e.pos.z.whole = a.pos.z.whole;
            BlastParams params(e);
            params.size = 0x50;
            params.attack = 0xa;
        }
    }
    --f.timer;
    if (f.timer == 0) a.enterState(uint8_t(DroneState::Gone));
}
// exe 0x5aab20: the chase -- every 0x5a ticks a new height offset (+-(rand & 0x1f) + 0xa, the sign alternating); turned
// to the player, forward 0xa0 with the vertical speed toward its chest + the offset; hit -> the burst
void droneChase(const PlayerEnv& env, StaticActor& a, DroneFields& f) {
    f.retarget = int16_t(uint16_t(f.retarget) - 1u);
    if (f.retarget < 0) {
        f.retarget = 0x5a;
        int e = int((crtRand() & 0x1f) + 0xa);
        a.step = uint8_t(~a.step);
        if (a.step != 0) e = -e;
        f.heightOffset = int16_t(e);
    }
    const int dy = (f.heightOffset - a.pos.y.whole + player().pos.y.whole - 0x64) >> 2;
    turnYawTo(a.rotation.y, uint32_t(angleOf(player().pos, a.pos)), 0x10); // exe 0x5e3bc0 / 0x5e3df0
    integrateVelocity(a, 0, dy, -0xa0);
    droneCollide(env, a);
    if (f.hitBox.result & kHitDamageMask) {
        a.enterState(uint8_t(DroneState::Burst));
        droneBurst(a, f);
        return;
    }
    droneHit(a, f);
}
// exe 0x5aa920
void droneUpdate(const PlayerEnv& env, StaticActor& a) {
    auto&& f = classState<DroneFields>(a);
    savePrevious(a);
    switch (DroneState(a.state)) {
    case DroneState::Init: droneInit(a, f); return;
    case DroneState::Fly:
        switch (DroneSub(a.subState)) {
        case DroneSub::Rise: droneRise(a, f); return;
        case DroneSub::Drift: droneDrift(a, f); return;
        case DroneSub::Chase: droneChase(env, a, f); return;
        case DroneSub::Idle: return; // exe 0x52ef00
        default: notPortedAt(kStoryPort, "0x5aa920 sub", a.subState);
        }
    case DroneState::Burst: droneBurst(a, f); return;
    case DroneState::Gone: // exe 0x5aad20: one drone less, freed
        examDroneCount().alive = int16_t(uint16_t(examDroneCount().alive) - 1u);
        freeActor(a); // exe 0x5e1540
        return;
    default: notPortedAt(kStoryPort, "0x5aa920 state", a.state);
    }
}

// ==== the ambush (exe 0x426df0: pool 0xe0 type 0x21, states [0x88bfd4]) ==============================================
// Its variant picks a record list [0x88bfc8 + v * 4] (a count of 0x18-byte records) and three cell boxes 0x88bfe0 +
// v * 0xc (the wake box, the near boxes A / B). The spawned actors' words, their enemy group, the woken one.
enum class AmbushState : uint8_t { Spawn = 0, Watch = 1, Free = 2 }; // exe 0x426e10 / 0x426e80 / 0x427cf0
enum class AmbushSub : uint8_t { Sleeping = 0, Awake = 1, Alerted = 2 };

constexpr uint32_t kAmbushDefeated = 0xffffffffu;
// a record of the exe's data (0x18 bytes)
struct AmbushRecord : DataRecord {
    uint8_t flags = 0;
    uint32_t typeWord = 0, paramsWord = 0;
    int16_t x = 0, y = 0, z = 0, yaw = 0;
    uint32_t id = 0;
    static AmbushRecord load(const TableReader& r) {
        return {{}, r.u8(0), r.u32(4), r.u32(8), r.s16(0xc), r.s16(0xe), r.s16(0x10), r.s16(0x12), r.u32(0x14)};
    }
};
constexpr uint32_t kAmbushRecordSize = 0x18;
// by variant (exe table 0x88bfc8: a pointer per variant to its records)
struct AmbushLists : DataRecord {
    static constexpr size_t kVariants = 4, kMaxRecords = 0x20;
    static constexpr uint32_t kBytes = kVariants * 4;
    std::array<TableReader, kVariants> lists;
    AmbushRecord record(size_t variant, size_t k) const {
        if (variant >= kVariants) tableIndexError(variant, kVariants);
        return AmbushRecord::load(lists[variant].sub(k * kAmbushRecordSize, kAmbushRecordSize));
    }
    static AmbushLists load(const TableReader& r) {
        AmbushLists t;
        for (size_t v = 0; v < kVariants; ++v) t.lists[v] = r.follow(v * 4, kMaxRecords * kAmbushRecordSize);
        return t;
    }
};
const DataTable<AmbushLists> ambushLists{TableId::ExamAmbushLists};
// the boxes of a variant (exe data 0x88bfe0, 0xc bytes)
struct AmbushBoxes : DataRecord {
    CellBoxValues wake{}, nearA{}, nearB{};
    static CellBoxValues box(const TableReader& r, size_t o) { return {r.s8(o), r.s8(o + 1), r.s8(o + 2), r.s8(o + 3)}; }
    static AmbushBoxes load(const TableReader& r) { return {{}, box(r, 0), box(r, 4), box(r, 8)}; }
};
const DataTable<RecordTable<AmbushBoxes, 4, 0xc>> ambushBoxes{TableId::ExamAmbushBoxes};
constexpr uint8_t kAmbushFlyerType = 0x67;

// The ambush flyer's wake byte (its spawn parameter 0: 1 woken, 2 the group alerted), written by the ambush through
// its actor's word (unchecked as the exe: a defeated record's word -1 included)
uint8_t& flyerWake(uint32_t flyer) { return actorHeaderAt(flyer).params[0]; } // its +0xc

// exe 0x427020(record): an un-defeated record spawned as a pool-0x20 actor (tracked in the script slot); kAmbushDefeated
// when defeated, 0 when the pool is full
uint32_t ambushSpawn(const AmbushRecord& rec) {
    if (enemyDefeated(rec.id)) return kAmbushDefeated; // exe 0x5e6420
    const uint32_t address = allocActor20();           // exe 0x5e1160
    if (address) {
        AnimatedActor& e = animatedActorAt(address);
        e.flags = rec.flags;
        e.loopFlags |= kActorSweepGroup2;
        e.setTypeWord(rec.typeWord);
        e.setParamsWord(rec.paramsWord);
        e.pos.x = int32_t(uint32_t(int32_t(rec.x)) << 16);
        e.pos.y = int32_t(uint32_t(int32_t(rec.y)) << 16);
        e.pos.z = int32_t(uint32_t(int32_t(rec.z)) << 16);
        e.rotation.y = rec.yaw;
        encounterTrackInScriptSlot(e, uint16_t(rec.id)); // exe 0x5e6500
    }
    return address;
}
// exe 0x426e10: the records spawned; the enemy group of the first one
void ambushSpawnAll(ControllerActor& c, AmbushFields& f) {
    f.group = 0xff;
    for (int8_t k = 0; k < f.records; ++k) {
        const uint32_t r = ambushSpawn(ambushLists->record(c.variant, size_t(k)));
        f.spawned[size_t(k)] = r;
        if (r != kAmbushDefeated && f.group == 0xff) {
            if (r == 0) notPorted(kStoryPort, "0x426e10: exe 0x5e6cd0 on a null actor");
            f.group = uint8_t(enemyGroupFindFor(animatedActorAt(r), 1)); // exe 0x5e6cd0(actor, 1)
        }
    }
    c.subState = uint8_t(AmbushSub::Sleeping);
    ++c.state;
    f.woken = 0xff;
}
// the first live flyer woken (its wake byte 1), remembered, the group marked; whether one woke
bool ambushWake(AmbushFields& f) {
    for (int8_t k = 0; k < f.records; ++k) {
        const uint32_t e = f.spawned[size_t(uint8_t(k))];
        if (e == kAmbushDefeated || e == 0) continue;
        const AnimatedActor& flyer = animatedActorAt(e);
        if (flyer.flags == 0 || flyer.type != kAmbushFlyerType) continue;
        flyerWake(e) = 1;
        f.woken = uint8_t(k);
        enemyGroupMark(int32_t(f.group), 1); // exe 0x5e6d40 (then the count: the loop ends)
        return true;
    }
    return false;
}
// exe 0x426e80: the player in the wake box wakes one; in a near box (the group woken) alerts the woken one; the group
// gone -> asleep again
void ambushWatch(ControllerActor& c, AmbushFields& f) {
    const AmbushBoxes& boxes = ambushBoxes[c.variant];
    const int32_t g = int32_t(f.group);
    const Fixed16Vec3& pos = player().pos;
    switch (AmbushSub(c.subState)) {
    case AmbushSub::Sleeping: // exe 0x426f91
        if (enemyGroupMarks(g, EnemyGroupMarks::Either) != 0) {
            ++c.subState;
            return;
        }
        if (!pointInCellBox(pos.x.whole, pos.z.whole, boxes.wake)) return;
        if (ambushWake(f)) ++c.subState;
        return;
    case AmbushSub::Awake: // exe 0x426ebc
        if (f.holdNear == 0 && (pointInCellBox(pos.x.whole, pos.z.whole, boxes.nearA) || pointInCellBox(pos.x.whole, pos.z.whole, boxes.nearB))) {
            if (enemyGroupMarks(g, EnemyGroupMarks::Either) == 0) return;
            flyerWake(f.spawned[f.woken]) = 2;
            c.subState = uint8_t(AmbushSub::Alerted);
            return;
        }
        if (enemyGroupMarks(g, EnemyGroupMarks::Either) != 0) return;
        ambushWake(f);
        return;
    case AmbushSub::Alerted: // the group gone -> asleep
        if (enemyGroupMarks(g, EnemyGroupMarks::Either) == 0) c.subState = uint8_t(AmbushSub::Sleeping);
        return;
    default: return;
    }
}
// exe 0x426df0
void ambushUpdate(ControllerActor& c) {
    auto&& f = classState<AmbushFields>(c);
    switch (AmbushState(c.state)) {
    case AmbushState::Spawn: ambushSpawnAll(c, f); return;
    case AmbushState::Watch: ambushWatch(c, f); return;
    case AmbushState::Free: freeActor(c); return; // exe 0x427cf0
    default: notPortedAt(kStoryPort, "0x426df0 state", c.state);
    }
}

// ==== the drone hatch (exe 0x4da900: pool 0x20 type 0x4f, states [0x895744], sub-states [0x895754]) ================
// While the player stands in its cell box (its spawn parameters: 512-unit cells) it releases a drone every 0x1e ticks
// (at most 0x10 alive), sound 0x245.
enum class HatchState : uint8_t { Init = 0, Run = 1, Destroyed = 2, Gone = 3 }; // 0x4daa00 / 0x4daad0 / 0x4dac60
enum class HatchSub : uint8_t {
    Open = 0,      // exe 0x4dab10
    Closed = 1,    // exe 0x4dac20
    Rebind = 2,    // exe 0x4dae60
    ScriptMark = 3, // exe 0x4daf00: [0x895768 + variant * 4](actor, the script block)
    Free = 4,      // exe 0x468fa0
};
constexpr uint8_t kHatchNoHitEntry = 0x1, kHatchNoHurt = 0x2;
const DataTable<CollisionBoxRecord> hatchBox{TableId::ExamEnemiesBHatchBox};
constexpr uint16_t kSoundRelease = 0x245;
constexpr uint16_t kReleaseTicks = 0x1e;
constexpr int32_t kHatchScriptTick = 0x53b;

// exe 0x4daa00: the model, the scale 1.0, the hit points (the row's << 2), its box, registered
void hatchInit(AnimatedActor& a, HatchFields& f) {
    if (!bindActorModel(a, kAnyModelVariant)) { // exe 0x5e0bc0(actor, 0)
        freeActor(a);                           // exe 0x5e1490
        return;
    }
    a.anim = 0;
    resetModelScale(a);
    a.prevAnim = 0xff;
    const uint16_t hp = hasAnimatedStats(a.type) ? uint16_t(animatedStats(a.type).hitPoints << 2) : 0;
    a.maxHitPoints = int16_t(hp);
    a.hitPoints = int16_t(hp);
    a.collisionBox.pointAt(hatchBox.stateAddress());
    a.airborne = 0;
    a.onPlatform = 0;
    a.clearClassBlock(); // exe 0x40dc40
    f.hitBox.previousX = a.pos.x.whole;
    f.hitBox.previousZ = a.pos.z.whole;
    f.hitBox.result = 0;
    f.releaseTimer = kReleaseTicks;
    a.enterState(uint8_t(HatchState::Run));
    registerEnemy(a.slot, 0); // exe 0x5e5c10
}
// exe 0x4dab10: open -- every 0x1e ticks a drone while the player is in its box; it spins
void hatchOpen(AnimatedActor& a, HatchFields& f) {
    if (a.step == 0) {
        f.mode = 0;
        a.enterStep(1);
        a.tint = kTintNeutral;
        a.anim = 0;
    }
    const uint16_t t = f.releaseTimer;
    f.releaseTimer = uint16_t(t - 1u);
    if (t == 0) {
        f.releaseTimer = kReleaseTicks;
        const Player& p = player();
        const int px = (p.pos.x.whole + 0x8000) >> 9, pz = (p.pos.z.whole + 0x8000) >> 9;
        ExamDroneCount& count = examDroneCount();
        if (f.cellX0 <= px && f.cellX1 >= px && f.cellZ0 <= pz && f.cellZ1 >= pz && count.alive < kMaxDrones) {
            count.alive = int16_t(uint16_t(count.alive) + 1u);
            if (const uint32_t address = allocActor60(kActorStageClass)) { // exe 0x5e12f0
                StaticActor& e = staticActorAt(address);
                e.loopFlags |= kActorSweepGroup2;
                e.flags = kActorLive;
                e.type = kExamDroneType;
                e.subtype = 0;
                e.variant = 0;
                e.spawnByte = 0;
                e.pos = a.pos;
                e.rotation.x = 0;
                e.pos.y.whole = int16_t(uint16_t(e.pos.y.whole) - 0x60u);
                e.rotation.z = 0;
                e.rotation.y = a.rotation.y;
                soundPlayAtPos(kSoundRelease, e.pos); // exe 0x5d5380
            }
        }
    }
    a.rotation.z = int16_t(uint16_t(a.rotation.z) + 0x20u);
}
// exe 0x4dac20: closed (no hits) until its animation ends -> open
void hatchClosed(AnimatedActor& a, HatchFields& f) {
    if (a.step == 0) {
        f.mode = kHatchNoHitEntry | kHatchNoHurt;
        a.enterStep(1);
        a.tint = kTintNeutral;
        a.anim = 1;
    }
    if (a.animEnded()) {
        a.subState = uint8_t(HatchSub::Open);
        a.restartStep();
    }
}
// exe 0x4dae60: the model of variant 0 again, the animation 1 / 2 by the variant, still, open
void hatchRebind(AnimatedActor& a, HatchFields& f) {
    if (!bindActorModel(a, 0)) { // exe 0x5e0ba0
        freeActor(a);            // exe 0x5e1490
        return;
    }
    a.flags |= kActorDrawn;
    a.anim = a.variant == 0 ? 1 : 2;
    a.prevAnim = 0xff;
    animSetPose(a, a.anim, 0); // exe 0x5e1c30
    a.velocity.x = 0;
    a.velocity.y = 0;
    a.velocity.z = 0;
    a.rotation.z = 0;
    a.rotation.x = 0;
    a.airborne = 0;
    a.onPlatform = 0;
    a.platform = nullptr;
    a.actorHit = 0;
    a.collisionBox = nullptr;
    f.hitBox.result = 0;
    f.scriptMark = 0;
    a.subState = uint8_t(HatchSub::Open);
    a.restartStep();
    a.state = uint8_t(HatchState::Run);
}
// exe 0x4dad40: the hit box's damage (a thrown / pushing hit closes it: sub-state 1); dead -> destroyed
void hatchHurt(AnimatedActor& a, HatchFields& f) {
    const uint32_t h = f.hitBox.result, d = h & kHitDamageMask;
    if (d == 0) return;
    uint32_t dmg = 0;
    if (h & (kHitAttackThrown | kHitAttackStrongPush)) {
        a.tint = kTintFlash;
        a.subState = uint8_t(HatchSub::Closed);
        a.restartStep();
        dmg = d;
    } else if (h & kHitAttackByPlayer) {
        a.tint = kTintFlash;
        dmg = d;
    }
    if (h & (kHitLevelAbove | kHitLevelWithin | kHitLevelBelow)) {
        a.tint = kTintFlash;
        dmg = d;
    }
    if (hitPointsAdd(a, -int(dmg)) != 0) a.enterState(uint8_t(HatchState::Destroyed)); // exe 0x5e52f0
}
// exe 0x4dac60: destroyed -- animation 2, an explosion, blinking 0x14 ticks, the drops, freed
void hatchDestroyed(AnimatedActor& a, HatchFields& f) {
    a.tint = kTintFlash;
    if (a.subState == 0) {
        a.subState = 1;
        a.anim = 2;
        f.blinkTicks = 0x14;
        a.velocity.x = 0;
        a.velocity.y = 0;
        a.velocity.z = 0;
        a.acceleration.y = 0x40;
        explosionAt(a.pos.x.whole, int16_t(uint16_t(a.pos.y.whole) - 0x60u), a.pos.z.whole, 0x30, f.blinkTicks);
    }
    if (uint8_t(f.blinkTicks) & 1) a.tint = kTintHurtBlink;
    --f.blinkTicks;
    if (f.blinkTicks == 0) {
        spawnDrops(a.pos, animatedStats(a.type).dropTable); // exe 0x5f1270
        freeActor(a);                                        // exe 0x5e1490
    }
}
// exe 0x4da900
void hatchUpdate(AnimatedActor& a) {
    HatchFields& f = classState<HatchFields>(a);
    a.prevAnim = a.anim;
    savePrevious(a);
    switch (HatchState(a.state)) {
    case HatchState::Init: hatchInit(a, f); break;
    case HatchState::Run: // exe 0x4daad0
        a.tint = kTintNeutral;
        switch (HatchSub(a.subState)) {
        case HatchSub::Open: hatchOpen(a, f); break;
        case HatchSub::Closed: hatchClosed(a, f); break;
        case HatchSub::Rebind: hatchRebind(a, f); break;
        case HatchSub::ScriptMark:
            if (a.variant == 1) {
                if (scriptState().stepTick == kHatchScriptTick) f.scriptMark = 1; // exe 0x4daf20
            } else if (a.variant != 0) {
                notPortedAt(kStoryPort, "0x4daf00 variant", a.variant);
            }
            break;
        case HatchSub::Free: freeActor(a); break; // exe 0x468fa0
        default: notPortedAt(kStoryPort, "0x4da900 sub", a.subState);
        }
        if (!(f.mode & kHatchNoHurt)) hatchHurt(a, f);
        break;
    case HatchState::Destroyed: hatchDestroyed(a, f); break;
    case HatchState::Gone: freeActor(a); break; // exe 0x468fa0
    default: notPortedAt(kStoryPort, "0x4da900 state", a.state);
    }
    if (!(a.flags & kActorLive)) return;
    if (a.anim == a.prevAnim) animStepPose(a); // exe 0x5e1e80
    else animSetPose(a, a.anim, 0);            // exe 0x5e1c30
    const AnimatedActorStats stats = animatedStats(a.type);
    f.hitBox.result = 0;
    f.hitBox.previousX = a.prevPos.x.whole;
    f.hitBox.previousZ = a.prevPos.z.whole;
    if (f.mode & kHatchNoHitEntry) return;
    int16_t p[3];
    bonePointOf(a, 0, p);
    const uint32_t owner = hitOwner(a.slot) | hitHeightLevel(stats.hitHeight) | hitGroup(kHitGroupBodies) |
                           kHitCompareHeights | kHitContactMotion | hitRadius(stats.bodyAttack); // (the row's +8)
    hitListSubmit(p, f.hitBox, owner, uint32_t(stats.attack) | kHitAttackSolid); // exe 0x5e4120
}

// ==== the floating mine (exe 0x592580: pool 0x60 type 4, states [0x8cdf14], sub-states [0x8cdf20]) ==================
// It bobs on the 64-step wave around its base height, sways, turns to the player when alerted and homes on the player's
// bone point 0; near it, it flashes and bursts. Its two glows ride it (pool 0xa0 stage type 6).
enum class MineState : uint8_t { Init = 0, Run = 1, Burst = 2 }; // exe 0x592600 / 0x5926d0 / 0x592c00
enum class MineSub : uint8_t {
    Reset = 0,  // exe 0x592760
    Idle = 1,   // exe 0x592780
    Home = 2,   // exe 0x592850
    Fuse = 3,   // exe 0x592ac0
    Return = 4, // exe 0x592b50
};
enum class MineBurst : uint8_t { Killed = 0, Fused = 1 };
const DataTable<CollisionBoxRecord> mineBox{TableId::ExamEnemiesBMineBox};
constexpr uint16_t kSoundFuse = 0xc3;
constexpr uint32_t kMineLayers = 2;
constexpr uint8_t kMineGlowSize = 6;

// the player's bone point 0 (the homing point)
void homingPoint(int16_t out[3]) {
    const ShortVec3& p = player().bonePoints[0];
    out[0] = p.x;
    out[1] = p.y;
    out[2] = p.z;
}
// exe 0x592d00: the two glows (stage effect type 6, variant 1, at x +-8, y / z -0x1d) for the free glow slots
void mineGlows(StaticActor& a, MineFields& f) {
    for (int k = 1; k >= 0; --k) {
        if (f.glows[size_t(k)] != 0) continue;
        const uint32_t address = allocActorA0(kActorStageClass); // exe 0x5e13a0
        if (!address) continue;
        EffectActor& e = effectActorAt(address);
        GlowTrailFields& glow = classState<GlowTrailFields>(e);
        const uint8_t slot = e.slot;
        e.flags = kActorLiveDrawn;
        e.loopFlags |= kActorSweepGroup2 | kActorLateLoop;
        e.type = kGlowTrailStageType;
        e.variant = uint8_t(GlowTrailVariant::Trail);
        glow.size = kMineGlowSize;
        glow.mode = 0;
        glow.parent = &a;
        e.pos.x = int16_t(k != 0 ? 8 : -8);
        e.pos.y = -0x1d;
        e.pos.z = -0x1d;
        glow.bone = 0;
        f.glows[size_t(k)] = slot;
    }
}
// exe 0x592d80: the glows freed (the second, then the first)
void mineGlowsOff(MineFields& f) {
    for (int k = 1; k >= 0; --k) {
        auto&& glow = f.glows[size_t(k)];
        if (glow == 0) continue;
        freeActor(effectActorAt(actorBySlot(glow))); // exe 0x5e0fa0 / 0x5e1580
        glow = 0;
    }
}
// exe 0x592dc0: the player within 0x140 in height and faced / near (exe 0x5e5a90(actor, player, 0x500, 0x600) == 0)
bool mineNear(const StaticActor& a) {
    const int dy = std::abs(int(player().pos.y.whole) - a.pos.y.whole);
    if (dy > 0x140) return false;
    return facingAndRange(a, player(), 0x500, 0x600) == 0;
}
// exe 0x592e10: its hit entry (unseen: no lock-on until the player faces it within 0x200 / 0xa00)
void mineHit(StaticActor& a, MineFields& f) {
    const int16_t p[3] = {a.pos.x.whole, a.pos.y.whole, a.pos.z.whole};
    uint32_t unseen = 0;
    f.hitBox.previousX = a.pos.x.whole;
    f.hitBox.previousZ = a.pos.z.whole;
    f.hitBox.result = 0;
    if (f.flags & kMineUnseen) {
        if (facingAndRange(player(), a, 0x200, 0xa00) != 0) unseen = kHitNoLockOn;
        else f.flags &= uint8_t(~kMineUnseen);
    }
    hitListSubmit(p, f.hitBox, hitOwner(a.slot) | unseen | hitGroup(kHitGroupBodies) | hitRadius(0x28),
                  uint32_t(staticStats(a.type, 0).damage) | kHitAttackSolid);
}
// exe 0x592ec0: the walls (layer mask 2): blocked -> pushed out (gave up: back); landed: on the ground, no fall; a
// wall: no forward speed
void mineCollide(const PlayerEnv& env, StaticActor& a) {
    const int16_t p[3] = {a.pos.x.whole, a.pos.y.whole, a.pos.z.whole};
    const int16_t q[3] = {a.prevPos.x.whole, a.prevPos.y.whole, a.prevPos.z.whole};
    if (int32_t(testMoveBlocked(env, p, q, a.collisionBox, kMineLayers)) >= 0) return; // exe 0x40f5a0
    const uint32_t r = actorPushOutMask(env, a, kMineLayers);                                  // exe 0x410980
    a.wallHit = r;
    if (int32_t(r) < 0) {
        a.pos = a.prevPos;
        return;
    }
    if (r & kSolidOnYPlus) {
        a.pos.y.whole = int16_t(actorGround(env, a)); // exe 0x40f190
        a.velocity.y = 0;
    }
    if (a.wallHit & 0xf00) a.velocity.z = 0;
}
// exe 0x592f40: a hit (the player's / a solid's): the flash, alerted, the sound, the damage; 2 dead, 1 hurt, 0 none
int mineHurt(StaticActor& a, MineFields& f) {
    const uint32_t h = f.hitBox.result, d = h & kHitDamageMask;
    if (d == 0 || !(h & (kHitAttackSolid | kHitAttackByPlayer))) return 0;
    a.tint = kTintFlash;
    f.flags |= kMineAlerted;
    soundPlayAtPos(kSoundHurt, a.pos);
    return hitPointsAdd(a, -int(d)) != 0 ? 2 : 1;
}
// exe 0x592600: the model of variant 0 or freed; its box, the hit points, the sway, unseen, the base height, a random
// phase; registered
void mineInit(StaticActor& a, MineFields& f) {
    if (!bindActorModel(a, 0)) { // exe 0x5e0ba0
        freeActor(a);            // exe 0x5e1540
        return;
    }
    a.clearClassBlock(); // exe 0x40dc40
    a.collisionBox.pointAt(mineBox.stateAddress());
    a.interactionBox = nullptr;
    a.interaction = 0;
    a.airborne = 1;
    a.onPlatform = 0;
    a.wallHit = 0;
    a.actorHit = 0;
    const uint16_t hp = hasStaticStats(a.type) ? staticStats(a.type, 0).hitPoints : 0;
    a.maxHitPoints = int16_t(hp);
    a.hitPoints = int16_t(hp);
    a.velocity.y = 0;
    a.spin.y = 4;
    f.swayAngle = 0;
    f.flags |= kMineUnseen;
    f.baseHeight = a.pos.y.whole;
    f.phase = uint8_t(rngStep() & 0x3f);
    a.enterState(uint8_t(MineState::Run));
    registerEnemy(a.slot, 1); // exe 0x5e5c10
}
void toMineSub(StaticActor& a, MineSub s) {
    a.subState = uint8_t(s);
    a.step = 0;
}
// exe 0x592780: sways; alerted: turns to the player (the glows on); turned or near -> homing
void mineIdle(StaticActor& a, MineFields& f) {
    a.velocity.z = 0;
    bool go = mineNear(a); // exe 0x592dc0
    switch (a.step) {
    case 0: {
        const int16_t s = f.swayAngle;
        if (std::abs(int(s)) > 0x100) a.spin.y = int16_t(-a.spin.y);
        const uint16_t c = uint16_t(a.spin.y);
        a.rotation.y = int16_t(uint16_t(a.rotation.y) + c);
        f.swayAngle = int16_t(uint16_t(c + uint16_t(s)));
        if (f.flags & kMineAlerted) {
            a.step = 1;
            const int ang = angleOf(player().pos, a.pos);
            f.timer = int16_t(ang);
            turnYawTo(a.rotation.y, uint32_t(int32_t(int16_t(ang))), 0x30);
            mineGlows(a, f);
        }
        break;
    }
    case 1:
        if (turnYawTo(a.rotation.y, uint32_t(int32_t(f.timer)), 0x30) <= 0x30) go = true;
        break;
    default: break;
    }
    if (!go) return;
    toMineSub(a, MineSub::Home);
    f.timer = 0;
    mineGlows(a, f);
}
// exe 0x592850: homes on the player's bone point 0 (forward 0x80 + rand & 0x7f; of two mines homing side by side the
// farther slows down); within 0x96 -> the fuse; within 0x800 turned to the player matching its height; else searching
// sways (0xb4 ticks -> back)
void mineHome(StaticActor& a, MineFields& f) {
    int16_t t[3];
    homingPoint(t);
    const int d = distance3(a.pos, t); // exe 0x5e3a60
    if (f.skipNeighbour != 0) {
        f.skipNeighbour = 0;
    } else {
        a.velocity.z = int16_t((rngStep() & 0x7f) + 0x80u);
        uint16_t id = 0; // the exe's argument slot, zeroed
        if (neighbourFind(a, id, 1) != 0 && (id & kNeighbourStatic)) { // exe 0x5e5e00
            Actor& o = neighbourActor(id);                              // exe 0x5e5cb0
            if (a.type == o.type && o.subState == uint8_t(MineSub::Home)) {
                const int d2 = distance3(o.pos, t);
                const uint32_t r = rngStep() & 0x3f;
                if (d <= d2) o.velocity.z = int16_t(r);
                else a.velocity.z = int16_t(r);
                classState<MineFields>(asStatic(o)).skipNeighbour = 1;
            }
        }
    }
    if (d <= 0x96) {
        a.subState = uint8_t(MineSub::Fuse);
        f.timer = 0;
        return;
    }
    if (d <= 0x800) {
        f.timer = 0;
        a.step = 0;
        turnYawTo(a.rotation.y, uint32_t(angleOf(player().pos, a.pos)), 0x30);
        const int dy = int(t[1]) - a.pos.y.whole;
        if (std::abs(dy) <= 0x20) a.velocity.y = 0;
        else a.velocity.y = int16_t(dy > 0 ? -0x30 : 0x30);
        return;
    }
    f.timer = int16_t(uint16_t(f.timer) + 1u);
    uint8_t b = a.step;
    if (b % 3 == 0) {
        a.velocity.z = 0;
        if (std::abs(int(f.swayAngle)) > 0x180) a.spin.y = int16_t(-a.spin.y);
        const int st = int(a.spin.y) << 3;
        f.swayAngle = int16_t(uint16_t(f.swayAngle) + uint16_t(st));
        a.rotation.y = int16_t(uint16_t(a.rotation.y) + uint16_t(st));
        if (std::abs(int(f.swayAngle)) < st) a.step = ++b;
    } else {
        a.velocity.z = 0x100;
        if ((uint8_t(f.timer) & 0xf) == 0) a.step = ++b;
    }
    if (f.timer > 0xb4) toMineSub(a, MineSub::Return);
}
// exe 0x592ac0: the fuse -- flashing, slowing to 0x20, a sound every 8 ticks; past 0x1e ticks -> the burst (no drops)
void mineFuse(StaticActor& a, MineFields& f) {
    const uint8_t c = (uint8_t(f.timer) & 7) < 4 ? 0x18 : 0x1f;
    a.velocity.z = int16_t(uint16_t(a.velocity.z) - 0x20u);
    a.tint = uint16_t(0x4200u | c);
    if (a.velocity.z < 0x20) a.velocity.z = 0x20;
    switch (a.step) {
    case 0: {
        if ((uint8_t(f.timer) & 7) == 0) soundPlayAtPos(kSoundFuse, a.pos);
        const int16_t t = f.timer;
        f.timer = int16_t(t + 1);
        if (t > 0x1e) ++a.step;
        return;
    }
    case 1:
        a.state = uint8_t(MineState::Burst);
        a.subState = uint8_t(MineBurst::Fused);
        return;
    default: return;
    }
}
// exe 0x592b50: it gives up -- the glows off, back down to the ground - 0x180, then the reset; alerted -> idle; near ->
// homing
void mineReturn(const PlayerEnv& env, StaticActor& a, MineFields& f) {
    a.velocity.z = 0;
    switch (a.step) {
    case 0:
        f.flags &= uint8_t(~kMineAlerted);
        ++a.step;
        mineGlowsOff(f);                                             // exe 0x592d80
        f.returnHeight = int16_t(actorGround(env, a) - 0x180); // exe 0x40f190
        [[fallthrough]];
    case 1:
        if (f.baseHeight <= f.returnHeight) {
            a.velocity.y = 0;
            ++a.step;
        } else {
            a.velocity.y = 0x30;
        }
        break;
    case 2: toMineSub(a, MineSub::Reset); break;
    default: break;
    }
    if (f.flags & kMineAlerted) toMineSub(a, MineSub::Idle);
    if (mineNear(a)) {
        toMineSub(a, MineSub::Home);
        f.timer = 0;
        mineGlows(a, f);
    }
}
// exe 0x5926d0: the damage, the sub-state, the motion from the base height, the bob
void mineRun(const PlayerEnv& env, StaticActor& a, MineFields& f) {
    if (mineHurt(a, f) == 2) {
        a.state = uint8_t(MineState::Burst);
        a.subState = uint8_t(MineBurst::Killed);
        return;
    }
    switch (MineSub(a.subState)) {
    case MineSub::Reset: // exe 0x592760
        a.velocity.z = 0;
        a.velocity.x = 0;
        a.acceleration.z = 0;
        a.acceleration.x = 0;
        a.subState = uint8_t(MineSub::Idle);
        break;
    case MineSub::Idle: mineIdle(a, f); break;
    case MineSub::Home: mineHome(a, f); break;
    case MineSub::Fuse: mineFuse(a, f); break;
    case MineSub::Return: mineReturn(env, a, f); break;
    default: notPortedAt(kStoryPort, "0x5926d0 sub", a.subState);
    }
    a.pos.y.whole = f.baseHeight;
    integrateVelocity(a, 0, -int(a.velocity.y), -int(a.velocity.z)); // exe 0x5e3690
    const uint16_t y = uint16_t(a.pos.y.whole);
    f.baseHeight = int16_t(y);
    a.pos.y.whole = int16_t(((int(sin64(f.phase)) << 5) >> 12) + int16_t(y));
    f.phase = uint8_t((f.phase + 1u) & 0x3f);
}
// exe 0x592c00: killed (the drops of its variant's row, the defeat, blasts 0xc0) or fused (blasts 0x100): two blasts
// (variants 0 / 1, the second twice the size), freed
void mineBurst(StaticActor& a) {
    uint16_t size, attack;
    switch (MineBurst(a.subState)) {
    case MineBurst::Killed: {
        const StaticActorStats stats = staticStats(a.type, 0);
        size = 0xc0;
        attack = stats.deathAttack;
        spawnDrops(a.pos, staticStats(a.type, a.variant).dropTable); // exe 0x5f1270
        countDefeat();                                                // exe 0x5e53e0
        break;
    }
    case MineBurst::Fused:
        size = 0x100;
        attack = staticStats(a.type, 0).power;
        break;
    default: freeActor(a); return; // exe 0x5e1540
    }
    for (uint32_t v = 0; v < 2; ++v) {
        const uint32_t address = allocActor60(0); // exe 0x5e1300
        if (!address) continue;
        StaticActor& e = staticActorAt(address);
        e.flags = kActorLive;
        e.loopFlags |= kActorSweepGroup2;
        e.type = typeByte(GlobalStaticType::Blast);
        e.variant = uint8_t(v);
        e.pos = a.pos;
        BlastParams params(e);
        params.size = uint16_t(size << v);
        params.attack = uint16_t(attack << v);
    }
    freeActor(a); // exe 0x5e1540
}
// exe 0x592580
void mineUpdate(const PlayerEnv& env, StaticActor& a) {
    auto&& f = classState<MineFields>(a);
    a.tint = kTintNeutral;
    savePrevious(a);
    switch (MineState(a.state)) {
    case MineState::Init: mineInit(a, f); break;
    case MineState::Run: mineRun(env, a, f); break;
    case MineState::Burst: mineBurst(a); break;
    default: notPortedAt(kStoryPort, "0x592580 state", a.state);
    }
    if (!(a.flags & kActorLive)) return;
    mineCollide(env, a); // exe 0x592ec0
    mineHit(a, f);       // exe 0x592e10
    // exe 0x5f20f0(actor, 0x30, 0x100, 0): the shadow list entry (render output)
}

// ==== the digger (exe 0x4dd9d0: pool 0x20 type 0x53, states [0x89584c], sub-states [0x895860]) ======================
// It surfaces next to the player (offsets around the camera's yaw), walks / lunges with its two claws (bones 2 / 4),
// sinks back underground.
enum class DiggerState : uint8_t { Init = 0, Run = 1, Gone = 2 }; // exe 0x4dda70 / 0x4ddbe0 / 0x468fa0
enum class DiggerSub : uint8_t {
    Hidden = 0,  // exe 0x4ddfe0
    Walk = 1,    // exe 0x4de1d0
    Lunge = 2,   // exe 0x4de430
    Turn = 3,    // exe 0x4de640
    Fall = 4,    // exe 0x4de690
    BackOff = 5, // exe 0x4de880
    Swipe = 6,   // exe 0x4de960
    Spin = 7,    // exe 0x4debc0
    Hurt = 8,    // exe 0x4dece0
    Dying = 9,   // exe 0x4dedc0
};
// +0x15a
constexpr uint8_t kDiggerClaws = 0x01, kDiggerWallCooldown = 0x02, kDiggerAirborne = 0x04, kDiggerSink = 0x08;
constexpr uint8_t kDiggerRise = 0x10, kDiggerDying = 0x20, kDiggerNoHurt = 0x40, kDiggerSurfaceNear = 0x80;
// +0x15e
constexpr uint8_t kDiggerContactWall = 0x01, kDiggerContactLedge = 0x02, kDiggerContactLanded = 0x04;
const DataTable<CollisionBoxRecord> diggerBox{TableId::ExamEnemiesBDiggerBox};
const DataTable<ScalarTable<uint8_t, 4>> diggerModels{TableId::ExamEnemiesBDiggerModels}; // by the spawn byte
const DataTable<ScalarTable<uint16_t, 4>> diggerStartOffsets{TableId::ExamEnemiesBDiggerStartOffsets}; // by the variant >> 4
const DataTable<ScalarTable<uint16_t, 4>> diggerWalkOffsets{TableId::ExamEnemiesBDiggerWalkOffsets};
const DataTable<ScalarTable<uint16_t, 4>> diggerSwipeOffsets{TableId::ExamEnemiesBDiggerSwipeOffsets};
const DataTable<ScalarTable<uint16_t, 8>> diggerSurfaceAngles{TableId::ExamEnemiesBDiggerSurfaceAngles}; // around the camera's yaw
constexpr uint8_t kBossStage = 0x4f, kBossRoom = 0xb;        // its stage / room as the exam's boss
constexpr uint16_t kSoundKnocked = 0x8e, kSoundStepA = 0x23e, kSoundStepB = 0x23f, kSoundBackOff = 0x240;
constexpr uint16_t kSoundHop = 0x241, kSoundSwipe = 0x242;
constexpr uint8_t kSemiOn = ((1 & 1) << 4 | (1 & 3)) << 1;  // exe 0x5e1a00(actor, 1, 1)
constexpr uint8_t kSemiOff = ((1 & 1) << 4 | (0 & 3)) << 1; // exe 0x5e1a00(actor, 1, 0)

AnimatedActorStats diggerStats(const AnimatedActor& a) { return animatedStats(a.type, a.spawnByte); }
int distanceToPlayer(const AnimatedActor& a) { return distance3(player().pos, a.pos); }  // exe 0x5e3a60
int angleToPlayer(const AnimatedActor& a) { return angleOf(player().pos, a.pos); }      // exe 0x5e3bc0(player, actor)
int facingDiff(const AnimatedActor& a) { // exe 0x5e3d80(yaw, the angle to the player)
    return angleDiff(uint32_t(int32_t(a.rotation.y)), uint32_t(angleToPlayer(a)));
}
void setFlags(DiggerFields& f, uint8_t keep, uint8_t set) { f.flags = uint8_t((f.flags & keep) | set); }
void toDiggerSub(AnimatedActor& a, DiggerSub s) {
    a.subState = uint8_t(s);
    a.step = 0;
}

// exe 0x4dda70: the model of its spawn byte, the hit points (the row's * 2; the boss gauge as the exam's boss), its
// box, the scale, its hit block in its third bone block, the colour, risen next to the player (sub-state 1)
void diggerInit(AnimatedActor& a, DiggerFields& f) {
    if (!bindActorModel(a, diggerModels[a.spawnByte])) { // exe 0x5e0bc0(actor, 0x89585c + spawn byte)
        freeActor(a);                                    // exe 0x5e1490
        return;
    }
    a.anim = 0;
    a.prevAnim = 0xff;
    const uint16_t hp = uint16_t(uint32_t(diggerStats(a).hitPoints) * 2u);
    a.maxHitPoints = int16_t(hp);
    a.hitPoints = int16_t(hp);
    if (gameState().stage == kBossStage && gameState().room == kBossRoom) hudAddBossGauge(a, 1, 1, 0x2222aa);
    f.knockThreshold = diggerStats(a).bodyAttack; // the row's +8
    a.collisionBox.pointAt(diggerBox.stateAddress());
    a.interactionBox = nullptr;
    a.interaction = 0;
    f.setScaleWords(0x1800180, 0x1800180);
    a.velocity.setXY(0, 0);
    a.velocity.setZSpare(0, 0);
    a.acceleration.y = 0x30;
    a.rotation.z = 0;
    a.rotation.x = 0;
    a.airborne = 0;
    a.onPlatform = 0;
    a.platform = nullptr;
    a.actorHit = 0;
    BoneBlock* block = boneBlockOf(a, 2); // exe 0x5e1780(actor, 2)
    if (!block) notPorted(kStoryPort, "0x4dda70: exe 0x5e1780(actor, 2) returned null");
    f.hits.pointAt(block->at + 0x60);
    DiggerHitBlock& hits = *f.hits;
    hits.body.result = 0;
    hits.claws.result = 0;
    f.flags = 0;
    f.timer = 0;
    f.contact = 0;
    f.fadeTicks = 0;
    f.offscreen = 0;
    f.unused154 = 0;
    f.offset = int16_t(diggerStartOffsets[uint32_t(a.variant) >> 4]);
    f.blue = 0x10;
    f.green = 0x10;
    f.red = 0x10;
    a.step = 0;
    a.state = uint8_t(DiggerState::Run);
    a.subState = uint8_t(DiggerSub::Walk);
    a.tint = kTintNeutral;
}
// exe 0x4def60: the walls (pushed out; gave up: back), on the ground a drop over 0x20 (a ledge) or snapped, airborne a
// landing; the push-out's result
uint32_t diggerCollide(const PlayerEnv& env, AnimatedActor& a, DiggerFields& f) {
    uint32_t r = 0;
    if (int32_t(actorTestMove(env, a)) < 0) { // exe 0x40f570
        r = actorPushOut(env, a);             // exe 0x410960
        if (int32_t(r) < 0) a.pos = a.prevPos;
        if (r & 0xf00) f.contact |= kDiggerContactWall;
    }
    const int g = int(actorGround(env, a)); // exe 0x40f190
    if (!(f.flags & kDiggerAirborne)) {
        if (g - a.pos.y.whole > 0x20) f.contact |= kDiggerContactLedge;
        else a.pos.y.whole = int16_t(g);
        return r;
    }
    if (r & kSolidAboveOrBelow) {
        a.airborne = 0;
        a.pos.y.whole = int16_t(g);
        f.contact |= kDiggerContactLanded;
    }
    return r;
}
// exe 0x4df010: the sink (its step counter): the colour faded down, hidden, back underground
void diggerSink(AnimatedActor& a, DiggerFields& f) {
    switch (a.stepCounter) {
    case 0:
        a.semiTransparency = kSemiOn; // exe 0x5e1a00(actor, 1, 1)
        ++a.stepCounter;
        [[fallthrough]];
    case 1:
        if (f.red == 1) {
            ++a.stepCounter;
        } else {
            --f.red;
            --f.green;
        }
        return;
    case 2:
        if (f.blue == 1) {
            a.flags ^= kActorDrawn;
            a.stepCounter = 3;
        } else {
            --f.blue;
        }
        return;
    case 3:
        a.subState = uint8_t(DiggerSub::Hidden);
        a.step = 0;
        a.stepCounter = 0;
        a.anim = 0;
        setFlags(f, uint8_t(~(kDiggerSink | kDiggerAirborne)), 0);
        return;
    default: return;
    }
}
// exe 0x4df0c0: the rise (its step counter): shown, the colour faded up, 0x14 ticks, opaque
void diggerRise(AnimatedActor& a, DiggerFields& f) {
    switch (a.stepCounter) {
    case 0:
        a.semiTransparency = kSemiOn;
        a.flags |= kActorDrawn;
        ++a.stepCounter;
        return;
    case 1:
        if (f.blue >= 0x10) {
            f.blue = 0x10;
            ++a.stepCounter;
            f.fadeTicks = 0x14;
        } else {
            ++f.blue;
        }
        return;
    case 2:
        if (f.fadeTicks != 0) {
            --f.fadeTicks;
            if (f.red == 0x10) return;
            ++f.red;
            ++f.green;
            return;
        }
        a.semiTransparency = kSemiOff; // exe 0x5e1a00(actor, 1, 0)
        ++a.stepCounter;
        return;
    case 3:
        a.stepCounter = 0;
        setFlags(f, uint8_t(~kDiggerRise), 0);
        return;
    default: return;
    }
}
// exe 0x4df1a0(actor, yaw): the hitter's direction against the yaw -- in front: turned round; behind: | 0x10000
uint32_t knockDirection(const DiggerFields& f, int yaw) {
    const int v = (*f.hits).body.contact.x;
    if (((v - yaw + 0x400) & kAngleMask) >= 0x800) return uint32_t(v + int(0xfffff800)) & kAngleMask;
    return (uint32_t(v) & kAngleMask) | 0x10000;
}
// exe 0x4df1e0: the player behind it (within 0x400 of its yaw + 0x600) and facing it (its yaw within 0x200 the
// other way)
bool playerBehind(const AnimatedActor& a) {
    const int ang = angleOf(a.pos, player().pos);
    const uint16_t c = uint16_t((uint32_t(ang) - uint16_t(a.rotation.y) - 0x600) & kAngleMask);
    if (int16_t(c) > 0x400) return false;
    const uint16_t d = uint16_t((uint32_t(ang) - uint16_t(player().rotation.y) + 0x200) & kAngleMask);
    return int16_t(d) <= 0x400;
}
// a turn of 0x200 or more toward the player -> the turn sub-state (animation 0xd / 0xe)
void turnToPlayer(AnimatedActor& a, int diff) {
    if (diff >= 0x200) {
        a.anim = 0xd;
        a.subState = uint8_t(DiggerSub::Turn);
    } else if (diff <= -0x200) {
        a.anim = 0xe;
        a.subState = uint8_t(DiggerSub::Turn);
    }
}
// exe 0x4ddfe0: underground 0x1e ticks; a free floor cell around the player (else a retry in 0x1e); then it rises
void diggerHidden(const PlayerEnv& env, AnimatedActor& a, DiggerFields& f) {
    switch (a.step) {
    case 0:
        ++a.step;
        f.timer = 0x1e;
        return;
    case 1:
        if (f.timer != 0) --f.timer;
        else ++a.step;
        return;
    case 2: {
        const int lim = (f.flags & kDiggerSurfaceNear) ? 1 : 7;
        int8_t bl = int8_t(rngStep() & uint32_t(lim));
        int8_t cnt = 0;
        const Player& p = player();
        for (;;) {
            const int idx = bl;
            const uint32_t ang = (uint32_t(diggerSurfaceAngles[size_t(idx)]) + uint16_t(cameraState().viewAngles.y)) &
                                 kAngleMask;
            a.rotation.y = int16_t((ang - 0x800) & kAngleMask);
            int16_t dx, dz;
            angleVector(int(ang), bl < 3 ? 0x300 : 0x100, dx, dz); // exe 0x5e40c0
            const int16_t x = int16_t(uint16_t(p.pos.x.whole) + uint16_t(dx));
            const int16_t z = int16_t(uint16_t(p.pos.z.whole) + uint16_t(dz));
            a.pos.x.whole = x;
            a.pos.z.whole = z;
            const uint32_t cell = gridCellAtPoint(x, z); // exe 0x40f120
            if (cell == 0) break;
            const int16_t w = int16_t(GridCell(cell).word);
            if (w < 0 && (w & kCellFloor) && !(w & kCellSpecial)) break;
            if (idx > lim) {
                bl = 0;
            } else {
                ++bl;
                ++cnt;
                if (cnt > lim) {
                    f.timer = 0x1e;
                    a.step = 3;
                }
            }
            if (!(cnt < lim + 1)) break;
        }
        if (a.step == 3) return;
        a.pos.y.whole = int16_t(actorGround(env, a)); // exe 0x40f190
        setFlags(f, uint8_t(~(kDiggerSurfaceNear | kDiggerRise)), kDiggerRise);
        f.offset = int16_t(diggerWalkOffsets[uint32_t(a.variant) >> 4]);
        a.subState = uint8_t(DiggerSub::Walk);
        a.step = 0;
        f.offscreen = 0;
        f.unused154 = 0;
        return;
    }
    case 3:
        if (f.timer == 0) a.step = 0;
        else --f.timer;
        return;
    default: return;
    }
}
// off a ledge -> the fall (animation 6)
void offLedge(AnimatedActor& a) {
    toDiggerSub(a, DiggerSub::Fall);
    a.anim = 6;
    a.airborne = 1;
}
// exe 0x4de1d0: walks (forward 0xa0) toward the player (with its offset when far); far and off-screen -> the lunge,
// near -> the swipe; off-screen too long -> sinks
void diggerWalk(const PlayerEnv& env, AnimatedActor& a, DiggerFields& f) {
    if (a.step == 0) {
        a.anim = 1;
        a.step = 1;
        a.velocity.z = -0xa0;
    }
    integrateVelocity(a, 0, 0, a.velocity.z); // exe 0x5e3690
    const uint32_t r = diggerCollide(env, a, f);
    const uint8_t c = f.contact;
    if (c & kDiggerContactLedge) {
        offLedge(a);
        return;
    }
    if (c & kDiggerContactWall) {
        reflectRotationOffWalls(a.rotation, r); // exe 0x410740
        setFlags(f, 0xff, kDiggerWallCooldown);
        f.timer = 0x3c;
        return;
    }
    if (!(f.flags & kDiggerWallCooldown)) {
        if (uint32_t(distanceToPlayer(a)) < 0x200) turnYawTo(a.rotation.y, uint32_t(angleToPlayer(a)), 0x30);
        else turnYawTo(a.rotation.y, uint32_t(int(f.offset) + angleToPlayer(a)), 0x30);
    } else {
        --f.timer;
        if (f.timer == 0) f.flags ^= kDiggerWallCooldown;
    }
    const uint8_t off = uint8_t(pointVisibleOrNear(a.pos, 0)); // exe 0x5e59a0
    const int d = distanceToPlayer(a);
    if (d > 0x400 && off == 0) {
        const int diff = facingDiff(a);
        f.nextSub = uint8_t(DiggerSub::Lunge);
        toDiggerSub(a, DiggerSub::Lunge);
        f.timer = 0;
        if (!(a.flags & kActorDrawn)) setFlags(f, 0xff, kDiggerRise);
        setFlags(f, uint8_t(~kDiggerWallCooldown), 0);
        turnToPlayer(a, diff);
        return;
    }
    if (d < 0x200) {
        const int diff = facingDiff(a);
        if (diff < 0x80 && diff > -0x80) {
            f.nextSub = uint8_t(DiggerSub::Swipe);
            toDiggerSub(a, DiggerSub::Swipe);
        }
        if (!(a.flags & kActorDrawn)) setFlags(f, 0xff, kDiggerRise);
        setFlags(f, uint8_t(~kDiggerWallCooldown), 0);
        if (diff >= 0x200 || diff <= -0x200) f.nextSub = uint8_t(DiggerSub::Swipe);
        turnToPlayer(a, diff);
        return;
    }
    if (off != 0) {
        if (f.offscreen > 0x3c) {
            if (!(a.variant & 1)) setFlags(f, 0xff, kDiggerSurfaceNear | kDiggerSink);
            return;
        }
        f.offscreen = int16_t(uint16_t(f.offscreen) + 1u);
        return;
    }
    f.offscreen = 0;
}
// exe 0x4de430: the lunge (animation 5 -> 2, forward 0x3c0 with the claws) at the player; behind it -> the back-off
void diggerLunge(const PlayerEnv& env, AnimatedActor& a, DiggerFields& f) {
    switch (a.step) {
    case 0:
        a.anim = 5;
        a.step = 1;
        return;
    case 1:
        if (a.animEnded()) {
            a.anim = 2;
            a.velocity.z = -0x3c0;
            a.step = 2;
            setFlags(f, 0xff, kDiggerClaws);
        }
        return;
    case 2: break;
    default: return;
    }
    integrateVelocity(a, 0, 0, a.velocity.z);
    const uint32_t r = diggerCollide(env, a, f);
    const uint8_t c = f.contact;
    auto ledge = [&] {
        offLedge(a);
        setFlags(f, uint8_t(~kDiggerClaws), 0);
    };
    if (c & kDiggerContactLedge) {
        ledge();
        return;
    }
    if (c & kDiggerContactWall) {
        reflectRotationOffWalls(a.rotation, r);
        setFlags(f, 0xff, kDiggerWallCooldown);
        f.cooldown = 0xa;
        return;
    }
    const int d = distanceToPlayer(a);
    if (d > 0x200) {
        if (f.flags & kDiggerWallCooldown) {
            --f.cooldown;
            if (f.cooldown == 0) f.flags ^= kDiggerWallCooldown;
            return;
        }
        if (uint32_t(distanceToPlayer(a)) < 0x300) {
            turnYawTo(a.rotation.y, uint32_t(angleToPlayer(a)), 0x40);
        } else {
            const int ang = angleToPlayer(a);
            turnYawTo(a.rotation.y, uint32_t(int(f.offset) / 4 + ang), 0x40);
        }
        if (pointVisibleOrNear(a.pos, 0) != 0) {
            ++f.timer;
            if (int16_t(f.timer) > 0x12c && !(a.variant & 1)) setFlags(f, 0xff, kDiggerSink);
        } else {
            f.timer = 0;
        }
        return;
    }
    if (playerBehind(a)) { // exe 0x4df1e0
        toDiggerSub(a, DiggerSub::BackOff);
        setFlags(f, uint8_t(~kDiggerWallCooldown), 0);
        return;
    }
    turnYawTo(a.rotation.y, uint32_t(angleToPlayer(a)), 0x7ff);
    if (d < 0x100) ledge();
}
// exe 0x4de640: turns (0x55) until the animation ends, then the next sub-state
void diggerTurn(AnimatedActor& a, DiggerFields& f) {
    if (!a.animEnded()) {
        turnYawTo(a.rotation.y, uint32_t(angleToPlayer(a)), 0x55);
        return;
    }
    setFlags(f, uint8_t(~kDiggerWallCooldown), 0);
    a.subState = f.nextSub;
    a.step = 0;
}
// the next sub-state by the player's distance (exe 0x4de6c0 after the fall / exe 0x4dea0d after the swipe): far -> the
// lunge, near -> the swipe (after the swipe: stay, facing), else the walk
void diggerRechoose(AnimatedActor& a, DiggerFields& f, bool afterSwipe) {
    const int d = distanceToPlayer(a);
    if (d > 0x400) {
        const int diff = facingDiff(a);
        a.step = 0;
        f.nextSub = uint8_t(DiggerSub::Lunge);
        a.subState = uint8_t(DiggerSub::Lunge);
        if (afterSwipe) setFlags(f, uint8_t(~kDiggerClaws), 0);
        f.timer = 0;
        if (!afterSwipe && !(a.flags & kActorDrawn)) setFlags(f, 0xff, kDiggerRise);
        turnToPlayer(a, diff);
        return;
    }
    if (d < 0x200) {
        const int diff = facingDiff(a);
        if (afterSwipe) {
            a.step = 0;
            setFlags(f, uint8_t(~kDiggerClaws), 0);
            if (diff >= 0x200 || diff <= -0x200) {
                turnToPlayer(a, diff);
                return;
            }
            a.prevAnim = 0xff;
            turnYawTo(a.rotation.y, uint32_t(angleToPlayer(a)), 0x7ff);
            return;
        }
        f.nextSub = uint8_t(DiggerSub::Swipe);
        toDiggerSub(a, DiggerSub::Swipe);
        if (!(a.flags & kActorDrawn)) setFlags(f, 0xff, kDiggerRise);
        turnToPlayer(a, diff);
        return;
    }
    a.step = 0;
    f.nextSub = uint8_t(DiggerSub::Walk);
    a.subState = uint8_t(DiggerSub::Walk);
    f.offscreen = 0;
    if (afterSwipe) f.offset = int16_t(diggerSwipeOffsets[uint32_t(a.variant) >> 4]);
    const int diff = facingDiff(a);
    if (afterSwipe) setFlags(f, uint8_t(~kDiggerClaws), 0);
    else if (!(a.flags & kActorDrawn)) setFlags(f, 0xff, kDiggerRise);
    turnToPlayer(a, diff);
}
// exe 0x4de690: the hop off the ledge (on its animation's event: -0x300 up / -0x280 forward, the sound) until it lands
void diggerFall(const PlayerEnv& env, AnimatedActor& a, DiggerFields& f) {
    switch (a.step) {
    case 0:
        if (a.animKey.events & 1) {
            a.velocity.y = -0x300;
            a.velocity.z = -0x280;
            setFlags(f, 0xff, kDiggerAirborne);
            ++a.step;
            soundPlayAtPos(kSoundHop, a.pos);
        }
        return;
    case 1:
        integrateVelocity(a, 0, a.velocity.y, a.velocity.z);
        a.velocity.y = int16_t(uint16_t(a.velocity.y) + uint16_t(a.acceleration.y));
        diggerCollide(env, a, f);
        if (f.contact & kDiggerContactWall) a.velocity.z = 0;
        if (f.contact & kDiggerContactLanded) {
            a.anim = 7;
            setFlags(f, uint8_t(~kDiggerAirborne), 0);
            ++a.step;
        }
        return;
    case 2:
        if (!a.animEnded()) return;
        diggerRechoose(a, f, false);
        return;
    default: return;
    }
}
// exe 0x4de880: backs off (animation 4, the sound, 0x14 ticks accelerating), then the spin (or the walk with variant
// bit 0)
void diggerBackOff(const PlayerEnv& env, AnimatedActor& a, DiggerFields& f) {
    switch (a.step) {
    case 0:
        a.anim = 4;
        a.step = 1;
        f.timer = 0x14;
        a.acceleration.z = 0x30;
        soundPlayAtPos(kSoundBackOff, a.pos);
        break;
    case 1:
        if (f.timer != 0) {
            --f.timer;
            a.velocity.z = int16_t(uint16_t(a.velocity.z) + uint16_t(a.acceleration.z));
        } else {
            a.step = 2;
        }
        break;
    case 2:
        setFlags(f, uint8_t(~kDiggerClaws), 0);
        if (!(a.variant & 1)) {
            toDiggerSub(a, DiggerSub::Spin);
            return;
        }
        toDiggerSub(a, DiggerSub::Walk);
        f.offscreen = 0;
        break;
    default: break;
    }
    integrateVelocity(a, 0, 0, a.velocity.z);
    diggerCollide(env, a, f);
    if (f.contact & kDiggerContactLedge) offLedge(a);
}
// exe 0x4de960: the swipe (on screen and not rising: animation 3, the claws, the sound), then the distance's choice
void diggerSwipe(AnimatedActor& a, DiggerFields& f) {
    switch (a.step) {
    case 0:
        if (!(f.flags & kDiggerRise) && pointVisibleOrNear(a.pos, 0) == 0) {
            a.anim = 3;
            ++a.step;
            setFlags(f, 0xff, kDiggerClaws);
            soundPlayAtPos(kSoundSwipe, a.pos);
            return;
        }
        a.anim = 5;
        a.step = 3;
        return;
    case 1:
        if (a.animEnded()) {
            ++a.step;
            f.timer = 1;
        }
        return;
    case 2:
        if (f.timer != 0) {
            --f.timer;
            return;
        }
        diggerRechoose(a, f, true);
        return;
    case 3:
        if (a.animEnded()) {
            a.anim = 3;
            a.step = 1;
            setFlags(f, 0xff, kDiggerClaws);
        }
        return;
    default: return;
    }
}
// exe 0x4debc0: spins (animation 0xd / 0xe at random, the yaw + 0x2a), dashes (0x3c0) 0xa ticks, then sinks (unless
// variant bit 0)
void diggerSpin(const PlayerEnv& env, AnimatedActor& a, DiggerFields& f) {
    switch (a.step) {
    case 0:
        a.anim = (rngStep() & 1) ? 0xd : 0xe;
        ++a.step;
        return;
    case 1:
        a.rotation.y = int16_t(uint16_t(a.rotation.y) + 0x2au);
        if (a.animEnded()) ++a.step;
        return;
    case 2:
        ++a.step;
        a.anim = 2;
        a.velocity.z = -0x3c0;
        f.timer = 0xa;
        return;
    case 3: {
        if (f.timer != 0) --f.timer;
        else if (!(a.variant & 1)) setFlags(f, 0xff, kDiggerSink);
        integrateVelocity(a, 0, 0, a.velocity.z);
        const uint32_t r = diggerCollide(env, a, f);
        const uint8_t c = f.contact;
        if (c & kDiggerContactLedge) {
            offLedge(a);
            return;
        }
        if (c & kDiggerContactWall) reflectRotationOffWalls(a.rotation, r);
        return;
    }
    default: return;
    }
}
// the knock of the hurt / dying sub-states (steps 0 / 1): along the yaw (step 0: turned round), the gravity; landed ->
// animation 9 / 0xb
bool knockFlight(const PlayerEnv& env, AnimatedActor& a, DiggerFields& f) {
    if (a.step >= 2) return false;
    uint16_t ang = uint16_t(a.rotation.y);
    if (a.step == 0) ang = uint16_t(ang - 0x800);
    moveAlongYaw(a.pos, int(int16_t(ang & kAngleMask)), 0, a.velocity.y, a.velocity.z); // exe 0x5e36f0
    a.velocity.y = int16_t(uint16_t(a.velocity.y) + uint16_t(a.acceleration.y));
    diggerCollide(env, a, f);
    if (f.contact & kDiggerContactWall) a.velocity.z = 0;
    if (f.contact & kDiggerContactLanded) {
        a.anim = a.step == 0 ? 9 : 0xb;
        a.step = 2;
    }
    return true;
}
// exe 0x4dece0: knocked back, back on its feet -> the spin (or the walk with variant bit 0)
void diggerHurt(const PlayerEnv& env, AnimatedActor& a, DiggerFields& f) {
    if (knockFlight(env, a, f)) return;
    if (a.step < 3) {
        if (a.animEnded()) {
            ++a.step;
            a.anim = 0xc;
            setFlags(f, uint8_t(~kDiggerAirborne), 0);
        }
        return;
    }
    if (!a.animEnded()) return;
    if (!(a.variant & 1)) {
        toDiggerSub(a, DiggerSub::Spin);
        return;
    }
    f.nextSub = uint8_t(DiggerSub::Walk);
    toDiggerSub(a, DiggerSub::Walk);
    f.offscreen = 0;
}
// exe 0x4dedc0: dying -- an explosion at bone 0, blinking 0x1e ticks, then gone: the defeat and the drops (none as the
// exam's boss)
void diggerDying(const PlayerEnv& env, AnimatedActor& a, DiggerFields& f) {
    const AnimatedActorStats stats = diggerStats(a);
    if (knockFlight(env, a, f)) return;
    if (a.step < 3) {
        if (!a.animEnded() || a.stepCounter != 0) return;
        f.timer = 0x1e;
        ++a.step;
        ++a.stepCounter;
        int16_t p[3];
        bonePointOf(a, 0, p); // exe 0x5e1810(actor, 0)
        explosionAt(p[0], p[1], p[2], 0x60, 0x3c);
        return;
    }
    if (f.timer != 0) {
        if (uint8_t(f.timer) & 1) a.tint = 0x421f;
        --f.timer;
        if (int16_t(f.timer) < 0x1e) setFlags(f, 0xff, kDiggerDying);
        return;
    }
    a.state = uint8_t(DiggerState::Gone);
    countDefeat(); // exe 0x5e53e0
    if (gameState().stage == kBossStage && gameState().room == kBossRoom) return;
    spawnDrops(a.pos, stats.dropTable); // exe 0x5f1270
}
// knocked: -0x120 up / back, airborne, its yaw from the hitter's direction (exe 0x4df1a0)
void diggerKnock(AnimatedActor& a, DiggerFields& f) {
    a.velocity.z = -0x120;
    a.velocity.y = -0x120;
    a.airborne = 1;
    const uint32_t r = knockDirection(f, a.rotation.y);
    a.rotation.y = int16_t(r & kAngleMask);
    if (!(r & 0xffff0000u)) {
        a.anim = 8;
        a.step = 0;
    } else {
        a.anim = 0xa;
        a.step = 1;
    }
}
// exe 0x4ddbe0: its colour, the damage (dead -> dying; a hard hit -> knocked back), the sink / rise, the sub-state, the
// steps' sounds, the body's entry and the claws'
void diggerRun(const PlayerEnv& env, AnimatedActor& a, DiggerFields& f) {
    DiggerHitBlock& hits = *f.hits;
    a.tint = uint16_t(((uint32_t(f.blue) << 5 | f.green) << 5 | f.red) & 0xffff);
    f.contact = 0;
    if (!(f.flags & kDiggerNoHurt) && (hits.body.result & kHitAttackByPlayer)) {
        a.tint = kTintFlash;
        if (hitPointsAdd(a, -int(hits.body.result & kHitDamageMask)) != 0) { // exe 0x5e52f0
            a.subState = uint8_t(DiggerSub::Dying);
            a.stepCounter = 0;
            setFlags(f, uint8_t(~(kDiggerSink | kDiggerRise)), kDiggerNoHurt | kDiggerAirborne);
            diggerKnock(a, f);
        } else if (a.subState != uint8_t(DiggerSub::Hurt) &&
                   uint32_t(f.knockThreshold) < (hits.body.result & kHitDamageMask)) {
            a.subState = uint8_t(DiggerSub::Hurt);
            setFlags(f, 0xff, kDiggerAirborne);
            diggerKnock(a, f);
            soundPlayAtPos(kSoundKnocked, a.pos);
        } else {
            soundPlayAtPos(kSoundHurt, a.pos);
        }
    }
    if (!(a.variant & 1)) {
        if (f.flags & kDiggerSink) diggerSink(a, f);
        else if (f.flags & kDiggerRise) diggerRise(a, f);
    }
    switch (DiggerSub(a.subState)) {
    case DiggerSub::Hidden: diggerHidden(env, a, f); break;
    case DiggerSub::Walk: diggerWalk(env, a, f); break;
    case DiggerSub::Lunge: diggerLunge(env, a, f); break;
    case DiggerSub::Turn: diggerTurn(a, f); break;
    case DiggerSub::Fall: diggerFall(env, a, f); break;
    case DiggerSub::BackOff: diggerBackOff(env, a, f); break;
    case DiggerSub::Swipe: diggerSwipe(a, f); break;
    case DiggerSub::Spin: diggerSpin(env, a, f); break;
    case DiggerSub::Hurt: diggerHurt(env, a, f); break;
    case DiggerSub::Dying: diggerDying(env, a, f); break;
    default: notPortedAt(kStoryPort, "0x4dd9d0 sub", a.subState);
    }
    if (a.animKey.events & 2) soundPlayAtPos(kSoundStepA, a.pos);
    else if (a.animKey.events & 4) soundPlayAtPos(kSoundStepB, a.pos);
    const int16_t p[3] = {a.pos.x.whole, int16_t(uint16_t(a.pos.y.whole) - 0xc8u), a.pos.z.whole};
    hits.body.previousX = a.prevPos.x.whole;
    hits.body.previousZ = a.prevPos.z.whole;
    hits.body.result = 0;
    const AnimatedActorStats stats = diggerStats(a);
    const uint32_t owner = hitOwner(a.slot) | hitHeightLevel(stats.hitHeight);
    const uint8_t fl = f.flags;
    if (!(fl & (kDiggerSink | kDiggerRise)) && (a.flags & kActorDrawn))
        hitListSubmit(p, hits.body, owner | hitGroup(kHitGroupBodies) | kHitContactMotion | hitRadius(0xb4),
                      uint32_t(stats.attack) |
                          ((fl & kDiggerDying) ? kHitAttackBump : (kHitAttackThrown | kHitAttackSolid))); // 0x5e4120
    if (!(fl & kDiggerClaws)) return;
    const int16_t off[3] = {0, 0x9a, -4};
    int16_t q[3];
    for (uint32_t bone : {2u, 4u}) {
        bonePointOffset(a, bone, off, q); // exe 0x5e1970
        const uint32_t kind = (f.flags & kDiggerDying) ? kHitAttackBump
                                                       : (kHitAttackHarmless | kHitAttackThrown | kHitAttackSolid);
        hitListSubmit(q, hits.claws, owner | hitGroup(kHitGroupBodies) | kHitNoLockOn | hitRadius(0x50),
                      uint32_t(stats.strongAttack) | kind);
    }
}
// exe 0x4dd9d0
void diggerUpdate(const PlayerEnv& env, AnimatedActor& a) {
    auto&& f = classState<DiggerFields>(a);
    a.prevAnim = a.anim;
    savePrevious(a);
    switch (DiggerState(a.state)) {
    case DiggerState::Init: diggerInit(a, f); break;
    case DiggerState::Run: diggerRun(env, a, f); break;
    case DiggerState::Gone: freeActor(a); break; // exe 0x468fa0
    default: notPortedAt(kStoryPort, "0x4dd9d0 state", a.state);
    }
    if (a.anim == a.prevAnim) animStepPose(a); // exe 0x5e1e80
    else animSetPose(a, a.anim, 0);            // exe 0x5e1c30
    // exe 0x5f20f0(actor, 0x60, 0xc0, 0) while shown: the shadow (render output)
}

// ==== the ambush flyer (exe 0x4ed1a0: pool 0x20 type 0x67, states [0x8966ac], sub-states [0x8966b8]) ================
// Woken (its wake byte 1) it rises from its perch, hovers facing the player (two shots when facing it at its key 4;
// the player slowed -> the dive); the group alerted (wake byte 2) -> back to its perch.
enum class FlyerState : uint8_t { Init = 0, Run = 1, Gone = 2 }; // exe 0x4ed260 / 0x4ed380 / 0x468fa0
enum class FlyerSub : uint8_t {
    Perch = 0,   // exe 0x4ed5f0
    Hover = 1,   // exe 0x4ed880
    Dive = 2,    // exe 0x4ed970
    Return = 3,  // exe 0x4edc40
    Stagger = 4, // exe 0x4eecc0
    Dying = 5,   // exe 0x4edf10
};
constexpr uint8_t kFlyerLifted = 0x01, kFlyerCollideAsShot = 0x02, kFlyerDecodeOnly = 0x04, kFlyerStaggered = 0x08;
constexpr uint8_t kFlyerDying = 0x10, kFlyerInGroup = 0x20, kFlyerWallTurn = 0x40;
constexpr uint8_t kWakeUp = 1, kWakeAlerted = 2;
const DataTable<CollisionBoxRecord> flyerBox{TableId::ExamEnemiesBFlyerBox};
const DataTable<InteractionBoxRecord> flyerLiftBox{TableId::ExamEnemiesBFlyerLiftBox};
constexpr uint8_t kFlyerShotType = 0x4c;
constexpr uint8_t kShotSlot = 0x10; // the slot id it collides as (a shot's layers)
constexpr uint16_t kSoundFlap = 0x200, kSoundDiveFlap = 0x206;

AnimatedActorStats flyerStats(const AnimatedActor& a) {
    return animatedStatsAt(statTableRow(actorStatTables().animated, a.type) +
                              (uint32_t(a.spawnByte) + uint32_t(a.variant) * 2u) * 16u);
}
int angleOfFlyerToPlayer(const AnimatedActor& a) { return angleToPlayer(a); }
// the player's position with its integer y + dy (the exe's stack copy)
void playerAbove(int dy, int32_t out[3]) {
    const Player& p = player();
    out[0] = p.pos.x;
    out[1] = int32_t((uint32_t(p.pos.y) & 0xffffu) | uint32_t(uint16_t(uint16_t(p.pos.y.whole) + uint16_t(dy))) << 16);
    out[2] = p.pos.z;
}
void positionValues(const AnimatedActor& a, int32_t out[3]) {
    out[0] = a.pos.x;
    out[1] = a.pos.y;
    out[2] = a.pos.z;
}
// the pitch stepped (0x10) toward the point
void pitchToward(AnimatedActor& a, FlyerFields& f, const int32_t point[3]) {
    int32_t at[3];
    positionValues(a, at);
    const int c = 0x800 - pitchOf(point, at) - f.pitch;
    f.pitch = int16_t(uint16_t(f.pitch) + (c > 0 ? 0x10u : 0xfff0u));
}
// the move along its yaw and its pitch (the pitch put in the rotation for exe 0x5e3810, then cleared)
void flyForward(AnimatedActor& a, const FlyerFields& f) {
    a.rotation.x = f.pitch;
    moveAlongRotation(a.pos, a.rotation, 0, 0, a.velocity.z); // exe 0x5e3810
    a.rotation.x = 0;
}
// exe 0x4eee30: the walls (as the shot slot while kCollideAsShot); never nearer than 0x20 above the ground
uint32_t flyerCollide(const PlayerEnv& env, AnimatedActor& a, FlyerFields& f) {
    uint8_t slot = 0;
    if (f.flags & kFlyerCollideAsShot) {
        slot = a.slot;
        a.slot = kShotSlot;
    }
    uint32_t r = 0;
    if (int32_t(actorTestMove(env, a)) < 0) { // exe 0x40f570
        r = actorPushOut(env, a);             // exe 0x410960
        if (int32_t(r) < 0) a.pos = a.prevPos;
        if (r & 0xf00) f.contact |= kFlyerContactWall;
    }
    const int g = int(actorGround(env, a)); // exe 0x40f190
    if (g - a.pos.y.whole < 0x20) a.pos.y.whole = int16_t(g - 0x20);
    if (f.flags & kFlyerCollideAsShot) a.slot = slot;
    return r;
}
// exe 0x4eeec0: two shots (stage static type 0x4c), one pushed 0x280 to each side
void flyerShots(const AnimatedActor& a) {
    for (int k = 0; k < 2; ++k) {
        const uint32_t address = allocActor60(kActorStageClass); // exe 0x5e12f0
        if (!address) continue;
        StaticActor& e = staticActorAt(address);
        e.flags = kActorLive;
        e.loopFlags |= kActorSweepGroup2;
        e.type = kFlyerShotType;
        e.variant = 0;
        e.pos = a.pos;
        e.rotation.y = a.rotation.y;
        integrateVelocity(e, k == 0 ? 0x280 : -0x280, 0, 0); // exe 0x5e3690
    }
}
// exe 0x4ed260: the model of its variant, the stats, its boxes, the speeds, its perch, its enemy group
void flyerInit(AnimatedActor& a, FlyerFields& f) {
    if (!bindActorModel(a, a.variant)) { // exe 0x5e0bc0(actor, &variant)
        freeActor(a);                    // exe 0x5e1490
        return;
    }
    a.flags |= kActorDrawn;
    a.anim = 6;
    a.prevAnim = 0xff;
    const uint16_t hp = flyerStats(a).hitPoints;
    a.maxHitPoints = int16_t(hp);
    a.hitPoints = int16_t(hp);
    a.velocity.setXY(0, 0);
    a.collisionBox.pointAt(flyerBox.stateAddress());
    a.interactionBox.pointAt(flyerLiftBox.stateAddress());
    a.interaction = kInteractLift;
    a.velocity.setZSpare(0, 0);
    a.rotation.x = 0;
    a.rotation.z = 0;
    a.velocity.y = 0;
    a.acceleration.y = 0x10;
    a.acceleration.x = 0x10;
    a.acceleration.z = 0x18;
    a.airborne = 1;
    a.onPlatform = 0;
    a.platform = nullptr;
    a.actorHit = 0;
    f.hitBox.result = 0;
    f.flags = 0;
    f.timer = 0;
    f.pitch = 0;
    f.perch.x = a.pos.x.whole;
    f.perch.y = a.pos.y.whole;
    f.perch.z = a.pos.z.whole;
    f.perchYaw = a.rotation.y;
    f.gauge = 0;
    f.group = uint8_t(enemyGroupFindFor(a, 1)); // exe 0x5e6cd0(actor, 1)
    a.subState = uint8_t(FlyerSub::Perch);
    a.step = 0;
    a.state = uint8_t(FlyerState::Run);
}
// the group alerted: back to the perch (its z kept unless the perch's yaw has bit 0x400)
void flyerGoHome(AnimatedActor& a, FlyerFields& f, bool resetAnim) {
    a.subState = uint8_t(FlyerSub::Return);
    a.step = 0;
    if (resetAnim && a.anim != 0) a.anim = 0;
    f.keepZ = (uint16_t(f.perchYaw) & 0x400) ? 0 : 1;
}
// exe 0x4ed5f0: perched; woken -> lifts off (the pitch levelling, backing up to 0x100), climbs, flies to the player
void flyerPerch(const PlayerEnv& env, AnimatedActor& a, FlyerFields& f) {
    switch (a.step) {
    case 0:
        if (f.wake != kWakeUp) return;
        a.step = 1;
        f.timer = 0x14;
        a.anim = 0;
        a.rotation.x = -0xd8;
        f.flags |= kFlyerInGroup;
        return;
    case 1:
        if (f.timer != 0) {
            --f.timer;
            if (a.rotation.x < 0) a.rotation.x = int16_t(uint16_t(a.rotation.x) + 8u);
            else a.rotation.x = 0;
        } else {
            ++a.step;
        }
        if (a.velocity.z > -0x100) a.velocity.z = int16_t(uint16_t(a.velocity.z) - uint16_t(a.acceleration.z));
        else a.velocity.z = -0x100;
        moveAlongYaw(a.pos, int(a.rotation.y) + kHalfTurn, 0, 0, a.velocity.z); // exe 0x5e36f0
        return;
    case 2:
        if (a.rotation.x < 0) a.rotation.x = int16_t(uint16_t(a.rotation.x) + 8u);
        else a.rotation.x = 0;
        if (a.velocity.z > 0) {
            ++a.step;
            a.velocity.z = 0;
        } else {
            a.velocity.z = int16_t(uint16_t(a.velocity.z) + uint16_t(a.acceleration.z));
        }
        turnYawTo(a.rotation.y, uint32_t(int32_t(int16_t(angleOfFlyerToPlayer(a)))), 0x20);
        moveAlongYaw(a.pos, int(a.rotation.y) + kHalfTurn, 0, 0, a.velocity.z);
        flyerCollide(env, a, f);
        return;
    case 3: {
        if (f.wake == kWakeAlerted) {
            flyerGoHome(a, f, false);
            return;
        }
        turnYawTo(a.rotation.y, uint32_t(int32_t(int16_t(angleOfFlyerToPlayer(a)))), 0x20);
        int32_t above[3], at[3];
        playerAbove(-0x140, above);
        positionValues(a, at);
        const int p = pitchOf(above, at);
        const uint16_t c = uint16_t(0x800 - p - int(uint16_t(f.pitch)));
        f.pitch = int16_t(uint16_t(f.pitch) + (int16_t(c) > 0 ? 0x10u : 0xfff0u));
        if (planarDistance(player().pos, a.pos) > 0x340) {
            if (a.velocity.z > 0x180) a.velocity.z = int16_t(uint16_t(a.velocity.z) - uint16_t(a.acceleration.z));
            else a.velocity.z = -0x180;
        } else if (a.velocity.z < 0) {
            a.velocity.z = int16_t(uint16_t(a.velocity.z) + uint16_t(a.acceleration.z));
        } else {
            a.velocity.z = 0;
            a.subState = uint8_t(FlyerSub::Hover);
            a.step = 0;
        }
        flyForward(a, f);
        flyerCollide(env, a, f);
        return;
    }
    default: return;
    }
}
// exe 0x4ed880: hovers by the player (two shots when facing it at its key 4); the player slowed -> the dive; farther
// than 0x400 -> back to the flight (perch step 2)
void flyerHover(AnimatedActor& a, FlyerFields& f) {
    if (f.wake == kWakeAlerted) {
        flyerGoHome(a, f, true);
        return;
    }
    switch (a.step) {
    case 0:
        a.anim = 1;
        a.step = 1;
        return;
    case 1:
        if (planarDistance(player().pos, a.pos) >= 0x400) {
            a.anim = 0;
            a.subState = uint8_t(FlyerSub::Perch);
            a.step = 2;
            return;
        }
        if (facingWithin(a, player().pos.x, player().pos.z, 0x40)) {
            if (a.keyIndex == 4) flyerShots(a);
        } else {
            turnYawTo(a.rotation.y, uint32_t(angleOfFlyerToPlayer(a)), 0x20);
        }
        if (uint8_t(player().status) & (kStatusSlowed | kStatusSlowedFloor)) {
            a.subState = uint8_t(FlyerSub::Dive);
            a.step = 0;
        }
        return;
    default: return;
    }
}
// exe 0x4ed970: the dive (animations 2 / 3 / 4) at the player's height - 0xc0 until it leaves the 0x80 cone, 0x14 ticks
// levelling, the brake, 0x2d ticks -> back to the flight (perch step 3)
void flyerDive(const PlayerEnv& env, AnimatedActor& a, FlyerFields& f) {
    switch (a.step) {
    case 0:
        ++a.step;
        a.anim = 2;
        break;
    case 1:
        if (a.animEnded()) {
            ++a.step;
            a.anim = 3;
        }
        break;
    case 2: {
        turnYawTo(a.rotation.y, uint32_t(int32_t(int16_t(angleOfFlyerToPlayer(a)))), 0x20);
        int32_t above[3], at[3];
        playerAbove(-0xc0, above);
        positionValues(a, at);
        const int p = pitchOf(above, at);
        const uint16_t c = uint16_t(0x800 - p - int(uint16_t(f.pitch)));
        f.pitch = int16_t(uint16_t(f.pitch) + (int16_t(c) > 0 ? 0x10u : 0xfff0u));
        if (a.velocity.z > 0x200) a.velocity.z = int16_t(uint16_t(a.velocity.z) - uint16_t(a.acceleration.z));
        else a.velocity.z = -0x200;
        flyForward(a, f);
        if (!facingWithin(a, player().pos.x, player().pos.z, 0x80)) {
            ++a.step;
            f.timer = 0x14;
        }
        break;
    }
    case 3:
        if (a.velocity.z > 0x200) a.velocity.z = int16_t(uint16_t(a.velocity.z) - uint16_t(a.acceleration.z));
        else a.velocity.z = -0x200;
        f.pitch = int16_t(uint16_t(f.pitch) + (f.pitch < 0 ? 0x10u : 0xfff0u));
        flyForward(a, f);
        if (f.timer == 0) {
            ++a.step;
            a.anim = 4;
            a.acceleration.x = 4;
        } else {
            --f.timer;
        }
        break;
    case 4:
        if (a.velocity.z < 0) {
            a.velocity.z = int16_t(uint16_t(a.velocity.z) + uint16_t(a.acceleration.z));
        } else {
            ++a.step;
            a.velocity.z = 0;
            f.timer = 0x2d;
        }
        flyForward(a, f);
        break;
    case 5:
        if (a.animEnded() && a.anim != 0) a.anim = 0;
        turnYawTo(a.rotation.y, uint32_t(int32_t(int16_t(angleOfFlyerToPlayer(a)))), 0x20);
        if (f.timer != 0) {
            --f.timer;
        } else {
            a.subState = uint8_t(FlyerSub::Perch);
            a.step = 3;
        }
        break;
    default: break;
    }
    flyerCollide(env, a, f);
}
// exe 0x4edc40: back to the perch (keeping its z when told), turning off the walls; there: placed, its perch's yaw,
// perched again (animation 6, out of the group)
void flyerReturn(const PlayerEnv& env, AnimatedActor& a, FlyerFields& f) {
    const int32_t t[3] = {int32_t(uint32_t(int32_t(f.perch.x)) << 16), int32_t(uint32_t(int32_t(f.perch.y)) << 16),
                          f.keepZ != 0 ? int32_t(a.pos.z) : int32_t(uint32_t(int32_t(f.perch.z)) << 16)};
    switch (a.step) {
    case 0:
        turnYawTo(a.rotation.y, uint32_t(angleOf(t[0], t[2], a.pos.x, a.pos.z)), 0x20);
        if (facingWithin(a, t[0], t[2], 0x80)) {
            a.step = 1;
            f.pitch = 0;
        }
        return;
    case 1: {
        if (f.flags & kFlyerWallTurn) {
            if (turnYawTo(a.rotation.y, uint32_t(int32_t(f.reflectedYaw)), 0x20) == 0)
                f.flags &= uint8_t(~kFlyerWallTurn);
        } else {
            turnYawTo(a.rotation.y, uint32_t(angleOf(t[0], t[2], a.pos.x, a.pos.z)), 0x20);
            pitchToward(a, f, t);
        }
        if (planarDistance(a.pos, t) > 0x100) {
            if (a.velocity.z > 0x180) a.velocity.z = int16_t(uint16_t(a.velocity.z) - uint16_t(a.acceleration.z));
            else a.velocity.z = -0x180;
        } else if (a.velocity.z < -0x40) {
            a.velocity.z = int16_t(uint16_t(a.velocity.z) + uint16_t(a.acceleration.z));
        } else {
            a.velocity.z = -0x40;
            ++a.step;
        }
        flyForward(a, f);
        const uint32_t r = flyerCollide(env, a, f);
        if (f.contact & kFlyerContactWall) {
            const int16_t yaw = a.rotation.y;
            reflectRotationOffWalls(a.rotation, r); // exe 0x410740
            f.reflectedYaw = a.rotation.y;
            a.rotation.y = yaw;
            f.flags |= kFlyerWallTurn;
        }
        return;
    }
    case 2:
        if (planarDistance(a.pos, t) > 0x20) {
            turnYawTo(a.rotation.y, uint32_t(angleOf(t[0], t[2], a.pos.x, a.pos.z)), 0x20);
            pitchToward(a, f, t);
            flyForward(a, f);
            return;
        }
        a.pos.x = t[0];
        a.pos.y = t[1];
        a.pos.z = t[2];
        a.rotation.x = 0;
        a.rotation.y = f.perchYaw;
        a.subState = uint8_t(FlyerSub::Perch);
        a.step = 0;
        f.wake = 0;
        a.anim = 6;
        f.flags &= uint8_t(~kFlyerInGroup);
        return;
    default: return;
    }
}
// exe 0x4edf10: dying -- an explosion at bone 0, the defeat, the drops; blinking its timer's ticks, then gone
void flyerDying(AnimatedActor& a, FlyerFields& f) {
    switch (a.step) {
    case 0: {
        if (const uint32_t address = allocActor60(0)) { // exe 0x5e1300
            int16_t p[3];
            bonePointOf(a, 0, p); // exe 0x5e1810(actor, 0)
            StaticActor& e = staticActorAt(address);
            e.flags = kActorLive;
            e.type = typeByte(GlobalStaticType::Explosion);
            e.variant = 0;
            e.loopFlags |= kActorSweepGroup2;
            e.pos.x.whole = p[0];
            e.pos.y.whole = p[1];
            e.pos.z.whole = p[2];
            ExplosionParams params(e);
            params.spread = 0x60;
            params.flashes = 0x1e;
        }
        ++a.step;
        countDefeat();                                // exe 0x5e53e0
        spawnDrops(a.pos, flyerStats(a).dropTable); // exe 0x5f1270
        return;
    }
    case 1:
        if (uint8_t(f.timer) & 1) a.tint = 0x421f;
        if (f.timer == 0) a.state = uint8_t(FlyerState::Gone);
        else --f.timer;
        return;
    default: return;
    }
}
// exe 0x4ed380: the group marked, the gauge decaying, the damage (woken by it; dead -> dying; the gauge past the row's
// +9 -> the stagger), the lift, the sub-state, the flaps' sound, its hit entry
void flyerRun(const PlayerEnv& env, AnimatedActor& a, FlyerFields& f) {
    const AnimatedActorStats stats = flyerStats(a);
    f.contact = 0;
    if (f.flags & kFlyerInGroup) enemyGroupMark(int32_t(f.group), 1); // exe 0x5e6d40
    if (f.gauge != 0 && (gameState().frame & 1)) --f.gauge;
    if ((f.hitBox.result & kHitAttackByPlayer) && !(f.flags & kFlyerDying)) {
        if (enemyGroupMarks(int32_t(f.group), EnemyGroupMarks::Either) == 0 && f.wake == 0) { // exe 0x5e6d60
            f.wake = kWakeUp;
            enemyGroupMark(int32_t(f.group), 1);
        }
        const uint16_t hp = uint16_t(a.hitPoints);
        a.tint = kTintFlash;
        soundPlayAtPos(kSoundHurt, a.pos);
        if (hitPointsAdd(a, -int(f.hitBox.result & kHitDamageMask)) != 0) { // exe 0x5e52f0
            a.subState = uint8_t(FlyerSub::Dying);
            a.step = 0;
            f.timer = 0x1e;
            f.flags |= kFlyerDying;
        } else {
            const uint8_t fl = f.flags;
            if (!(fl & kFlyerStaggered) && (fl & kFlyerInGroup)) {
                if (int16_t(f.gauge) > int(stats.hitSize)) {
                    a.subState = uint8_t(FlyerSub::Stagger);
                    a.step = 0;
                    a.anim = 5;
                    f.flags = uint8_t(fl | kFlyerStaggered);
                } else {
                    f.gauge = uint16_t(uint32_t(f.gauge) - uint16_t(a.hitPoints) + hp);
                }
            }
        }
    } else if (playerLiftTargetCheck(a, 0) && !(f.flags & kFlyerLifted)) { // exe 0x573a60
        f.flags |= kFlyerLifted;
    }
    switch (FlyerSub(a.subState)) {
    case FlyerSub::Perch: flyerPerch(env, a, f); break;
    case FlyerSub::Hover: flyerHover(a, f); break;
    case FlyerSub::Dive: flyerDive(env, a, f); break;
    case FlyerSub::Return: flyerReturn(env, a, f); break;
    case FlyerSub::Stagger: // exe 0x4eecc0: the stagger's animation, then the flight (perch step 3)
        if (a.animEnded()) {
            a.anim = 6;
            a.subState = uint8_t(FlyerSub::Perch);
            a.step = 3;
            a.rotation.x = 0;
            f.pitch = 0;
        }
        break;
    case FlyerSub::Dying: flyerDying(a, f); break;
    default: notPortedAt(kStoryPort, "0x4ed380 sub", a.subState);
    }
    if (a.animKey.events & 1) soundPlayAtPos(a.anim == 3 ? kSoundDiveFlap : kSoundFlap, a.pos);
    f.hitBox.previousX = a.prevPos.x.whole;
    f.hitBox.previousZ = a.prevPos.z.whole;
    const uint8_t fl = f.flags;
    f.hitBox.result = 0;
    const int16_t p[3] = {a.pos.x.whole, int16_t(uint16_t(a.pos.y.whole) - 0x2eu), a.pos.z.whole};
    const uint32_t owner = hitOwner(a.slot) | hitHeightLevel(stats.hitHeight) | hitGroup(kHitGroupBodies) |
                           hitRadius(0x94);
    if (!(fl & kFlyerDying)) {
        hitListSubmit(p, f.hitBox, owner, uint32_t(stats.attack) | kHitAttackSolid); // exe 0x5e4120
        return;
    }
    if (f.timer == 0) return;
    hitListSubmit(p, f.hitBox, owner, uint32_t(stats.attack) | kHitAttackBump);
    --f.timer;
}
// exe 0x4ed1a0
void flyerUpdate(const PlayerEnv& env, AnimatedActor& a) {
    auto&& f = classState<FlyerFields>(a);
    a.prevAnim = a.anim;
    a.tint = kTintNeutral;
    savePrevious(a);
    switch (FlyerState(a.state)) {
    case FlyerState::Init: flyerInit(a, f); break;
    case FlyerState::Run: flyerRun(env, a, f); break;
    case FlyerState::Gone: freeActor(a); break; // exe 0x468fa0
    default: notPortedAt(kStoryPort, "0x4ed1a0 state", a.state);
    }
    if (a.anim == a.prevAnim) {
        if (!(f.flags & kFlyerDecodeOnly)) animStep(a); // exe 0x5e1ea0
        else animDecodeCurrent(a);                      // exe 0x5e1f30
    } else {
        animSet(a, a.anim, 0); // exe 0x5e1c50
    }
    animPose(a); // exe 0x5e2d90
}

// ==== the homing marker (exe 0x55f350: pool 0xa0 type 6, states [0x8c3f68]) ==========================================
// 8 ticks of a shrinking ring at the player's bone point 0 pushed 0x80 along its angles: a 0xf0 camera-plane gouraud
// ring draw command (the colour (n + 4) << 4); the GT4 band of OT packets is render output.
enum class MarkerState : uint8_t { Start = 0, Run = 1 }; // exe 0x55f370 / 0x55f3b0
struct MarkerFields {
    explicit MarkerFields(const EffectActor& actor) : MarkerFields(const_cast<EffectActor&>(actor)) {}
    explicit MarkerFields(EffectActor& actor) : pitch(actor.params, 0), yaw(actor.params, 2), ticks(actor.trail[0]) {}
    BytesField<int16_t, 4> pitch;
    BytesField<int16_t, 4> yaw;
    FieldAlias<uint16_t, int16_t> ticks;
};
constexpr uint16_t kMarkerTicks = 8;
constexpr uint16_t kSoundMarker = 0xfc;

// exe 0x55f3e0(actor, counter): the ring
void markerDraw(const EffectActor& e, const MarkerFields& f) {
    const Matrix3 m = rotationZYX(f.pitch, f.yaw, 0); // exe 0x40c070
    int16_t p[3];
    for (int i = 0; i < 3; ++i) p[i] = int16_t((int32_t(m.e[i * 3 + 2]) * 0x80) >> 12); // exe 0x40bb30 on (0, 0, 0x80)
    const ShortVec3& target = player().bonePoints[0];
    p[0] = int16_t(p[0] + target.x);
    p[1] = int16_t(p[1] + target.y);
    p[2] = int16_t(p[2] + target.z);
    const uint32_t c = (uint32_t(f.ticks) + 4u) << 4;
    dc::Ring ring;
    ring.inner = dc::ringColour(c | c << 8 | c << 16);
    ring.xy = dc::packXY(p[0], p[1]);
    ring.outer = dc::kRingFadedRim;
    ring.zStep = uint32_t(uint16_t(p[2])) | dc::ringStep(2);
    ring.radii = dc::ringRadii(0, 0x40);
    dc::emit(ring, e.at);
    // the GT4 band (2 rings x 16 quads of 0x34-byte GPU packets at *0x6d004c into the OT 0x6d0048, GTE projected
    // through 0x6d0014): render output, not drawn by the port's draw-command path
}
// exe 0x55f350
void markerUpdate(EffectActor& e) {
    auto&& f = MarkerFields(e);
    switch (MarkerState(e.state)) {
    case MarkerState::Start:
        e.runFlags |= kActorWarpKept;
        f.ticks = kMarkerTicks;
        markerDraw(e, f);
        soundPlayAtPlayer(kSoundMarker); // exe 0x5d53e0
        ++e.state;
        return;
    case MarkerState::Run:
        --f.ticks;
        if (f.ticks != 0) markerDraw(e, f);
        else freeActor(e); // exe 0x5e1580
        return;
    default: notPortedAt(kStoryPort, "0x55f350 state", e.state);
    }
}

} // namespace

void examDroneUpdate(const PlayerEnv& env, uint32_t actor) { droneUpdate(env, staticActorAt(actor)); }
void examAmbushUpdate(const PlayerEnv&, uint32_t actor) { ambushUpdate(controllerActorAt(actor)); }
void examMineUpdate(const PlayerEnv& env, uint32_t actor) { mineUpdate(env, staticActorAt(actor)); }
void examHatchUpdate(const PlayerEnv&, uint32_t actor) { hatchUpdate(animatedActorAt(actor)); }
void examDiggerUpdate(const PlayerEnv& env, uint32_t actor) { diggerUpdate(env, animatedActorAt(actor)); }
void examAmbushFlyerUpdate(const PlayerEnv& env, uint32_t actor) { flyerUpdate(env, animatedActorAt(actor)); }
void homingMarkerUpdate(const PlayerEnv&, uint32_t actor) { markerUpdate(effectActorAt(actor)); }

} // namespace od::port
