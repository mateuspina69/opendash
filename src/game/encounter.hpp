// Enemy encounters: the three encounter slots the stage load arms (exe 0x50f280), one per kind the stage asks for --
// zone-triggered spawn groups (the player enters a trigger zone of the room: its group spawns), random field
// encounters (the player walks the field: waves of a formation around him) and script-spawned groups -- their
// per-frame tick (exe 0x50f350), the enemies they track (alive, gone, defeated) and the defeated set, the group spawns
// the classes and scripts ask for, and the stage loads that draw their layout with the RNG. The RNG (game/random) is
// stepped exactly where the exe steps it.
#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

#include "engine/routine_ids.hpp"
#include "game/loaded_data.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_state.hpp"
#include "game/enemy_neighbours.hpp"
#include "game/state_array.hpp"
#include "game/vectors.hpp"
#include "game/stage_globals.hpp"
#include "game/patched_data.hpp"
#include "game/port_gap.hpp"

namespace od::port {

// ---- the slots ------------------------------------------------------------------------------------------------------

// +1 of a slot: its kind (the bit of the stage's kinds it was armed for; its handler [0x89b62c + kind * 4])
enum class EncounterKind : uint8_t {
    Zones = 0,       // zone-triggered groups
    Field = 1,       // random field encounters
    Script = 2,      // groups spawned by scripts and classes
    ZonesSecond = 3, // the table's 4th handler is the zones' wait state (no stage asks for this kind)
};
constexpr uint32_t kEncounterKinds = 4;
// +2 of a zone slot
enum class ZoneState : uint8_t {
    Wait = 0,    // until the player enters a trigger zone of the room
    Respawn = 1, // the slot's group spawned again
    Fight = 2,   // until the group is down or the player takes an exit zone
    Clear = 3,   // the group despawned, back to Wait
};
// +2 of a field slot
enum class FieldState : uint8_t {
    Init = 0,     // the level and the battle music reset
    Arm = 1,      // the walk's count restarted at the player's cell
    Roam = 2,     // each new cell may roll an encounter
    Waves = 3,    // the formation's waves (FieldWaveStep)
    LastWave = 4, // the last wave out: until its enemies are down or gone
};
// +3 of a field slot in FieldState::Waves
enum class FieldWaveStep : uint8_t {
    Start = 0, // the formation picked and placed
    Wave = 1,  // the next wave spawned
    Pause = 2, // the pause between two waves
};

constexpr uint32_t kEncounterSlotTable = 0xa6d100, kEncounterSlotSize = 0x20;
constexpr size_t kEncounterSlots = 3;
// One encounter slot (the exe's 0x20 bytes, kEncounterSlots of them)
struct EncounterSlot {
    uint8_t active = 0;    // 0 free, kEncounterSlotArmed, kEncounterSlotRunning
    uint8_t kind = 0;      // EncounterKind
    uint8_t state = 0;     // ZoneState / FieldState
    uint8_t waveStep = 0;  // FieldWaveStep
    ShortVec3 anchor;      // the field encounter's anchor (the player's point at the roll, then the formation's)
    int16_t unread0a = 0;  // (no reader)
    int16_t rollPitch = 0; // the player's rotation x / yaw and move-direction yaw at the roll
    int16_t rollYaw = 0;
    int16_t unread10 = 0;  // (no reader)
    int16_t rollMoveYaw = 0;
    uint8_t room = 0;      // the sub-room it serves (exe 0x50f320)
    uint8_t group = 0;     // the group spawned (s8)
    uint8_t alive = 0;     // the tracked actors still alive
    uint8_t lost = 0;      // a tracked actor went down
    int8_t cellX = 0;      // the field's last cell of the player (>> 9)
    int8_t cellZ = 0;
    uint16_t pause = 0;    // the pause between two waves (ticks; the wave's roll)
    uint8_t steps = 0;     // the field's cells walked since the last encounter
    uint8_t level = 0;     // the field's encounter level (0..kFieldTopLevel)
    uint8_t stepIndex = 0; // the field's walk stage (the odds rise with it)
    uint8_t unread1f = 0;  // (no reader)

    // the state s, its wave step cleared (the exe's word store at +2)
    void enterState(uint8_t s) {
        state = s;
        waveStep = 0;
    }
    // the sub-room, the group, the alive count and the lost byte cleared (the exe's dword store at +0x14)
    void clearRoomWord() {
        room = 0;
        group = 0;
        alive = 0;
        lost = 0;
    }
};
constexpr uint8_t kEncounterSlotArmed = 0x01;   // in use (armed: waits for its trigger)
constexpr uint8_t kEncounterSlotActive = 0x80;  // its group is out
constexpr uint8_t kEncounterSlotRunning = kEncounterSlotArmed | kEncounterSlotActive;
constexpr uint8_t kFieldTopLevel = 6;

// One tracked actor (64 of them, in use by the masks: bit 31 - entry of each word)
struct TrackedActor {
    uint8_t kind = 0;  // the actor's slot class (& 0xf0) | the encounter slot's kind (& 3; a field formation's kind
                       // bits & 0xc)
    uint8_t index = 0; // its index in its pool
    uint16_t id = 0;   // its enemy id
};
constexpr uint32_t kTrackedActors = 64;

// A stage enemy-group entry (8 bytes of the table *0xa7258c; a negative first short ends it): the class / type /
// variant it is for (the classes look their entry up, exe 0x5e6d00), its marks the classes set this tick (exe 0x5e6d40)
// and the last tick's (the tick moves them over)
// The table is exe data the encounter tick and the classes write (the marks): its entries are patched records
// (game/patched_data), read through enemyGroupAt.
struct EnemyGroupEntry : DataRecord {
    static constexpr uint32_t kBytes = 8;
    uint32_t key = 0;       // +0x0: class | type << 16 | variant << 24 (its low short, the tag: the slot class of the
                            // actors it is for; a negative one ends the table)
    uint16_t lastMarks = 0; // +0x4
    uint16_t marks = 0;     // +0x6

    int16_t tag() const { return int16_t(uint16_t(key)); }
    // the entry is the one of slot class cls, type and variant (the exe's dword compare of the key at +0)
    bool isFor(uint32_t cls, uint32_t type, uint32_t variant) const { return key == ((((variant << 8) | type) << 16) | cls); }
    // this tick's marks moved to the last tick's, this tick's cleared (the tick's dword shift)
    void ageMarks() {
        lastMarks = marks;
        marks = 0;
    }
    template <class R> static EnemyGroupEntry load(const R& r) { return {{}, r.u32(0), r.u16(4), r.u16(6)}; }
    void save(std::span<uint8_t> out) const {
        storeLe(out, 0, key);
        storeLe(out, 4, lastMarks);
        storeLe(out, 6, marks);
    }
};
// the entry at a pointer of the table
inline EnemyGroupEntry enemyGroupAt(uint32_t at) { return recordAt<EnemyGroupEntry>(at); }
// the marks exe 0x5e6d60 reads of an entry
enum class EnemyGroupMarks : uint8_t {
    Either = 0,   // this tick's | the last tick's
    ThisTick = 1,
    LastTick = 2,
};
constexpr uint32_t kEnemyGroupEntrySize = 8;

// The stage's field encounter table (*fieldTable)
struct FieldEncounterTable : DataRecord {
    static constexpr uint32_t kBytes = 0x14;
    uint32_t classes = 0; // +0x0: the class records (kClassRecordSize each)
    uint32_t formations = 0; // +0x4: the formations (kFormationSize each)
    uint32_t formationPick = 0; // +0x8: u8[32]: the formation of an RNG roll & 0x1f
    uint32_t positionSets = 0; // +0xc: the position sets (8 bytes each: PositionSet)
    uint32_t positionRanges = 0; // +0x10: u8 pairs {base, count} of the position sets by formation kind
    template <class R> static FieldEncounterTable load(const R& r) {
        FieldEncounterTable v;
        v.classes = r.template read<uint32_t>(0x0);
        v.formations = r.template read<uint32_t>(0x4);
        v.formationPick = r.template read<uint32_t>(0x8);
        v.positionSets = r.template read<uint32_t>(0xc);
        v.positionRanges = r.template read<uint32_t>(0x10);
        return v;
    }
    // the table in the exe's layout (a stage's spawner rewrites its formation picks: game/patched_data)
    void save(std::span<uint8_t> out) const {
        storeLe(out, 0, classes);
        storeLe(out, 4, formations);
        storeLe(out, 8, formationPick);
        storeLe(out, 0xc, positionSets);
        storeLe(out, 0x10, positionRanges);
    }
};

// ---- the field encounter table's lists -------------------------------------------------------------------------------
// A formation (0xc bytes)
struct Formation : DataRecord {
    static constexpr uint32_t kBytes = 0xc;
    uint32_t classes = 0; // +0x0: its class list: s8 indices of the class records (negative ends it)
    uint16_t mode = 0; // +0x4: kFormationNoPause, the turn (& kFormationTurnMask), the tracked kind bits
    int16_t distance = 0; // +0x6: its offset ahead of the anchor
    int16_t keepRadius = 0; // +0x8: its enemies are kept within this of the player
    uint16_t keepTicks = 0; // +0xa: ... and this long out of sight; also their enemy id
    template <class R> static Formation load(const R& r) {
        Formation v;
        v.classes = r.template read<uint32_t>(0x0);
        v.mode = r.template read<uint16_t>(0x4);
        v.distance = r.template read<int16_t>(0x6);
        v.keepRadius = r.template read<int16_t>(0x8);
        v.keepTicks = r.template read<uint16_t>(0xa);
        return v;
    }
};
constexpr uint32_t kFormationSize = 0xc;
constexpr uint16_t kFormationNoPause = 0x1;  // no pause roll between two positions
constexpr uint16_t kFormationTurnMask = 0x3; // 0: turned by the roll's yaw, 1: by the player's rotation, else none
constexpr uint16_t kFormationTurnRoll = 0, kFormationTurnPlayer = 1;
constexpr uint16_t kFormationKindBits = 0xc; // ORed into the tracked kind (the keep test's mode)
constexpr uint32_t kFormationPositionShift = 4;
// A position set (8 bytes)
struct PositionSet : DataRecord {
    static constexpr uint32_t kBytes = 0x6;
    uint32_t positions = 0; // +0x0: FormationPosition list
    uint8_t waveOddsRow = 0; // +0x4
    uint8_t unreadByte = 0; // +0x5: copied out (its reader is not traced)
    template <class R> static PositionSet load(const R& r) {
        PositionSet v;
        v.positions = r.template read<uint32_t>(0x0);
        v.waveOddsRow = r.template read<uint8_t>(0x4);
        v.unreadByte = r.template read<uint8_t>(0x5);
        return v;
    }
};
// A position of a set (8 bytes): the offset from the anchor and the yaw (a negative yaw ends the set)
struct FormationPosition : DataRecord {
    static constexpr uint32_t kBytes = 0x8;
    ShortVec4 point; // +0x0
    int16_t yaw = 0; // +0x6
    template <class R> static FormationPosition load(const R& r) {
        FormationPosition v;
        v.point = {r.template read<int16_t>(0x0), r.template read<int16_t>(0x2), r.template read<int16_t>(0x4), r.template read<int16_t>(0x6)};
        v.yaw = r.template read<int16_t>(0x6);
        return v;
    }
};
constexpr uint32_t kPositionSize = 8;
constexpr uint32_t kPositionSetsPerKind = 4; // a kind's sets: one per quarter turn of the roll's facing
// A field encounter's class record (0xc bytes): the first 0xc bytes of a group record (EncounterRecord, exe 0x50fd20).
// fieldWave reads only these (exe 0x5102d0 reads the class list's index * 0xc; applyRecord reads +0..+0xb).
struct FieldClassRecord : DataRecord {
    static constexpr uint32_t kBytes = 0xc;
    uint8_t flags = 0; // +0x0: the actor's +0
    uint8_t runFlags = 0; // +0x1: ORed into the actor's +1 (kRecordYawFlip: may face the other way)
    uint8_t pool = 0; // +0x2: 0x20 / 0x60 / 0xa0 / 0xe0 (any other: skipped)
    uint8_t loopFlags = 0; // +0x3: ORed into the actor's +3
    uint32_t typeWord = 0; // +0x4
    uint32_t paramsWord = 0; // +0x8
    template <class R> static FieldClassRecord load(const R& r) {
        FieldClassRecord v;
        v.flags = r.template read<uint8_t>(0x0);
        v.runFlags = r.template read<uint8_t>(0x1);
        v.pool = r.template read<uint8_t>(0x2);
        v.loopFlags = r.template read<uint8_t>(0x3);
        v.typeWord = r.template read<uint32_t>(0x4);
        v.paramsWord = r.template read<uint32_t>(0x8);
        return v;
    }
};

// The stage's field encounter table through its typed load (TableId::StageEncounterField): the table's five pointers
// (the addresses the exe keeps in EncounterState::formation / classCursor / positionCursor) and every list they lead
// to, followed to the extents the encounter code reads -- the picks; the formations the picks can name; each
// formation's class list to its negative terminator; the position ranges of the kinds the formations ask for; the
// position sets those ranges cover and each set's positions to their negative terminator; the class records the lists
// name (the exe's real 0xc-byte extent, not the 0x18 of a group record).
struct FieldEncounterData : DataRecord {
    static constexpr uint32_t kBytes = FieldEncounterTable::kBytes;
    static constexpr uint32_t kPickCount = 0x20;
    uint32_t classesAddress = 0; // +0x0
    uint32_t formationsAddress = 0; // +0x4
    uint32_t formationPickAddress = 0; // +0x8
    uint32_t positionSetsAddress = 0; // +0xc
    uint32_t positionRangesAddress = 0; // +0x10
    std::array<uint8_t, kPickCount> formationPick{}; // the roll & 0x1f names a formation (an index)
    std::vector<Formation> formations; // max pick index + 1
    std::vector<std::vector<int8_t>> classLists; // per formation: its indices, the negative terminator included
    std::vector<FieldClassRecord> classes; // max class index + 1
    std::vector<uint8_t> positionRanges; // {base, count} pairs, kinds 0..max (formation mode >> 4)
    std::vector<PositionSet> positionSets; // (max base + count) * kPositionSetsPerKind
    std::vector<std::vector<FormationPosition>> positions; // per set: its positions, the negative-yaw terminator included

    static FieldEncounterData load(const TableReader& r);

    // the formation a pick names
    const Formation& formation(size_t pick) const {
        if (pick >= formations.size()) tableIndexError(pick, formations.size());
        return formations[pick];
    }
    // the index of the formation at an exe address (EncounterState::formation; a picked / kept address)
    size_t formationIndex(uint32_t address) const {
        const uint32_t off = address - formationsAddress;
        const size_t i = size_t(off / kFormationSize);
        if (off % kFormationSize != 0 || i >= formations.size()) tableIndexError(i, formations.size());
        return i;
    }
    // the formation at an exe address
    const Formation& formationAt(uint32_t address) const { return formations[formationIndex(address)]; }
    // the position set at an exe address (pickFieldFormation's return)
    const PositionSet& positionSetAt(uint32_t address) const {
        const uint32_t off = address - positionSetsAddress;
        const size_t i = size_t(off / kPositionSize);
        if (off % kPositionSize != 0 || i >= positionSets.size()) tableIndexError(i, positionSets.size());
        return positionSets[i];
    }
    // the position at an exe address (EncounterState::positionCursor: a set's list entry)
    const FormationPosition& positionAt(uint32_t address) const {
        for (size_t s = 0; s < positionSets.size(); ++s) {
            const uint32_t off = address - positionSets[s].positions;
            const size_t i = size_t(off / kPositionSize);
            if (off % kPositionSize != 0 || i >= positions[s].size()) continue;
            return positions[s][i];
        }
        notPortedAt("field encounter typed load", "a position outside the sets' lists", address);
    }
    // the {base, count} of a position kind (a formation's mode >> 4)
    std::pair<uint8_t, uint8_t> positionRange(size_t kind) const {
        if (kind * 2 + 1 >= positionRanges.size()) tableIndexError(kind, positionRanges.size() / 2);
        return {positionRanges[kind * 2], positionRanges[kind * 2 + 1]};
    }
    // a class record a class list names (the caller stops at the list's negative terminator)
    const FieldClassRecord& classRecord(int8_t index) const {
        if (index < 0 || size_t(index) >= classes.size()) tableIndexError(size_t(index), classes.size());
        return classes[size_t(index)];
    }
};

struct EncounterState {
    StateArray<EncounterSlot, kEncounterSlots> slots{};
    StateArray<int8_t, kEncounterKinds> slotOfKind{}; // the slot of each kind (-1: none)
    StateArray<uint32_t, 2> trackedMasks{};           // the tracked entries in use (bit 31 - entry)
    StateArray<TrackedActor, kTrackedActors> tracked{};
    // the enemy ids defeated (bit 31 - (id & 0x1f) of word id >> 5; the stage load clears 4 words: ids 0..0x7f)
    StateArray<uint32_t, 4> defeated{};
    // the enemies' attack locks: byte 0 this tick's (1 a lunge, 2 an attack runs), byte 2 the last tick's (the tick
    // shifts them up 16)
    uint32_t attackLocks = 0;
    // the enemies left of a scripted wave (the room scripts set it, the wave's enemies count it down; the exe stores it
    // as a dword or a word: waveEnemiesWord / setWaveEnemiesWord)
    uint32_t waveEnemies = 0;
    // the stage's kinds (& 0xf) | kEncounterRefreshCells | the suspended kinds
    uint32_t control = 0;
    uint32_t zoneTables = 0;  // the stage's zone tables (by sub-room) ... (the address: the record's identity)
    uint32_t groupTables = 0; // ... and group tables (by group)
    uint32_t fieldTable = 0;  // the stage's field encounter table (FieldEncounterTable)
    uint32_t enemyGroups = 0; // the stage's enemy-group table (EnemyGroupEntry; 0: none)
    // the field encounter in progress: its formation, the next position and the next class index
    uint32_t formation = 0;
    uint32_t positionCursor = 0;
    uint32_t classCursor = 0;
    uint8_t waveOddsRow = 0;     // the position set's row of the wave-end odds
    uint8_t positionSetByte = 0; // the position set's +5 (its reader is not traced)
    // the battle music: the request (old << 4 | new decides: kBattleMusic*), the last one, on
    uint8_t battleMusicLast = 0;
    uint8_t battleMusicRequest = 0;
    uint8_t battleMusicOn = 0;

    // the slot of each kind cleared to -1 (the exe's dword store)
    void clearSlotOfKind() { slotOfKind.fill(-1); }
    // this tick's attack lock bits set (kAttackLock*: the classes' byte read-modify-write of byte 0)
    void markAttackLock(uint8_t bits) { attackLocks |= bits; }
    // the attack lock bits of this tick or the last (the classes' byte reads of bytes 0 and 2)
    uint8_t attackLocksRecent() const { return uint8_t(uint8_t(attackLocks >> 16) | uint8_t(attackLocks)); }
    // ... as the words of this tick and the last ORed (a class waiting on any bit of them: the exe's two word reads)
    uint32_t attackLockWordsRecent() const { return (attackLocks & 0xffff) | (attackLocks >> 16); }
    // the wave's count as the exe's word accesses see it (its low half)
    uint16_t waveEnemiesWord() const { return uint16_t(waveEnemies); }
    void setWaveEnemiesWord(uint16_t v) { waveEnemies = (waveEnemies & 0xffff0000u) | v; }
};
inline EncounterState& encounterState() {
    static EncounterState s;
    return s;
}
// EncounterState::attackLocks (bytes 0 / 2)
constexpr uint8_t kAttackLockLunge = 0x01;  // a lunge runs
constexpr uint8_t kAttackLockAttack = 0x02; // an attack runs (the charges, the bursts)
// EncounterState::control
constexpr uint32_t kEncounterKindMask = 0xf;
constexpr uint32_t kEncounterRefreshCells = 0x10;    // the tick refreshes the registered enemies' cells
constexpr uint32_t kEncounterSuspendShift = 8;       // bit 8 + kind: that kind suspended (exe 0x5e6870)
constexpr uint32_t kZonesSuspended = 1u << (kEncounterSuspendShift + uint32_t(EncounterKind::Zones));
constexpr uint32_t kFieldSuspended = 1u << (kEncounterSuspendShift + uint32_t(EncounterKind::Field));
// battle music requests (EncounterState::battleMusicRequest)
constexpr uint8_t kBattleMusicCalm = 1;
constexpr uint8_t kBattleMusicFight = 2;

// ---- the stage / room load -----------------------------------------------------------------------------------------
// exe 0x50f280(kinds): the slots armed for the stage's kinds (one per set bit of kinds & 0xf, in bit order), the
// defeated set, the tracked masks, the attack locks and the registered cells cleared, the control word = kinds
void encounterSlotsSetup(uint32_t kinds);
// exe 0x50f320(room): the slots in use serve the sub-room; the attack locks cleared
void encounterSlotsEnterRoom(uint8_t room);
// exe 0x5e67f0: the tracked entries dropped, the slot of each kind armed again (alive 0; the field slot at its arm
// state with a calm battle music request); also ST48's room function (after exe 0x50f320)
void encounterSlotsRearm();
// exe 0x5e68a0: for each slot in use, its tracked actors whose pool has their free pending dropped (exe 0x5e68d0: the
// entry's bit off, the slot's alive count - 1)
void encounterDropFreedTracked();

// ---- the frame ------------------------------------------------------------------------------------------------------
// exe 0x50f350: unless a script or an event runs, each slot in use runs its kind's handler; then the registered cells
// refreshed (kEncounterRefreshCells), the attack locks aged, the enemy groups' counters aged. env is not read (the exe
// reads the room grid directly; the port's tick signature keeps it).
void encounterTick(const PlayerEnv& env);

// ---- the spawns the classes and scripts ask for ---------------------------------------------------------------------
// exe 0x5e6640(g): the zone slot spawns group g if it is armed (then active, Fight). false without a zone slot or when
// it is busy
bool encounterSpawnGroupZoneSlot(int g);
// exe 0x5e66b0(g): the script slot spawns group g (its state kept, its alive count added to). false without one
bool encounterSpawnGroupScriptSlot(int g);
// exe 0x5e66f0(slot, g): a slot in use despawned if active, then group g spawned, active, Fight. false when it is free
bool encounterRespawnGroup(EncounterSlot& slot, int g);
// exe 0x50f480(slot, yawFlip, g): the records of group g not defeated spawned and tracked under the slot's kind (the
// yaw turned by a half turn when yawFlip and the record allows); the slot's alive count adds the spawned ones
void encounterSpawnGroup(EncounterSlot& slot, bool yawFlip, int32_t g);
// exe 0x50f980(slot): the slot's tracked actors freed (alive) or marked defeated (gone), their entries dropped
void encounterDespawn(EncounterSlot& slot);
// exe 0x50f780(slot): the slot's tracked actors that are gone marked defeated, their entries dropped, the slot's lost
// byte set and its alive count lowered; the alive count
uint8_t encounterCountAlive(EncounterSlot& slot);
// exe 0x5e63c0(id): the enemy id defeated (id 0: none)
void enemyMarkDefeated(uint32_t id);
// exe 0x5e6420(id): the enemy id is defeated
bool enemyDefeated(uint32_t id);
// exe 0x5e67d0: the script slot's alive count, -1 without a script slot
int32_t encounterScriptSlotAlive();
// exe 0x5e6770: the zone slot's group while it is active, else -1 (also without a zone slot)
int32_t encounterZoneGroup();
// exe 0x5e67a0: the zone slot's alive count while it is active, 0 when not, -1 without a zone slot
int32_t encounterZoneAlive();
// exe 0x5e6500's first test: the encounter has a script slot (without one, exe 0x5e6500 returns before reading the
// actor)
bool encounterHasScriptSlot();
// exe 0x5e6500(actor, id): the actor tracked under the script slot's kind, the slot's alive count + 1 (false: no script
// slot / no free entry)
bool encounterTrackInScriptSlot(const ActorHeader& actor, uint16_t id);
// exe 0x41dce0(record) (exe 0x422c40(record, 0)): one group record (0x18 bytes of exe data) spawned as a pool-0x20
// actor of the stage's table -- kActorSweepGroup2, the record's flags, type and parameter words, the 16.16 position of
// its point, its yaw -- and tracked under the script slot with the record's id; the actor's address, 0 when the pool is
// full
uint32_t encounterSpawnRecord(uint32_t record);
// exe 0x422c40(record, 1): the same, not tracked
uint32_t encounterSpawnRecordUntracked(uint32_t record);
// exe 0x5e64b0(actor, id) as content/manda/manda_ruin_burrower's transcription has it: the actor tracked under the FIELD kind's
// slot while that slot is out (its alive count + 1); false without such a slot, the slot not out or no free entry. The exe reads the zones' slot (0xa67844), the transcription the field's (0xa67845): kept
bool encounterTrackInFieldSlotWhenOut(const ActorHeader& actor, uint16_t id);
// exe 0x5e6540(actor): the actor's tracked entry of the first slot's kind dropped (the first slot's alive count - 1);
// -1 when found, else 0
int encounterUntrack(const ActorHeader& actor);
// exe 0x5e6040(actor, pitch, yaw): the actor's bearing in the field encounter -- the pitch of the player from it (the
// 16.16 differences) against the roll's pitch (at most level), the yaw of the player from it against the roll's yaw
// -- and the planar distance; without a field slot 0 (the outputs untouched)
int encounterFieldBearing(const Actor& actor, int16_t& pitch, int16_t& yaw);
// exe 0x5e6140(actor, distance, pitch, yaw): the actor put `distance` from the player, turned by the player's yaw + yaw
// and pitch (+ the player's pitch when it looks up), through the projection registers' single point
void encounterPlaceAroundPlayer(Actor& actor, int distance, int pitch, int yaw);
// exe 0x5e6450(actor, local): the angle of the actor seen from the field slot's anchor (the slot unchecked, as the exe)
int encounterFieldAnchorAngle(const Actor& actor);
// exe 0x5e63f0(id): the enemy id no longer defeated (id 0: none)
void enemyClearDefeated(uint32_t id);
// exe 0x5e6d40(index, bits): the stage enemy-group entry's this-tick marks |= bits (no table / index < 0: nothing)
void enemyGroupMark(int32_t index, uint16_t bits);
// exe 0x5e6d00(class, type, variant): the index of the stage enemy-group entry for the slot class, type and variant;
// -1 without a table or when none is
int32_t enemyGroupFind(uint32_t cls, uint32_t type, uint32_t variant);
// exe 0x5e6cd0(actor, variant): ... for the actor's slot class and type
int32_t enemyGroupFindFor(const ActorHeader& actor, uint32_t variant);
// exe 0x5e6d60(index, which): the marks of entry `index` (unchecked, as the exe: a -1 reads the 8 bytes before the
// table); -1 without a table
uint32_t enemyGroupMarks(int32_t index, EnemyGroupMarks which);
// exe 0x5e6870(kinds, resume): the encounter kinds' bits (1 << kind) suspended (resume 0) or resumed
void encounterSuspend(uint32_t kinds, uint32_t resume);
// exe 0x50fcc0(actor, zone, type): the zone record at `zone` (exe data) is of `type` (0 a trigger, 1 an exit) and the
// position's cell lies in its box
bool encounterInZone(const Fixed16Vec3& pos, uint32_t zone, uint32_t type);

// ---- the stage loads that draw their layout -------------------------------------------------------------------------
// The layout part of the four stage setups that pick their encounter layout with the RNG at stage load: the story flag
// calls, the RNG steps and the zone / group table writes, in the exe's order. false (nothing done) for the other
// stages.
bool encounterStageLayout(int stage);
// The other effects of a layout stage's setup, in the exe's order (game/stage_setup runs them around the layout)
enum class EncounterRestKind : uint8_t {
    Write = 0, // the global = the stage's table (TableId::Stage*; a callback written: `routine`)
    Call = 1,  // `routine`(a0, a1)
};
struct EncounterRest {
    uint8_t stage;
    EncounterRestKind kind;
    StageGlobal global; // a write's global (None: a call)
    uint32_t a0, a1;    // a call's arguments
    RoutineId routine = RoutineId::None;
};
std::span<const EncounterRest> encounterStageRest(int stage);
// the routine of a layout stage's setup that arms its encounter slots (exe 0x50f280; its a0: the kinds)
constexpr RoutineId kEncounterSlotsSetupRoutine = RoutineId::EncounterSlotsSetup;

} // namespace od::port
