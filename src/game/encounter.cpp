// Enemy encounters (see encounter.hpp), one routine per exe routine. Widths and signedness checked against the
// disassembly where the decompile was ambiguous. Not replicated (render scratch only, overwritten by every projection):
// the float screen-coordinate / depth history of the exe's projection (0xa59a58..0xa59a6c, 0xa59d28..0xa59d34,
// 0xa599e4).
#include "game/loaded_data.hpp"
#include "game/actor_state.hpp"
#include "game/encounter.hpp"

#include <algorithm>
#include <array>

#include "engine/data_source.hpp"
#include "game/actor_pools.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/camera_state.hpp"
#include "game/distances.hpp"
#include "game/event_state.hpp"
#include "game/game_state.hpp"
#include "game/matrix.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/projection.hpp"
#include "game/projection_state.hpp"
#include "game/random.hpp"
#include "game/room_grid.hpp"
#include "game/script_state.hpp"
#include "game/story_flags.hpp"
#include "game/transition_state.hpp"

namespace od::port {

namespace {

// ---- the generated tables of the stage setups ----------------------------------
struct EncStage {
    RoutineId fn;
    int32_t kinds; // exe 0x50f280's argument, -1 none
    int32_t root;  // the layout tree's root in kEncLayoutOps, -1 none
};
// a node of the layout tree of an RNG stage setup
enum class LayoutOp : uint8_t {
    End = 0,
    ClearFlag = 1, // the story flag cleared
    SetFlag = 2,   // the story flag set
    TestFlag = 3,  // next[0] when the flag is clear, next[1] when set
    RngStep = 4,   // one RNG step: next[r & 3]
    Write = 5,     // the global = the op's table (a zone / group table: TableId::EncounterLayoutTable by the op's index)
};
struct EncLayoutOp {
    LayoutOp op;
    uint16_t flag;
    StageGlobal global;
    int16_t next[4];
};
#include "game/encounter_tables.inc"
#include "game/vectors.hpp"
constexpr int kStages = int(sizeof kEncStages / sizeof kEncStages[0]);
static_assert(kStages == 93, "93 stages");
constexpr int kLayoutTreeLimit = 64; // the deepest tree is far below (a malformed table is a port gap)

constexpr const char* kEncounterPort = "encounter port"; // the area of its port-gap errors

// ---- the records ----------------------------------------------------------------------------------------------------

// A zone record (8 bytes of a zone table [*zoneTables + room * 4]; kZoneEnd in +0 ends the list): a box of cells and
// the group it triggers
struct EncounterZone : DataRecord {
    static constexpr uint32_t kBytes = 0x8;
    uint8_t marker = 0; // +0x0: kZoneEnd
    int8_t group = 0; // +0x2
    uint8_t bits = 0; // +0x3: kZoneExit (the type), kZoneYawFlip
    int8_t cellX = 0; // +0x4: the box: first cell x / z, width / depth in cells
    int8_t cellZ = 0; // +0x5
    uint8_t width = 0; // +0x6
    uint8_t depth = 0; // +0x7
    template <class R> static EncounterZone load(const R& r) {
        EncounterZone v;
        v.marker = r.template read<uint8_t>(0x0);
        v.group = r.template read<int8_t>(0x2);
        v.bits = r.template read<uint8_t>(0x3);
        v.cellX = r.template read<int8_t>(0x4);
        v.cellZ = r.template read<int8_t>(0x5);
        v.width = r.template read<uint8_t>(0x6);
        v.depth = r.template read<uint8_t>(0x7);
        return v;
    }
};
constexpr uint8_t kZoneEnd = 0xff;
constexpr uint32_t kZoneTypeMask = 0x1;
constexpr uint32_t kZoneTrigger = 0; // the zone's type: entering it spawns its group
constexpr uint32_t kZoneExit = 1;    // leaving the fight through it ends it
constexpr uint8_t kZoneYawFlip = 0x80; // the group's records may face the other way
constexpr uint32_t kZoneSize = 8;

// A group record (0x18 bytes of a group [*groupTables + g * 4]; kZoneEnd in +0 ends it) and, its first 0xc bytes, a
// field encounter's class record (0xc bytes)
struct EncounterRecord : DataRecord {
    static constexpr uint32_t kBytes = 0x18;
    uint8_t flags = 0; // +0x0: the actor's +0
    uint8_t runFlags = 0; // +0x1: ORed into the actor's +1 (kRecordYawFlip: may face the other way)
    uint8_t pool = 0; // +0x2: 0x20 / 0x60 / 0xa0 / 0xe0 (any other: skipped)
    uint8_t loopFlags = 0; // +0x3: ORed into the actor's +3
    uint32_t typeWord = 0; // +0x4
    uint32_t paramsWord = 0; // +0x8
    ShortVec4 point; // +0xc: the integer position and, in the 4th lane, the yaw
    uint16_t yaw = 0; // +0x12
    uint32_t id = 0; // +0x14: its enemy id (a defeated one is skipped)
    template <class R> static EncounterRecord load(const R& r) {
        EncounterRecord v;
        v.flags = r.template read<uint8_t>(0x0);
        v.runFlags = r.template read<uint8_t>(0x1);
        v.pool = r.template read<uint8_t>(0x2);
        v.loopFlags = r.template read<uint8_t>(0x3);
        v.typeWord = r.template read<uint32_t>(0x4);
        v.paramsWord = r.template read<uint32_t>(0x8);
        v.point = {r.template read<int16_t>(0xc), r.template read<int16_t>(0xe), r.template read<int16_t>(0x10), r.template read<int16_t>(0x12)};
        v.yaw = r.template read<uint16_t>(0x12);
        v.id = r.template read<uint32_t>(0x14);
        return v;
    }
};
constexpr uint8_t kRecordYawFlip = 0x80;
constexpr uint32_t kGroupRecordSize = 0x18;

// the field encounters' odds (exe data): the cells per walk stage, the odds by level + stage (bit r & 0x1f of the
// word), the wave-end odds by row (r & 0x1e shift: 2 bits)
const DataTable<ScalarTable<uint8_t, 8>> fieldStageCells{TableId::EncounterFieldStageCells};
const DataTable<ScalarTable<uint32_t, 0x10>> fieldOdds{TableId::EncounterFieldOdds};
const DataTable<ScalarTable<uint32_t, 0x10>> waveEndOdds{TableId::EncounterWaveEndOdds}; // 2-bit odds packed in words
// the stage's field encounter table through its typed load (the lists the exe reads by pointer)
const TableFamily<FieldEncounterData> fieldTables{TableId::StageEncounterField};
const FieldEncounterData& fieldEncounterData() { return fieldTables[uint32_t(gameState().stage)]; }

// the keep test (exe 0x510ad0): the screen, how far a seen enemy may be, the facing window of mode 4
constexpr uint16_t kScreenWidth = 0x140, kScreenHeight = 0xf0;
constexpr uint32_t kSeenFarDistance2 = 0xa900000;
constexpr uint32_t kKeepModeMask = 0xc;
constexpr uint32_t kKeepByRadius = 0x0;   // out of sight: kept within the radius
constexpr uint32_t kKeepFacing = 0x4;     // out of sight: kept within the radius or in front of the player
constexpr int kKeepFacingWindow = 0x400;
constexpr uint8_t kActorKeptAlways = 0x10; // +1: the keep test never despawns it
// the formation's anchor and turn (exe 0x5101a0 / 0x5100a0)
constexpr int16_t kPlayerPitchLift = 0xe0;
constexpr int16_t kBlockedTurn = kQuarterTurn; // the roll's facing turned when the formation's area is blocked
// the battle music (exe 0x50fe50's tail)
constexpr uint32_t kStageMusicFade = 1u << 0; // the fade entry of the stage music's channel
constexpr uint32_t kBattleMusicFade = 1u << 1; // ... of the battle music's
constexpr int16_t kMusicCutStep = 0x3fff;     // dipped to silence at once
constexpr int16_t kMusicBackStep = 0x111;
constexpr uint8_t kBattleMusicFromCalm = 0x12, kBattleMusicToCalm = 0x21, kBattleMusicStaysCalm = 0x11;

// ---- the tracked actors --------------------------------------------------------------------------------------------

// the actor of a tracked entry is live in its pool (bit 31 - index of the pool's live mask)
bool trackedLive(uint32_t cls, uint8_t i) {
    const ActorSystemState& s = actorSystem();
    switch (cls) {
    case kAnimatedClass: return int32_t(uint32_t(s.animatedLive) << (i & 0x1f)) < 0;
    case kStaticClass: return int32_t(uint32_t(s.staticsLive[i >> 5]) << (i & 0x1f)) < 0;
    case kEffectClass: return int32_t(uint32_t(s.effectsLive[i >> 5]) << (i & 0x1f)) < 0;
    case kControllerClass: return int32_t(uint32_t(s.controllersLive) << (i & 0x1f)) < 0;
    default: return false;
    }
}
// the pending free of a tracked entry's actor (by its pool)
void freeTracked(uint32_t cls, uint8_t i) {
    switch (cls) {
    case kAnimatedClass: freeActor(animatedPool()[i]); break;
    case kStaticClass: freeActor(staticPool()[i]); break;
    case kEffectClass: freeActor(effectPool()[i]); break;
    case kControllerClass: freeActor(controllerPool()[i]); break;
    default: break;
    }
}

// exe 0x50fdc0(kind, actor, id): the first free tracked entry = {the actor's class | kind, its pool index, id}, its bit
// set; false when all are in use (the exe returns the entry's address; its callers only test it)
bool trackActor(uint8_t kind, const ActorHeader& actor, uint16_t id) {
    EncounterState& e = encounterState();
    uint32_t entry = 0;
    for (size_t w = 0; w < e.trackedMasks.size(); ++w) {
        uint32_t v = e.trackedMasks[w];
        for (int bit = 31; bit >= 0; --bit, ++entry, v <<= 1) {
            if (int32_t(v) < 0) continue;
            const uint8_t slot = actor.slot;
            const uint32_t cls = actorSlotClass(slot);
            auto&& t = e.tracked[entry];
            t.kind = uint8_t((cls & 0xf0) | kind);
            t.index = uint8_t(slot - cls);
            t.id = id;
            e.trackedMasks[w] |= 1u << bit;
            return true;
        }
    }
    return false;
}

// ---- the records into actors ---------------------------------------------------------------------------------------

// exe 0x50fd20(actor, record): the flags, the run / loop flags ORed, the type and parameter words
void applyRecord(ActorHeader& a, const EncounterRecord& r) {
    a.flags = r.flags;
    a.runFlags |= r.runFlags;
    a.loopFlags |= r.loopFlags;
    a.setTypeWord(r.typeWord);
    a.setParamsWord(r.paramsWord);
}
// the same for a field encounter's class record (the 0xc bytes fieldWave reads)
void applyRecord(ActorHeader& a, const FieldClassRecord& r) {
    a.flags = r.flags;
    a.runFlags |= r.runFlags;
    a.loopFlags |= r.loopFlags;
    a.setTypeWord(r.typeWord);
    a.setParamsWord(r.paramsWord);
}
// exe 0x50fd60(actor, point, yawAdd): the 16.16 position and the previous one from the s16 point, the rotation x / z
// cleared, the yaw = the point's 4th lane + yawAdd (current and previous)
void placeRecord(Actor& a, int16_t x, int16_t y, int16_t z, uint16_t pointYaw, int16_t yawAdd) {
    a.prevPos.x = int32_t(uint32_t(int32_t(x)) << 16);
    a.pos.x = int32_t(uint32_t(int32_t(x)) << 16);
    a.prevPos.y = int32_t(uint32_t(int32_t(y)) << 16);
    a.pos.y = int32_t(uint32_t(int32_t(y)) << 16);
    a.prevPos.z = int32_t(uint32_t(int32_t(z)) << 16);
    a.pos.z = int32_t(uint32_t(int32_t(z)) << 16);
    a.prevRotation.x = 0;
    a.rotation.x = 0;
    a.prevRotation.z = 0;
    a.rotation.z = 0;
    const int16_t yaw = int16_t(uint16_t(pointYaw + uint16_t(yawAdd)));
    a.prevRotation.y = yaw;
    a.rotation.y = yaw;
}
// the actor of a pool code (exe 0x5e1160 / 0x5e12f0 / 0x5e13a0 / 0x5e1440: the stage's class tables), 0 when full or
// none of these
uint32_t allocEncounterActor(uint8_t pool) {
    switch (pool) {
    case kAnimatedClass: return allocActor20();
    case kStaticClass: return allocActor60(kActorStageClass);
    case kEffectClass: return allocActorA0(kActorStageClass);
    case kControllerClass: return allocActorE0();
    default: return 0;
    }
}
// a record's pool byte is one the group spawn allocates from (allocEncounterActor's cases; other records are skipped)
bool knownEncounterPool(uint8_t pool) {
    return pool == kAnimatedClass || pool == kStaticClass || pool == kEffectClass || pool == kControllerClass;
}

// ---- kind 0: zone-triggered groups ---------------------------------------------------------------------------------

// exe 0x50fcc0(actor, zone, type): the zone is of `type` and the actor's cell lies in its box (unsigned differences)
bool inZone(const Fixed16Vec3& pos, const EncounterZone& z, uint32_t type) {
    if (type != (z.bits & kZoneTypeMask)) return false;
    if (uint32_t(int32_t(pos.x.whole >> 9) - int32_t(z.cellX)) >= z.width) return false;
    return uint32_t(int32_t(pos.z.whole >> 9) - int32_t(z.cellZ)) < z.depth;
}
// the first zone record of the slot's room
uint32_t roomZones(const EncounterSlot& slot) {
    return dataScalar<uint32_t>(encounterState().zoneTables + uint32_t(slot.room) * 4);
}

// exe 0x50f720 (Fight): the alive counted (exe 0x50f780); none left, or the player in an exit zone of the slot's group
// -> armed again, Clear
void zoneFight(EncounterSlot& slot) {
    encounterCountAlive(slot);
    bool end = slot.alive == 0;
    if (!end) {
        uint32_t at = roomZones(slot);
        if (dataRecord<EncounterZone>(at, EncounterZone::kBytes).marker == kZoneEnd) return;
        for (;; at += kZoneSize) {
            const EncounterZone z = dataRecord<EncounterZone>(at, EncounterZone::kBytes);
            if (uint8_t(z.group) == slot.group && inZone(player().pos, z, kZoneExit)) {
                end = true;
                break;
            }
            if (dataRecord<EncounterZone>(at + kZoneSize, EncounterZone::kBytes).marker == kZoneEnd) return;
        }
    }
    slot.active = kEncounterSlotArmed;
    slot.enterState(uint8_t(ZoneState::Clear));
}
// exe 0x50f3f0 (Wait): unless zones are suspended or a transition is asked: the first trigger zone of the room holding
// the player -> its group spawned (the yaw flip by the zone), active, Fight, then the fight state at once
void zoneWait(EncounterSlot& slot) {
    uint32_t at = roomZones(slot);
    if ((encounterState().control & kZonesSuspended) || transitionRequest().state != kTransitionNone ||
        dataRecord<EncounterZone>(at, EncounterZone::kBytes).marker == kZoneEnd)
        return;
    while (!inZone(player().pos, dataRecord<EncounterZone>(at, EncounterZone::kBytes), kZoneTrigger)) {
        at += kZoneSize;
        if (dataRecord<EncounterZone>(at, EncounterZone::kBytes).marker == kZoneEnd) return;
    }
    const EncounterZone z = dataRecord<EncounterZone>(at, EncounterZone::kBytes);
    slot.alive = 0;
    slot.group = uint8_t(z.group);
    slot.lost = 0;
    encounterSpawnGroup(slot, (z.bits & kZoneYawFlip) != 0, int32_t(z.group));
    slot.active = kEncounterSlotRunning;
    slot.enterState(uint8_t(ZoneState::Fight));
    zoneFight(slot);
}
// exe 0x50f6f0 (Respawn): the slot's group spawned again (no yaw flip), active, Fight, then the fight state at once
void zoneRespawn(EncounterSlot& slot) {
    slot.alive = 0;
    slot.lost = 0;
    encounterSpawnGroup(slot, false, int32_t(int8_t(slot.group)));
    slot.active = kEncounterSlotRunning;
    slot.enterState(uint8_t(ZoneState::Fight));
    zoneFight(slot);
}
// exe 0x50f3d0: the zone slot's state
void zoneSlot(EncounterSlot& slot) {
    switch (ZoneState(slot.state)) {
    case ZoneState::Wait: zoneWait(slot); return;
    case ZoneState::Respawn: zoneRespawn(slot); return;
    case ZoneState::Fight: zoneFight(slot); return;
    case ZoneState::Clear: // exe 0x50fbb0
        encounterDespawn(slot);
        slot.enterState(uint8_t(ZoneState::Wait));
        return;
    default: notPortedAt(kEncounterPort, "encounter zone state", slot.state);
    }
}

// ---- kind 1: random field encounters -------------------------------------------------------------------------------

// a 16-bit counter's wrap
int16_t wrap16(int32_t v) { return int16_t(uint16_t(uint32_t(v))); }

// exe 0x510d60(x0, x1, z0, z1): every grid cell x0..x1 / z0..z1 (16-bit counters) of the room is a plain floor cell
// (collides, floor, not special): 0; else -1
int cellsBlocked(int16_t x0, int32_t x1, int16_t z0, int32_t z1) {
    for (int16_t z = z0; int32_t(z) <= z1; z = wrap16(z + 1)) {
        for (int16_t x = x0; int32_t(x) <= x1; x = wrap16(x + 1)) {
            const uint16_t c = GridCell(gridCellAtIndex(uint32_t(int32_t(x)), uint32_t(int32_t(z)))).word;
            if (int16_t(c) >= 0 || !(c & kCellFloor) || (c & kCellSpecial)) return -1;
        }
    }
    return 0;
}
// exe 0x510cb0(x, z): the 2 x 2 cells around (x, z) toward the nearer half-cell
int areaBlocked(int32_t x, int32_t z) {
    const int32_t xs = x + kGridOrigin, zs = z + kGridOrigin;
    const int32_t cx = (xs >> kCellShift) & 0xff, cz = (zs >> kCellShift) & 0xff;
    switch ((((zs >> 1) & 0x80) | (xs & 0x100)) >> 7) {
    case 0: return cellsBlocked(wrap16(cx - 1), cx, wrap16(cz - 1), cz);
    case 1: return cellsBlocked(wrap16(cx - 1), cx, wrap16(cz), cz + 1);
    case 2: return cellsBlocked(wrap16(cx), cx + 1, wrap16(cz - 1), cz);
    case 3: return cellsBlocked(wrap16(cx), cx + 1, wrap16(cz), cz + 1);
    default: return -1;
    }
}
// exe 0x510c00: an RNG step picks the formation. The typed load's picks are the table's; a stage's spawner may rewrite
// the table's pick pointer (game/patched_data), then the picks are that list's (the exe reads [table + 8]).
uint32_t pickFormation() {
    const uint32_t r = rngStep();
    const FieldEncounterData& t = fieldEncounterData();
    const FieldEncounterTable raw = recordAt<FieldEncounterTable>(encounterState().fieldTable);
    const uint32_t i = r & 0x1f;
    const uint32_t pick = raw.formationPick == t.formationPickAddress ? t.formationPick[i]
                                                                      : dataScalar<uint8_t>(raw.formationPick + i);
    return t.formationsAddress + pick * kFormationSize;
}
// exe 0x510c40(kind, yaw): an RNG step picks the position set of the kind ({base, count}: base + (r & 0xffff) % count),
// its quarter by the roll's facing
uint32_t pickPositions(uint32_t kind, int32_t yaw) {
    const FieldEncounterData& t = fieldEncounterData();
    const auto [base, n] = t.positionRange(kind);
    rngStep();
    if (n == 0) notPorted(kEncounterPort, "a field position kind of count 0 (the exe divides by zero)");
    const uint32_t q = uint32_t(uint16_t(rngLastValue())) % n;
    return t.positionSetsAddress + ((uint32_t((yaw + 0x200) >> 10) & 3u) + (q + base) * kPositionSetsPerKind) * kPositionSize;
}
// exe 0x5101a0(slot): a formation and a position set picked (the class and position cursors), the scratch transform
// turned for it (by the roll's yaw, by the player's rotation with the pitch at most level and lifted, or kept) and the
// point (0, 0, -distance) through it: the formation's offset. The position set.
uint32_t pickFieldFormation(EncounterSlot& slot) {
    EncounterState& e = encounterState();
    ProjectionScratch& s = projectionScratch();
    const FieldEncounterData& t = fieldEncounterData();
    const uint32_t fa = pickFormation();
    const Formation& f = t.formationAt(fa);
    e.formation = fa;
    e.classCursor = f.classes;
    const uint32_t setAt = pickPositions(uint32_t(f.mode) >> kFormationPositionShift, slot.rollMoveYaw);
    e.positionCursor = t.positionSetAt(setAt).positions;
    s.transform.setIdentityWords();
    switch (f.mode & kFormationTurnMask) {
    case kFormationTurnRoll:
        // exe 0x40bea0: m = Ry * m
        s.transform.matrix =
            mulMatrix3x3(rotationY(int32_t(slot.rollYaw) - int32_t(slot.rollMoveYaw)), s.transform.matrix);
        loadProjectionMatrix(s.transform);
        loadProjectionTranslation(s.transform);
        break;
    case kFormationTurnPlayer:
        s.point = player().rotation;
        if (s.point.x > 0) s.point.x = 0;
        s.point.x = int16_t(s.point.x - kPlayerPitchLift);
        s.transform.setRotationYXZ(s.point);
        loadProjectionMatrix(s.transform);
        loadProjectionTranslation(s.transform);
        break;
    default: break;
    }
    s.point.x = 0;
    s.point.y = 0;
    s.point.z = int16_t(-int32_t(f.distance));
    loadProjectionPoint(s.point);
    transformProjectionPoint();
    storeProjectionPoint(s.transformed);
    return setAt;
}
// exe 0x5102d0(slot) (Wave): the next wave of the formation around the anchor, turned by the roll's yaw; each position
// whose 2 x 2 cells are free floor spawns its class record (tracked under the formation's kind bits | the slot's kind,
// id = the formation's keep ticks); between positions (unless kFormationNoPause) an RNG roll on the wave-end odds can
// pause the wave. The set done: alive -> LastWave, else armed again (Arm).
void fieldWave(EncounterSlot& slot) {
    EncounterState& e = encounterState();
    ProjectionScratch& s = projectionScratch();
    s.transform.setIdentityWords();
    // exe 0x40bea0: m = Ry * m
    s.transform.matrix = mulMatrix3x3(rotationY(int32_t(slot.rollYaw) - int32_t(slot.rollMoveYaw)), s.transform.matrix);
    loadProjectionMatrix(s.transform);
    loadProjectionTranslation(s.transform);
    const int16_t x = slot.anchor.x, y = slot.anchor.y, z = slot.anchor.z;
    const FieldEncounterData& t = fieldEncounterData();
    const size_t fi = t.formationIndex(e.formation);
    const Formation& f = t.formations[fi];
    const std::vector<int8_t>& classList = t.classLists[fi];
    const uint8_t kind = uint8_t((f.mode & kFormationKindBits) | slot.kind);
    uint32_t posAt = e.positionCursor, listIndex = e.classCursor - f.classes;
    if (listIndex >= classList.size()) tableIndexError(listIndex, classList.size());
    for (;;) {
        const FormationPosition& pos = t.positionAt(posAt);
        loadProjectionPoint(pos.point);
        transformProjectionPoint();
        storeProjectionPoint(s.transformed);
        s.point.x = int16_t(uint16_t(x) + uint16_t(uint32_t(s.transformed[0])));
        s.point.y = y;
        s.point.z = int16_t(uint16_t(z) + uint16_t(uint32_t(s.transformed[2])));
        s.pointYaw = int16_t((uint32_t(uint16_t(pos.yaw)) - uint16_t(slot.rollMoveYaw) + uint16_t(slot.rollYaw) -
                              uint32_t(kHalfTurn)) &
                             kAngleMask);
        const FieldClassRecord& rec = t.classRecord(classList[listIndex]);
        if (areaBlocked(s.point.x, s.point.z) == 0) {
            e.battleMusicRequest = kBattleMusicFight;
            const uint8_t pool = rec.pool;
            const uint32_t a = allocEncounterActor(pool);
            if (a && trackActor(kind, actorHeaderAt(a), uint16_t(f.keepTicks))) {
                applyRecord(actorHeaderAt(a), rec);
                if (pool == kAnimatedClass || pool == kStaticClass) {
                    placeRecord(actorAt(a), s.point.x, s.point.y, s.point.z, uint16_t(s.pointYaw), 0);
                } else if (pool == kEffectClass) {
                    effectActorAt(a).setPositionWords(s.point);
                } else {
                    controllerActorAt(a).setRecordPointWords(s.point);
                }
                ++slot.alive;
            }
        }
        ++listIndex;
        if (pos.yaw < 0 || listIndex >= classList.size() || classList[listIndex] < 0) {
            if (slot.alive != 0) {
                slot.enterState(uint8_t(FieldState::LastWave));
            } else {
                slot.active = kEncounterSlotArmed;
                slot.enterState(uint8_t(FieldState::Arm));
            }
            return;
        }
        posAt += kPositionSize;
        if (f.mode & kFormationNoPause) continue;
        const uint32_t r = rngStep();
        const uint32_t v = (waveEndOdds[e.waveOddsRow] >> (r & 0x1e)) & 3u;
        slot.pause = uint16_t(v);
        if (v != 0) {
            slot.waveStep = uint8_t(FieldWaveStep::Pause);
            e.classCursor = f.classes + listIndex;
            e.positionCursor = posAt;
            return;
        }
    }
}
// exe 0x5100a0 (Start): active, Wave; a formation at the anchor; its first area blocked -> the roll's move yaw turned
// by a quarter (toward the half turn) and picked again, twice at most; the anchor moved by the formation's offset, the
// counts cleared, the position set's bytes kept, then the first wave at once
void fieldStart(EncounterSlot& slot) {
    EncounterState& e = encounterState();
    ProjectionScratch& s = projectionScratch();
    slot.active = kEncounterSlotRunning;
    slot.waveStep = uint8_t(FieldWaveStep::Wave);
    uint32_t setAt = pickFieldFormation(slot);
    const int32_t z = slot.anchor.z, x = slot.anchor.x, y = slot.anchor.y;
    if (areaBlocked(s.transformed[0] + x, s.transformed[2] + z) != 0) {
        const int16_t d = slot.rollMoveYaw <= kHalfTurn ? kBlockedTurn : int16_t(-kBlockedTurn);
        slot.rollMoveYaw = int16_t(slot.rollMoveYaw + d);
        setAt = pickFieldFormation(slot);
        if (areaBlocked(s.transformed[0] + x, s.transformed[2] + z) != 0) {
            slot.rollMoveYaw = int16_t(slot.rollMoveYaw + d);
            setAt = pickFieldFormation(slot);
        }
    }
    slot.anchor.x = int16_t(uint16_t(uint32_t(s.transformed[0])) + uint32_t(x));
    slot.anchor.y = int16_t(uint32_t(y) + uint16_t(uint32_t(s.transformed[1])));
    slot.anchor.z = int16_t(uint32_t(z) + uint16_t(uint32_t(s.transformed[2])));
    slot.alive = 0;
    slot.lost = 0;
    const PositionSet& set = fieldEncounterData().positionSetAt(setAt);
    e.waveOddsRow = set.waveOddsRow;
    e.positionSetByte = set.unreadByte;
    fieldWave(slot);
}
// exe 0x510ad0(actor, entry, radius2, ticks): the actor's point (the scratch point) projected with the camera (its
// screen point kept in the scratch); +1 kActorOffScreen when off the screen. Unless kActorKeptAlways: on the screen
// and farther than kSeenFarDistance2 from the player, or off the screen and (kKeepByRadius: farther than the radius;
// kKeepFacing: farther and not within a quarter turn of the player's facing) -> the entry's ticks count down, -1
// (despawn) at 0; otherwise the ticks reset (the facing case leaves them). 0 / -1
int fieldKeepTest(ActorHeader& a, TrackedActor& entry, uint32_t radius2, uint16_t ticks) {
    ProjectionScratch& s = projectionScratch();
    const Player& p = player();
    const uint8_t runFlags = a.runFlags & uint8_t(~kActorOffScreen);
    a.runFlags = runFlags;
    loadProjectionPoint(s.point);
    transformProjectionPoint(); // exe 0x40c860's integer half (the camera loaded by the caller)
    const int16_t v[3] = {s.point.x, s.point.y, s.point.z};
    const CameraPoint cp = cameraPoint(v); // the same camera as the loaded registers
    projection().clipFlags = cp.clipped ? kProjectionClipped : 0u;
    s.screenX = cp.sx; // exe 0x40cb10
    s.screenY = cp.sy;
    const int32_t dx = int32_t(s.point.x) - p.pos.x.whole, dz = int32_t(s.point.z) - p.pos.z.whole;
    const uint32_t d2 = uint32_t(dz) * uint32_t(dz) + uint32_t(dx) * uint32_t(dx);
    auto countDown = [&]() {
        entry.id = uint16_t(entry.id - 1u);
        return entry.id != 0 ? 0 : -1;
    };
    if (uint16_t(s.screenX) < kScreenWidth && uint16_t(s.screenY) < kScreenHeight) {
        if (!(runFlags & kActorKeptAlways) && d2 > kSeenFarDistance2) return countDown();
    } else {
        a.runFlags = uint8_t(runFlags | kActorOffScreen);
        if (!(runFlags & kActorKeptAlways)) {
            const uint32_t mode = entry.kind & kKeepModeMask;
            if (mode == kKeepByRadius) {
                if (d2 > radius2) return countDown();
            } else if (mode == kKeepFacing && d2 > radius2) {
                const int ang = angleOf(actorLayoutOf(a).pos, p.pos); // exe 0x5e3bc0
                const int d = angleDiff(uint32_t(int32_t(p.rotation.y)), uint32_t(ang));
                if (uint32_t(d + kKeepFacingWindow) <= uint32_t(2 * kKeepFacingWindow)) return 0;
                return countDown();
            }
        }
    }
    entry.id = ticks;
    return 0;
}
// exe 0x510750(slot): the camera loaded; each tracked entry of the slot: alive -> its point into the scratch point
// (pools 0x20 / 0x60: the integer position; 0xa0 / 0xe0: their first two dwords) and the keep test (the formation's
// radius and ticks): despawn -> its pending free, the entry dropped, the alive count lowered; gone -> lost, the entry
// dropped, the alive count lowered (no defeated mark). The alive count.
uint8_t fieldTrack(EncounterSlot& slot) {
    EncounterState& e = encounterState();
    ProjectionScratch& s = projectionScratch();
    const ViewTransform& camera = cameraTransform().view;
    loadProjectionMatrix(camera);
    loadProjectionTranslation(camera);
    const Formation& f = fieldEncounterData().formationAt(e.formation);
    const uint16_t ticks = f.keepTicks;
    const int32_t rad = f.keepRadius;
    const uint32_t radius2 = uint32_t(rad) * uint32_t(rad);
    uint32_t entry = 0;
    for (size_t w = 0; w < e.trackedMasks.size(); ++w, entry = uint32_t(w) * 32) {
        uint32_t bit = 0x80000000u;
        for (uint32_t v = e.trackedMasks[w]; v != 0; v <<= 1, bit >>= 1, ++entry) {
            auto&& t = e.tracked[entry];
            if (int32_t(v) >= 0 || (t.kind & 3u) != slot.kind) continue;
            const uint8_t i = t.index;
            const uint32_t cls = t.kind & 0xf0;
            if (!knownEncounterPool(uint8_t(cls))) continue;
            if (trackedLive(cls, i)) {
                ActorHeader* a = nullptr;
                switch (cls) {
                case kAnimatedClass: {
                    AnimatedActor& b = animatedPool()[i];
                    s.point.x = b.pos.x.whole;
                    s.point.y = b.pos.y.whole;
                    s.point.z = b.pos.z.whole;
                    a = &b;
                    break;
                }
                case kStaticClass: {
                    StaticActor& b = staticPool()[i];
                    s.point.x = b.pos.x.whole;
                    s.point.y = b.pos.y.whole;
                    s.point.z = b.pos.z.whole;
                    a = &b;
                    break;
                }
                case kEffectClass: {
                    EffectActor& b = effectPool()[i];
                    s.point.setWords(b.positionWord(0), b.positionWord(1));
                    a = &b;
                    break;
                }
                default: {
                    ControllerActor& b = controllerPool()[i];
                    s.point.setWords(b.recordPointWord(0), b.recordPointWord(1));
                    a = &b;
                    break;
                }
                }
                if (fieldKeepTest(*a, t, radius2, ticks) == 0) continue;
                if (cls != kControllerClass) freeTracked(cls, i);
            } else {
                slot.lost = 1;
            }
            e.trackedMasks[w] ^= bit;
            --slot.alive;
        }
    }
    return slot.alive;
}
// exe 0x50ff90 (Roam): unless the field is suspended, each new cell of the player: a step (the walk stage up at its
// count), an RNG step, bit (r & 0x1f) of the odds of level + stage set -> the anchor (the player's point and rotation
// and move-direction yaw) kept, Waves; the cell kept
void fieldRoam(EncounterSlot& slot) {
    if (encounterState().control & kFieldSuspended) return;
    const Player& p = player();
    const int32_t cx = int32_t(p.pos.x.whole) >> 9, cz = int32_t(p.pos.z.whole) >> 9;
    if (int32_t(slot.cellX) != cx || int32_t(slot.cellZ) != cz) {
        const uint8_t steps = uint8_t(slot.steps + 1u);
        slot.steps = steps;
        if (steps == fieldStageCells[slot.stepIndex]) ++slot.stepIndex;
        const uint32_t odds = fieldOdds[uint32_t(slot.level) + slot.stepIndex];
        const uint32_t r = rngStep();
        if (int32_t(odds << (r & 0x1f)) < 0) {
            slot.anchor.x = p.pos.x.whole;
            slot.anchor.y = p.pos.y.whole;
            slot.anchor.z = p.pos.z.whole;
            slot.rollPitch = p.rotation.x;
            slot.rollYaw = p.rotation.y;
            slot.rollMoveYaw = p.directionOffset.y;
            slot.enterState(uint8_t(FieldState::Waves));
        }
    }
    slot.cellX = int8_t(cx);
    slot.cellZ = int8_t(cz);
}
// exe 0x5106e0 (LastWave): the anchor follows the player; the tracking (exe 0x510750) down to 0 -> armed again (Arm),
// the level up (nobody went down, below the top) or down (someone did, above 0), a calm battle music request
void fieldEnd(EncounterSlot& slot) {
    const Player& p = player();
    slot.anchor.x = p.pos.x.whole;
    slot.anchor.y = p.pos.y.whole;
    slot.anchor.z = p.pos.z.whole;
    if (fieldTrack(slot) != 0) return;
    const uint8_t lv = slot.level;
    slot.active = kEncounterSlotArmed;
    slot.enterState(uint8_t(FieldState::Arm));
    if (slot.lost == 0) {
        if (lv < kFieldTopLevel) slot.level = uint8_t(lv + 1u);
    } else if (lv != 0) {
        slot.level = uint8_t(lv - 1u);
    }
    encounterState().battleMusicRequest = kBattleMusicCalm;
}
// exe 0x50fe50: the field slot's state; then (unless the stage plays no music) the battle music: a request (its last
// one << 4 | it): from calm to fight -> on, the stage music cut; fight to calm -> off, the stage music back, the battle
// music faded out; on: the battle music asked again (exe 0x5d5260 answers 0: off)
void fieldSlot(EncounterSlot& slot) {
    EncounterState& e = encounterState();
    switch (FieldState(slot.state)) {
    case FieldState::Init: // exe 0x50ff20
        slot.enterState(uint8_t(FieldState::Arm));
        slot.level = 0;
        slot.stepIndex = 0;
        e.battleMusicLast = kBattleMusicCalm;
        e.battleMusicRequest = 0;
        e.battleMusicOn = 0;
        break;
    case FieldState::Arm: { // exe 0x50ff50
        const Player& p = player();
        slot.steps = 0;
        slot.stepIndex = 0;
        slot.pause = 0;
        slot.cellX = int8_t(int32_t(p.pos.x.whole) >> 9);
        slot.cellZ = int8_t(int32_t(p.pos.z.whole) >> 9);
        slot.enterState(uint8_t(FieldState::Roam));
        break;
    }
    case FieldState::Roam: fieldRoam(slot); break;
    case FieldState::Waves: // exe 0x510080
        switch (FieldWaveStep(slot.waveStep)) {
        case FieldWaveStep::Start: fieldStart(slot); break;
        case FieldWaveStep::Wave: fieldWave(slot); break;
        case FieldWaveStep::Pause: // exe 0x5106c0
            --slot.pause;
            if (slot.pause == 0) slot.waveStep = uint8_t(FieldWaveStep::Wave);
            break;
        default: notPortedAt(kEncounterPort, "field encounter wave step", slot.waveStep);
        }
        fieldTrack(slot);
        break;
    case FieldState::LastWave: fieldEnd(slot); break;
    default: notPortedAt(kEncounterPort, "field encounter state", slot.state);
    }
    SoundContext& sound = soundContext();
    if (sound.stageFlags & kSoundStageSilent) return;
    const uint8_t req = e.battleMusicRequest;
    if (req != 0) {
        switch ((uint32_t(e.battleMusicLast) << 4) | req) {
        case kBattleMusicStaysCalm: e.battleMusicOn = 0; break;
        case kBattleMusicFromCalm:
            e.battleMusicOn = 1;
            soundFadeDip(kStageMusicFade, kMusicCutStep, 0, 0);
            break;
        case kBattleMusicToCalm:
            e.battleMusicOn = 0;
            soundFadeRestore(kStageMusicFade, kMusicBackStep, 0);
            soundFadeOut(kBattleMusicFade, kMusicBackStep, 0);
            break;
        default: break;
        }
        e.battleMusicLast = req;
        e.battleMusicRequest = 0;
    }
    if (e.battleMusicOn != 0 && soundPlayFlatForced(sound.battleMusic) == 0) e.battleMusicOn = 0;
}

// ---- kind 2 and the frame's tail -----------------------------------------------------------------------------------

// exe 0x50fbd0: the script slot active; its alive ones counted (exe 0x50f780)
void scriptSlot(EncounterSlot& slot) {
    slot.active = kEncounterSlotRunning;
    if (slot.alive != 0) encounterCountAlive(slot);
}
// exe 0x50fbf0: each registered enemy: its entry's low half = the high bytes of its integer x / z (pools 0x20 / 0x60;
// slot ids 0x80.. are statics), its bit dropped once it is gone (the entry still refreshed). Another class reads
// address 0 in the exe.
void refreshEnemyCells() {
    EnemyCells& c = enemyCells();
    uint32_t bit = 0x80000000u;
    size_t k = 0;
    for (uint32_t v = c.mask; v != 0; v <<= 1, ++k, bit >>= 1) {
        if (int32_t(v) >= 0) continue;
        const uint32_t entry = c.entries[k];
        const uint32_t slot = entry >> 24;
        const Actor* a = nullptr;
        if ((slot & 0xe0) == kAnimatedClass) {
            const uint8_t i = uint8_t(slot - kAnimatedClass);
            if (!trackedLive(kAnimatedClass, i)) c.mask ^= bit;
            a = &animatedPool()[i];
        } else if ((slot & 0xe0) == kStaticClass || (slot & 0xe0) == 0x80) {
            const uint8_t i = uint8_t(slot - kStaticClass);
            if (!trackedLive(kStaticClass, i)) c.mask ^= bit;
            a = &staticPool()[i];
        } else {
            notPortedAt(kEncounterPort, "a registered enemy outside pools 0x20 / 0x60 (the exe reads address 0)", slot);
        }
        const uint32_t xHigh = uint16_t(a->pos.x.whole) >> 8, zHigh = uint16_t(a->pos.z.whole) >> 8;
        c.entries[k] = (entry & 0xffff0000u) | xHigh << 8 | zHigh;
    }
}

// exe 0x5e6670(slot, g): armed -> group g spawned, active, Fight; true
bool armedSpawn(EncounterSlot& slot, int32_t g) {
    if (slot.active != kEncounterSlotArmed) return false;
    slot.group = uint8_t(g);
    slot.alive = 0;
    slot.lost = 0;
    encounterSpawnGroup(slot, false, g);
    slot.active = kEncounterSlotRunning;
    slot.enterState(uint8_t(ZoneState::Fight));
    return true;
}

// exe 0x5e68d0(slot): the slot's tracked actors whose free is pending in their pool dropped
void dropFreedTracked(EncounterSlot& slot) {
    EncounterState& e = encounterState();
    ActorSystemState& pools = actorSystem();
    for (uint32_t w = 0; w < e.trackedMasks.size(); ++w) {
        auto&& mask = e.trackedMasks[w];
        uint32_t bit = 0x80000000u;
        uint32_t entry = w * 32;
        for (uint32_t m = mask; m != 0; m <<= 1, ++entry, bit >>= 1) {
            const TrackedActor& t = e.tracked[entry];
            if (int32_t(m) >= 0 || (t.kind & 3) != slot.kind) continue;
            const uint8_t k = t.index;
            int32_t pending;
            switch (t.kind & 0xf0) {
            case kAnimatedClass: pending = int32_t(uint32_t(pools.animatedFreed) << (k & 0x1f)); break;
            case kStaticClass: pending = int32_t(uint32_t(pools.staticsFreed[k >> 5]) << (k & 0x1f)); break;
            case kEffectClass: pending = int32_t(uint32_t(pools.effectsFreed[k >> 5]) << (k & 0x1f)); break;
            case kControllerClass: pending = int32_t(uint32_t(pools.controllersFreed) << (k & 0x1f)); break;
            default: continue;
            }
            if (pending < 0) {
                mask ^= bit;
                --slot.alive;
            }
        }
    }
}

} // namespace

// The typed load of a stage's field encounter table (TableId::StageEncounterField): every list the encounter routines
// can read, followed to its real extent. The pick list names at most 0x20 formations (exe 0x510c00: r & 0x1f); the
// highest pick bounds the formation array; a formation's mode >> 4 bounds the position kind, whose {base, count}
// bounds the sets (four per kind); a set's positions and a formation's classes end at a negative yaw / index. The class
// records are the 0xc-byte records fieldWave reads (exe 0x5102d0 / 0x50fd20), not a group record's 0x18.
FieldEncounterData FieldEncounterData::load(const TableReader& r) {
    constexpr const char* kArea = "field encounter typed load";
    constexpr uint32_t kMaxClassList = 0x80, kMaxPositionList = 0x80;
    FieldEncounterData t;
    t.classesAddress = r.u32(0);
    t.formationsAddress = r.u32(4);
    t.formationPickAddress = r.u32(8);
    t.positionSetsAddress = r.u32(0xc);
    t.positionRangesAddress = r.u32(0x10);
    const TableReader picks = r.follow(8, kPickCount);
    for (size_t i = 0; i < kPickCount; ++i) t.formationPick[i] = picks.u8(i);
    uint32_t maxPick = 0;
    for (const uint8_t p : t.formationPick)
        if (p > maxPick) maxPick = p;
    const TableReader forms = r.follow(4, (maxPick + 1) * kFormationSize);
    t.formations.resize(maxPick + 1);
    t.classLists.resize(maxPick + 1);
    uint32_t maxKind = 0;
    int32_t maxClass = -1;
    for (size_t i = 0; i <= maxPick; ++i) {
        t.formations[i] = Formation::load(forms.sub(i * kFormationSize, kFormationSize));
        maxKind = std::max<uint32_t>(maxKind, uint32_t(t.formations[i].mode) >> kFormationPositionShift);
        for (uint32_t n = 1;; ++n) {
            if (n > kMaxClassList) notPortedAt(kArea, "a class list with no terminator, formation", uint32_t(i));
            const TableReader list = forms.follow(i * kFormationSize, n);
            const int8_t c = list.s8(n - 1);
            t.classLists[i].push_back(c);
            if (c < 0) break;
            if (c > maxClass) maxClass = c;
        }
    }
    const size_t kinds = size_t(maxKind) + 1;
    const TableReader ranges = r.follow(0x10, uint32_t(kinds * 2));
    t.positionRanges.resize(kinds * 2);
    uint32_t maxSet = 0;
    for (size_t k = 0; k < kinds; ++k) {
        t.positionRanges[k * 2] = ranges.u8(k * 2);
        t.positionRanges[k * 2 + 1] = ranges.u8(k * 2 + 1);
        maxSet = std::max<uint32_t>(maxSet, uint32_t(t.positionRanges[k * 2]) + t.positionRanges[k * 2 + 1]);
    }
    const size_t setCount = size_t(maxSet) * kPositionSetsPerKind;
    t.positionSets.resize(setCount);
    t.positions.resize(setCount);
    if (setCount != 0) {
        const TableReader sets = r.follow(0xc, uint32_t(setCount * kPositionSize));
        for (size_t s = 0; s < setCount; ++s) {
            t.positionSets[s] = PositionSet::load(sets.sub(s * kPositionSize, kPositionSize));
            for (uint32_t n = 1;; ++n) {
                if (n > kMaxPositionList) notPortedAt(kArea, "a position list with no terminator, set", uint32_t(s));
                const TableReader list = sets.follow(s * kPositionSize, n * kPositionSize);
                const FormationPosition pos = FormationPosition::load(list.sub((n - 1) * kPositionSize, kPositionSize));
                t.positions[s].push_back(pos);
                if (pos.yaw < 0) break;
            }
        }
    }
    const size_t classCount = maxClass >= 0 ? size_t(maxClass) + 1 : 0;
    t.classes.resize(classCount);
    if (classCount != 0) {
        const TableReader records = r.follow(0, uint32_t(classCount * FieldClassRecord::kBytes));
        for (size_t i = 0; i < classCount; ++i)
            t.classes[i] = FieldClassRecord::load(records.sub(i * FieldClassRecord::kBytes, FieldClassRecord::kBytes));
    }
    return t;
}

void encounterSlotsSetup(uint32_t kinds) {
    EncounterState& e = encounterState();
    for (size_t k = 0; k < e.slots.size(); ++k) {
        auto&& s = e.slots[k];
        s.active = 0;
    }
    e.clearSlotOfKind();
    uint32_t bits = kinds & kEncounterKindMask, slot = 0;
    for (uint32_t i = 0; bits != 0; ++i, bits >>= 1) {
        if (!(bits & 1)) continue;
        auto&& s = e.slots[slot];
        e.slotOfKind[i] = int8_t(slot);
        s.active = kEncounterSlotArmed;
        s.kind = uint8_t(i);
        s.enterState(0);
        s.clearRoomWord();
        ++slot;
    }
    e.defeated[0] = 0;
    e.trackedMasks[0] = 0;
    e.defeated[1] = 0;
    e.defeated[2] = 0;
    e.defeated[3] = 0;
    e.attackLocks = 0;
    e.trackedMasks[1] = 0;
    enemyCells().mask = 0;
    e.control = kinds;
}

void encounterSlotsEnterRoom(uint8_t room) {
    EncounterState& e = encounterState();
    for (size_t k = 0; k < e.slots.size(); ++k) {
        auto&& s = e.slots[k];
        if (s.active != 0) s.room = room;
    }
    e.attackLocks = 0;
}

void encounterTick(const PlayerEnv& env) {
    (void)env;
    EncounterState& e = encounterState();
    if (scriptState().flags == 0 && eventState().active == 0) {
        for (size_t k = 0; k < e.slots.size(); ++k) {
            auto&& slot = e.slots[k];
            if (slot.active == 0) continue;
            switch (EncounterKind(slot.kind)) { // [0x89b62c + kind * 4]
            case EncounterKind::Zones: zoneSlot(slot); break;
            case EncounterKind::Field: fieldSlot(slot); break;
            case EncounterKind::Script: scriptSlot(slot); break;
            case EncounterKind::ZonesSecond: zoneWait(slot); break;
            default: notPortedAt(kEncounterPort, "encounter slot kind", slot.kind);
            }
        }
    }
    if (e.control & kEncounterRefreshCells) refreshEnemyCells();
    e.attackLocks = e.attackLocks << 16;
    if (const uint32_t groups = e.enemyGroups) {
        for (uint32_t at = groups; enemyGroupAt(at).tag() >= 0; at += kEnemyGroupEntrySize)
            patchedRecordFor<EnemyGroupEntry>(at).ageMarks();
    }
}

bool encounterSpawnGroupZoneSlot(int g) {
    EncounterState& e = encounterState();
    if (e.slotOfKind[uint32_t(EncounterKind::Zones)] < 0) return false;
    auto&& slot = e.slots[uint8_t(e.slotOfKind[uint32_t(EncounterKind::Zones)])];
    return armedSpawn(slot, g);
}

bool encounterSpawnGroupScriptSlot(int g) {
    EncounterState& e = encounterState();
    if (e.slotOfKind[uint32_t(EncounterKind::Script)] < 0) return false;
    auto&& slot = e.slots[uint8_t(e.slotOfKind[uint32_t(EncounterKind::Script)])];
    slot.group = uint8_t(g);
    slot.lost = 0;
    encounterSpawnGroup(slot, false, g);
    return true;
}

bool encounterRespawnGroup(EncounterSlot& slot, int g) {
    const uint8_t b = slot.active;
    if (!(b & kEncounterSlotArmed)) return false;
    if (b & kEncounterSlotActive) encounterDespawn(slot);
    slot.group = uint8_t(g);
    slot.alive = 0;
    slot.lost = 0;
    encounterSpawnGroup(slot, false, g);
    slot.active = kEncounterSlotRunning;
    slot.enterState(uint8_t(ZoneState::Fight));
    return true;
}

void encounterSpawnGroup(EncounterSlot& slot, bool yawFlip, int32_t g) {
    const DataArray<uint32_t> groups(encounterState().groupTables);
    for (uint32_t at = groups[size_t(uint32_t(g))]; dataRecord<EncounterRecord>(at, EncounterRecord::kBytes).flags != kZoneEnd; at += kGroupRecordSize) {
        const EncounterRecord r = dataRecord<EncounterRecord>(at, EncounterRecord::kBytes);
        if (enemyDefeated(r.id)) continue;
        const int16_t yaw = (yawFlip && (r.runFlags & kRecordYawFlip)) ? int16_t(kHalfTurn) : int16_t(0);
        const uint8_t pool = r.pool;
        if (!knownEncounterPool(pool)) continue;
        const uint32_t a = allocEncounterActor(pool);
        if (!a || !trackActor(slot.kind, actorHeaderAt(a), uint16_t(r.id))) continue;
        applyRecord(actorHeaderAt(a), r);
        if (pool == kAnimatedClass || pool == kStaticClass) {
            placeRecord(actorAt(a), r.point.x, r.point.y, r.point.z, r.yaw, yaw);
        } else if (pool == kEffectClass) {
            EffectActor& b = effectActorAt(a);
            b.pos.x = r.point.x;
            b.pos.y = r.point.y;
            b.pos.z = r.point.z;
        } else {
            controllerActorAt(a).setRecordPointShorts(r.point.x, r.point.y, r.point.z);
        }
        ++slot.alive;
    }
}

void encounterDespawn(EncounterSlot& slot) {
    EncounterState& e = encounterState();
    uint32_t entry = 0;
    for (size_t w = 0; w < e.trackedMasks.size(); ++w, entry = uint32_t(w) * 32) {
        uint32_t bit = 0x80000000u;
        for (uint32_t v = e.trackedMasks[w]; v != 0; v <<= 1, bit >>= 1, ++entry) {
            const TrackedActor& t = e.tracked[entry];
            if (int32_t(v) >= 0 || (t.kind & 0xfu) != slot.kind) continue;
            const uint8_t i = t.index;
            const uint32_t cls = t.kind & 0xf0;
            if (!knownEncounterPool(uint8_t(cls))) continue;
            if (trackedLive(cls, i)) freeTracked(cls, i);
            else enemyMarkDefeated(uint32_t(int32_t(int16_t(t.id))));
            e.trackedMasks[w] ^= bit;
        }
    }
}

uint8_t encounterCountAlive(EncounterSlot& slot) {
    EncounterState& e = encounterState();
    uint32_t entry = 0;
    for (size_t w = 0; w < e.trackedMasks.size(); ++w, entry = uint32_t(w) * 32) {
        uint32_t bit = 0x80000000u;
        for (uint32_t v = e.trackedMasks[w]; v != 0; v <<= 1, bit >>= 1, ++entry) {
            const TrackedActor& t = e.tracked[entry];
            if (int32_t(v) >= 0 || (t.kind & 3u) != slot.kind) continue;
            const uint32_t cls = t.kind & 0xf0;
            if (!knownEncounterPool(uint8_t(cls)) || trackedLive(cls, t.index)) continue;
            enemyMarkDefeated(uint32_t(int32_t(int16_t(t.id))));
            e.trackedMasks[w] ^= bit;
            slot.lost = 1;
            --slot.alive;
        }
    }
    return slot.alive;
}

void enemyMarkDefeated(uint32_t id) {
    if (id != 0) encounterState().defeated[id >> 5] |= 0x80000000u >> (id & 0x1f);
}

bool enemyDefeated(uint32_t id) {
    return ((0x80000000u >> (id & 0x1f)) & encounterState().defeated[id >> 5]) != 0;
}

int32_t encounterZoneGroup() {
    const EncounterState& e = encounterState();
    const int8_t i = e.slotOfKind[uint32_t(EncounterKind::Zones)];
    if (i < 0) return -1;
    const EncounterSlot& s = e.slots[uint8_t(i)];
    if (!(s.active & kEncounterSlotActive)) return -1;
    return int8_t(s.group);
}

int32_t encounterZoneAlive() {
    const EncounterState& e = encounterState();
    const int8_t i = e.slotOfKind[uint32_t(EncounterKind::Zones)];
    if (i < 0) return -1;
    const EncounterSlot& s = e.slots[uint8_t(i)];
    if (!(s.active & kEncounterSlotActive)) return 0;
    return s.alive;
}

bool encounterHasScriptSlot() { return encounterState().slotOfKind[uint32_t(EncounterKind::Script)] >= 0; }

bool encounterTrackInScriptSlot(const ActorHeader& actor, uint16_t id) {
    EncounterState& e = encounterState();
    const int8_t i = e.slotOfKind[uint32_t(EncounterKind::Script)];
    if (i < 0) return false;
    auto&& slot = e.slots[uint8_t(i)];
    const bool tracked = trackActor(slot.kind, actor, id);
    if (tracked) ++slot.alive;
    return tracked;
}

namespace {
// exe 0x422c40(record, untracked)
uint32_t spawnRecordActor(uint32_t record, bool tracked) {
    const uint32_t at = allocActor20();
    if (at == 0) return 0;
    AnimatedActor& a = animatedActorAt(at);
    const EncounterRecord r = dataRecord<EncounterRecord>(record, EncounterRecord::kBytes);
    a.loopFlags |= kActorSweepGroup2;
    a.flags = r.flags;
    a.setTypeWord(r.typeWord);
    a.setParamsWord(r.paramsWord);
    a.pos.x = int32_t(uint32_t(int32_t(r.point.x)) << 16);
    a.pos.y = int32_t(uint32_t(int32_t(r.point.y)) << 16);
    a.pos.z = int32_t(uint32_t(int32_t(r.point.z)) << 16);
    a.rotation.y = int16_t(r.yaw);
    if (tracked) encounterTrackInScriptSlot(a, uint16_t(r.id));
    return at;
}
} // namespace

uint32_t encounterSpawnRecord(uint32_t record) { return spawnRecordActor(record, true); }

uint32_t encounterSpawnRecordUntracked(uint32_t record) { return spawnRecordActor(record, false); }

bool encounterTrackInFieldSlotWhenOut(const ActorHeader& actor, uint16_t id) {
    EncounterState& e = encounterState();
    const int8_t i = e.slotOfKind[uint32_t(EncounterKind::Field)];
    if (i < 0) return false;
    auto&& slot = e.slots[uint8_t(i)];
    if (!(slot.active & kEncounterSlotActive)) return false;
    const bool tracked = trackActor(slot.kind, actor, id);
    if (tracked) ++slot.alive;
    return tracked;
}

int encounterUntrack(const ActorHeader& actor) {
    EncounterState& e = encounterState();
    const uint32_t cls = actorSlotClass(actor.slot);
    const uint32_t index = uint32_t(actor.slot) - cls;
    auto&& first = e.slots[0];
    size_t entry = 0;
    for (size_t w = 0; w < e.trackedMasks.size(); ++w) {
        uint32_t bits = e.trackedMasks[w];
        for (int k = 31; k >= 0; --k, ++entry, bits <<= 1) {
            if (int32_t(bits) >= 0) continue;
            const TrackedActor& t = e.tracked[entry];
            const uint32_t kind = uint32_t(int32_t(int8_t(t.kind)));
            if ((kind & 0xf0) != cls || uint32_t(t.index) != index || uint32_t(first.kind) != (kind & 3)) continue;
            e.trackedMasks[w] ^= 1u << k;
            first.alive -= 1;
            return -1;
        }
    }
    return 0;
}

int encounterFieldBearing(const Actor& actor, int16_t& pitch, int16_t& yaw) {
    EncounterState& e = encounterState();
    const int8_t i = e.slotOfKind[uint32_t(EncounterKind::Field)];
    if (i < 0) return 0;
    const EncounterSlot& slot = e.slots[uint8_t(i)];
    const int16_t roll = slot.rollPitch;
    const int32_t level = roll > 0 ? 0 : roll;
    const Player& p = player();
    const int32_t dz = int32_t(uint32_t(p.pos.z) - uint32_t(actor.pos.z));
    const int32_t dy = int32_t(uint32_t(p.pos.y) - uint32_t(actor.pos.y));
    pitch = int16_t(angleDiff(uint32_t(atanAngle(float(dy), float(dz))), uint32_t(level))); // exe 0x40b960, 0x5e3d80
    yaw = int16_t(angleDiff(uint32_t(angleOf(actor.pos, p.pos)), uint32_t(int32_t(slot.rollYaw))));
    return planarDistance(actor.pos, p.pos); // exe 0x5e3b00
}

void encounterPlaceAroundPlayer(Actor& actor, int distance, int pitch, int yaw) {
    static constexpr int32_t kNoTranslation[3] = {0, 0, 0};
    const Player& p = player();
    const int16_t look = p.rotation.x;
    const int16_t turnX = int16_t(look < 0 ? int(look) + pitch : pitch);
    const int16_t turnY = int16_t(uint16_t(p.rotation.y) + uint32_t(yaw));
    loadProjectionMatrix(rotationYXZ(turnX, turnY, 0), identityTablePad()); // exe 0x40bfd0, 0x40c3c0
    loadProjectionTranslation(kNoTranslation);                               // exe 0x40c3e0
    loadProjectionPoint(0, 0, int16_t(-distance), 0);                        // exe 0x40c6a0
    transformProjectionPoint();                                              // exe 0x40c800
    int32_t d[3];
    storeProjectionPoint(d); // exe 0x40caa0
    actor.pos.x = int32_t((uint32_t(d[0]) << 16) + uint32_t(p.pos.x));
    actor.pos.y = int32_t((uint32_t(d[1]) << 16) + uint32_t(p.pos.y));
    actor.pos.z = int32_t((uint32_t(d[2]) << 16) + uint32_t(p.pos.z));
}

namespace {
// exe 0x5e6450's unchecked slot read: the field kind's slot byte read unsigned (no -1 test) and the slot at 0xa6d100 +
// it * 0x20 read as it is. A byte past the three slots (0xff when there is no field slot) gives an address past them,
// inside the exe's .data (always mapped), read with the slot layout: the view of any byte 0..0xff
ShortVec3 fieldSlotAnchor(uint8_t index) {
    if (index < kEncounterSlots) return encounterState().slots[index].anchor;
    // exe 0x5e6450 reads the field kind's slot unchecked: the index of no field slot (slotOfKind -1, as a byte 0xff)
    // reads the slot layout's anchor (+4..+9) 0xff slots past the table -- animated slot 9's root matrix e[2..4]
    // (its +0x78..+0x7d); another index past the three slots has no reader traced (the port gap names it)
    if (index != 0xff) notPortedAt(kEncounterPort, "0x5e6450: a field slot index past the three slots", index);
    const Matrix3& m = animatedPool()[9].rootMatrix;
    return {m.e[2], m.e[3], m.e[4]};
}
} // namespace

int encounterFieldAnchorAngle(const Actor& actor) {
    EncounterState& e = encounterState();
    const ShortVec3 anchor = fieldSlotAnchor(uint8_t(e.slotOfKind[uint32_t(EncounterKind::Field)]));
    const int dx = int(actor.pos.x.whole) - anchor.x, dz = int(actor.pos.z.whole) - anchor.z;
    return atanAngle(float(dx), float(dz)) & kAngleMask;
}

void enemyClearDefeated(uint32_t id) {
    if (id == 0) return;
    encounterState().defeated[id >> 5] &= ~(0x80000000u >> (id & 0x1f));
}

void enemyGroupMark(int32_t index, uint16_t bits) {
    const uint32_t list = encounterState().enemyGroups;
    if (list == 0 || index < 0) return;
    patchedRecordFor<EnemyGroupEntry>(list + uint32_t(index) * kEnemyGroupEntrySize).marks |= bits;
}

int32_t enemyGroupFind(uint32_t cls, uint32_t type, uint32_t variant) {
    const uint32_t list = encounterState().enemyGroups;
    if (list == 0) return -1;
    int32_t i = 0;
    for (uint32_t e = list; enemyGroupAt(e).tag() >= 0; e += kEnemyGroupEntrySize, ++i)
        if (enemyGroupAt(e).isFor(cls, type, variant)) return i;
    return -1;
}

int32_t enemyGroupFindFor(const ActorHeader& actor, uint32_t variant) {
    if (encounterState().enemyGroups == 0) return -1;
    return enemyGroupFind(actorSlotClass(actor.slot), actor.type, variant);
}

uint32_t enemyGroupMarks(int32_t index, EnemyGroupMarks which) {
    const uint32_t list = encounterState().enemyGroups;
    if (list == 0) return 0xffffffffu;
    const EnemyGroupEntry g = enemyGroupAt(list + uint32_t(index) * kEnemyGroupEntrySize);
    switch (which) {
    case EnemyGroupMarks::Either: return uint32_t(g.marks | g.lastMarks);
    case EnemyGroupMarks::ThisTick: return g.marks;
    case EnemyGroupMarks::LastTick: return g.lastMarks;
    }
    return 0xffffffffu;
}

void encounterSuspend(uint32_t kinds, uint32_t resume) {
    const uint32_t bits = kinds << kEncounterSuspendShift;
    if (resume == 0) encounterState().control |= bits;
    else encounterState().control &= ~bits;
}

bool encounterInZone(const Fixed16Vec3& pos, uint32_t zone, uint32_t type) {
    return inZone(pos, dataRecord<EncounterZone>(zone, EncounterZone::kBytes), type);
}

int32_t encounterScriptSlotAlive() {
    const EncounterState& e = encounterState();
    const int8_t i = e.slotOfKind[uint32_t(EncounterKind::Script)];
    if (i < 0) return -1;
    return e.slots[uint8_t(i)].alive;
}

void encounterSlotsRearm() {
    EncounterState& e = encounterState();
    e.trackedMasks[0] = 0;
    e.trackedMasks[1] = 0;
    // the slot's state and its alive count (the zones' and the script groups' at their first state, the field's at Arm)
    auto rearm = [&](int8_t slot, uint8_t state) {
        auto&& s = e.slots[uint8_t(slot)];
        s.alive = 0;
        s.active = kEncounterSlotArmed;
        s.state = state;
    };
    const int8_t zones = e.slotOfKind[uint32_t(EncounterKind::Zones)],
                 field = e.slotOfKind[uint32_t(EncounterKind::Field)],
                 script = e.slotOfKind[uint32_t(EncounterKind::Script)];
    if (zones >= 0) rearm(zones, uint8_t(ZoneState::Wait));
    if (script >= 0) rearm(script, 0);
    if (field >= 0) {
        rearm(field, uint8_t(FieldState::Arm));
        e.battleMusicRequest = kBattleMusicCalm;
    }
}

void encounterDropFreedTracked() {
    EncounterState& e = encounterState();
    for (size_t i = 0; i < e.slots.size(); ++i) {
        auto&& slot = e.slots[i];
        if (slot.active != 0) dropFreedTracked(slot);
    }
}

bool encounterStageLayout(int stage) {
    if (stage < 0 || stage >= kStages || kEncStages[stage].root < 0) return false;
    int i = kEncStages[stage].root;
    for (int guard = 0; guard < kLayoutTreeLimit; ++guard) {
        const EncLayoutOp& o = kEncLayoutOps[i];
        switch (o.op) {
        case LayoutOp::End: return true;
        case LayoutOp::ClearFlag: clearStoryFlag(o.flag); i = o.next[0]; break;
        case LayoutOp::SetFlag: setStoryFlag(o.flag); i = o.next[0]; break;
        case LayoutOp::TestFlag: i = o.next[storyFlag(o.flag) ? 1 : 0]; break;
        case LayoutOp::RngStep: i = o.next[rngStep() & 3]; break;
        case LayoutOp::Write: {
            EncounterState& e = encounterState();
            const uint32_t table = dataSource().stateAddress({TableId::EncounterLayoutTable, uint32_t(i)});
            if (o.global == StageGlobal::EncounterZoneTables) e.zoneTables = table;
            else if (o.global == StageGlobal::EncounterGroupTables) e.groupTables = table;
            else notPortedAt(kEncounterPort, "encounter layout write of another global", uint32_t(o.global));
            i = o.next[0];
            break;
        }
        default: notPortedAt(kEncounterPort, "encounter layout op", uint32_t(o.op));
        }
        if (i < 0) notPortedAt(kEncounterPort, "encounter layout tree (missing branch), stage", uint32_t(stage));
    }
    notPortedAt(kEncounterPort, "encounter layout tree (no end), stage", uint32_t(stage));
}

std::span<const EncounterRest> encounterStageRest(int stage) {
    const size_t n = sizeof kEncStageRest / sizeof kEncStageRest[0];
    size_t a = 0;
    while (a < n && kEncStageRest[a].stage != stage) ++a;
    size_t b = a;
    while (b < n && kEncStageRest[b].stage == stage) ++b;
    return {kEncStageRest + a, b - a};
}

} // namespace od::port
