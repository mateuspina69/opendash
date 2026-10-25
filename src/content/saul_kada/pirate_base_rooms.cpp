// The room handlers of the pirate base: ST3B (case 0x587c70, table 0x8cb7a0) and ST3C (case 0x5885b0, table 0x8cbd7c)
// in story 12 (the attack on the base, the gold statue) and ST3C's story-13 rooms (the race). One case per exe handler
// (its address in the case); each keeps its step in the room handler's byte 0.
#include "content/saul_kada/pirate_base_rooms.hpp"

#include "content/saul_kada/mission_rooms.hpp"

#include "game/loaded_data.hpp"
#include "game/breakable_walls.hpp"
#include "game/distances.hpp"
#include "game/encounter.hpp"
#include "game/game_state.hpp"
#include "game/message.hpp"
#include "game/player/event_loadout.hpp"
#include "game/player/player_state.hpp"
#include "game/room_grid.hpp"
#include "game/room_scripts.hpp"
#include "game/script_engine.hpp"
#include "game/spawn.hpp"
#include "game/story_flags.hpp"
#include "game/visited_rooms.hpp"

namespace od::port {

namespace {

constexpr uint32_t kFlagDoor = 0x700;
constexpr int16_t kMusicDipStep = 0x222;
constexpr int kBaseDefeatCount = 0x12;
constexpr int kMarkedCount = 0xb;
constexpr uint32_t kBaseGroup = 2;            // the room's group whose records stand at those walls
constexpr int32_t kOpenedSegment = 9;         // the floor segment a marked wall's cell switches to
constexpr uint8_t kHintWindow = 1;
constexpr uint32_t kMessageBank = 0x73c000;
constexpr int8_t kSaulKadaRoomList = 6;
constexpr uint8_t kLateScriptBlock = 8;
const int32_t kGatePoint[3] = {0x6200000, 0, 0xa000000}; // the base's gate (16.16)

uint8_t step() { return roomScriptState().handlerStep; }
void setStep(uint8_t s) { roomScriptState().handlerStep = s; }
void nextStep() { roomScriptState().handlerStep += 1u; }
template <class SetUp> bool once(bool run, SetUp setUp) {
    if (!run || step() != 0) return true;
    setUp();
    nextStep();
    return true;
}
// A record of the room's group (its integer point)
struct GroupRecord : DataRecord {
    static constexpr uint32_t kBytes = 0x14;
    ShortVec4 point; // +0xc
    template <class R> static GroupRecord load(const R& r) {
        GroupRecord v;
        v.point = {r.template read<int16_t>(0xc), r.template read<int16_t>(0xe), r.template read<int16_t>(0x10), r.template read<int16_t>(0x12)};
        return v;
    }
};
constexpr uint32_t kGroupRecordSize = 0x18;
// exe 0x588210(x, z): a floor cell of segment 0 at the point switched to the opened segment (exe 0x600f80)
void openFloorCell(int32_t x, int32_t z) {
    const uint32_t c = gridCellAtPoint(x, z); // exe 0x40f120
    if (c == 0) return;
    const uint16_t word = GridCell(c).word;
    if ((word & (kCellCollides | kCellFloor)) != (kCellCollides | kCellFloor) || (word & kCellSegmentNumberMask) != 0)
        return;
    cellSetSegment(uint32_t(gridColumn(x)), uint32_t(gridColumn(z)), kOpenedSegment, -1); // exe 0x600f80
}
// exe 0x5881b0: the marked rows' flags set -> the floor cells of the base group's records opened (at their cells'
// centres)
void markedCellsOpen() {
    const DataArray<uint32_t> groups(encounterState().groupTables);
    const uint32_t records = groups[kBaseGroup];
    for (uint32_t i = 0; i < uint32_t(kMarkedCount); ++i) {
        if (!storyFlag(dataRecord<DefeatRow>(stateAddress(TableId::PirateBaseRoomsMarkedDefeats) + i * 4, DefeatRow::kBytes).flag)) continue;
        const GroupRecord r = dataRecord<GroupRecord>(records + i * kGroupRecordSize, GroupRecord::kBytes);
        openFloorCell(int16_t((uint16_t(r.point.x) & 0xfe00u) + 0x100u), int16_t((uint16_t(r.point.z) & 0xfe00u) + 0x100u));
    }
}
// exe 0x588000: the base's gate cells opened
void baseGateCells() {
    static constexpr uint8_t kCells[7][2] = {{0x3b, 0x3b}, {0x3c, 0x3b}, {0x3d, 0x3b}, {0x3b, 0x3e},
                                             {0x3d, 0x3e}, {0x3b, 0x42}, {0x3d, 0x42}};
    for (const auto& c : kCells) switchGridCellVariant(c[0], c[1], 1); // exe 0x58d5f0
}

// exe 0x587cb0 (ST3B story 12 sub 0): the pirate base -- its defeats by flags, its marked walls, the script slot's
// group and zone group 1 / 2, the gate cells (taken) or the guards' records; the first time the mission opening; then
// the guards' defeats -> flags, and with flag 0x5a8 near the gate script 0x5c; step 3: the base taken
void st3bBase() {
    switch (step()) {
    case 0:
        clearStoryFlag(0x710);
        spawnRecords(stateAddress(TableId::PirateBaseRoomsSt3bBaseSpawns), 1);
        spawnRecords(stateAddress(TableId::PirateBaseRoomsSt3bBaseMoreSpawns), 1);
        defeatsFromFlags(stateAddress(TableId::PirateBaseRoomsBaseDefeats), kBaseDefeatCount);
        markedCellsOpen(); // exe 0x5881b0
        encounterSpawnGroupScriptSlot(0);
        encounterSpawnGroupZoneSlot(storyFlag(0x59f) ? 2 : 1);
        if (!storyFlag(0x5a1)) missionMusicBack();
        if (storyFlag(0x59f)) {
            baseGateCells(); // exe 0x588000
        } else if (!storyFlag(0x5a0)) {
            if (!storyFlag(0x5a8)) {
                spawnRecords(stateAddress(TableId::PirateBaseRoomsSt3bBaseLaterSpawns), 1);
                spawnRecords(stateAddress(TableId::PirateBaseRoomsSt3bBaseLaterMoreSpawns), 1);
            } else {
                spawnRecords(stateAddress(TableId::PirateBaseRoomsSt3bBaseLaterSpawns), 2);
            }
        }
        if (storyFlag(0x5c2)) {
            setStep(2);
            break;
        }
        setStoryFlag(0x5c2);
        nextStep();
        break;
    case 1:
        if (missionOpening(false, 0x10)) nextStep(); // exe 0x588340
        break;
    case 2: {
        const bool d8 = enemyDefeated(8), d9 = enemyDefeated(9), d10 = enemyDefeated(10);
        if (d8) {
            setStoryFlag(0x5a0);
            setStoryFlag(0x5ce);
            break;
        }
        if (d9 || d10) setStoryFlag(0x5ce);
        if (!storyFlag(0x5a8) || storyFlag(0x5a0) || storyFlag(0x59f) || player().pos.x <= 0) break;
        if (planarDistance(player().pos, kGatePoint) > 0x400) break; // exe 0x5e3b00
        flagsFromDefeats(stateAddress(TableId::PirateBaseRoomsBaseDefeats), kBaseDefeatCount);            // exe 0x587fc0
        setStoryFlag(0x59f);
        setStoryFlag(0x5ce);
        setStoryFlag(0x5cf);
        scriptRun(0x5c);
        nextStep();
        break;
    }
    case 3:
        for (uint32_t i = 0; i < uint32_t(kMarkedCount); ++i) {
            const DefeatRow row = dataRecord<DefeatRow>(stateAddress(TableId::PirateBaseRoomsMarkedDefeats) + i * 4, DefeatRow::kBytes);
            enemyMarkDefeated(row.enemy);
            setStoryFlag(row.flag);
        }
        markedCellsOpen();
        encounterSpawnGroupScriptSlot(0);
        encounterSpawnGroupScriptSlot(2);
        baseGateCells();
        nextStep();
        break;
    default: break;
    }
    groupClearedMusic(0x5a1);
    if (storyFlag(kFlagDoor)) flagsFromDefeats(stateAddress(TableId::PirateBaseRoomsBaseDefeats), kBaseDefeatCount); // exe 0x587fc0
}
// exe 0x588270 (ST3B story 12 sub 1): its records, flag 0x5ce, the guards' records until flag 0x59e, the music dips;
// flag 0x3ec -> 0x59e / 0x5d0 / 0x5d1; flag 0x6ff -> script block 8
void st3bHall() {
    if (step() == 0) {
        clearStoryFlag(0x710);
        spawnRecords(stateAddress(TableId::PirateBaseRoomsSt3bHallSpawns), 6);
        setStoryFlag(0x5ce);
        if (!storyFlag(0x59e)) {
            spawnRecords(stateAddress(TableId::PirateBaseRoomsSt3bHallMoreSpawns), 1);
            spawnRecords(stateAddress(TableId::PirateBaseRoomsSt3bHallLastSpawns), 2);
        }
        missionMusicDip(kMusicDipStep);
        nextStep();
    }
    if (step() == 1 && storyFlag(0x3ec)) {
        setStoryFlag(0x59e);
        setStoryFlag(0x5d0);
        setStoryFlag(0x5d1);
    }
    if (storyFlag(0x6ff)) scriptStart(kLateScriptBlock); // exe 0x5112b0
}
// exe 0x588660 (ST3C story 12 sub 2): flag 0x710, script 0x5d once (flag 0x6e); at flag 0x6f (the statue beaten) the
// story's next step and script 0x5e; then the island's room list, its flags, its records and the four opened cells
void st3cStatueHall() {
    switch (step()) {
    case 0:
        setStoryFlag(0x710);
        if (!storyFlag(0x6e)) {
            scriptRun(0x5d);
            setStoryFlag(0x6e);
        }
        nextStep();
        return;
    case 1:
        if (!storyFlag(0x6f)) return;
        gameState().nextStory += 1u; // exe 0x512800
        scriptRun(0x5e);
        setStoryFlag(0x6f);
        nextStep();
        return;
    case 2:
        roomListSetup(kSaulKadaRoomList, stateAddress(TableId::SaulKadaRoomsSt3cLaterRooms), stateAddress(TableId::SaulKadaRoomsSt3cLaterLinks)); // exe 0x55c000
        clearStoryFlag(0x710);
        setStoryFlag(0x711);
        clearStoryFlag(0x712);
        spawnRecords(stateAddress(TableId::PirateBaseRoomsSt3cStatueHallSpawns), 3);
        switchGridCellVariant(0x3f, 0x3f, 1);
        switchGridCellVariant(0x3f, 0x40, 1);
        switchGridCellVariant(0x40, 0x3f, 1);
        switchGridCellVariant(0x40, 0x40, 1);
        nextStep();
        return;
    default: return;
    }
}
// exe 0x588810 (ST3C story 13 sub 1): its records, flag 0x711; the racer's record (flag 0x6a) unless the race is done;
// then at flag 0x24d the time trial's parts put on (exe 0x542960) and window 1's message by the race's flags
void st3cRaceStart() {
    if (step() == 0) {
        spawnRecords(stateAddress(TableId::PirateBaseRoomsSt3cRaceStartSpawns), 2);
        setStoryFlag(0x711);
        if (!storyFlag(0x68) || storyFlag(0x69)) {
            spawnRecords(stateAddress(TableId::PirateBaseRoomsSt3cRaceStartMoreSpawns), 1);
            setStoryFlag(0x6a);
        }
        nextStep();
    } else if (step() == 1 && storyFlag(0x24d)) {
        loadoutGiveTimeTrial(); // exe 0x542960
        const int16_t message = storyFlag(0x680) ? 0x7e : storyFlag(0x681) ? 0x7f : storyFlag(0x682) ? 0x80 : 0;
        if (message) {
            windowClose(kHintWindow, WindowClose::Force);   // exe 0x5576b0
            windowOpen(kHintWindow, kMessageBank, message); // exe 0x557390
            nextStep();
        }
    }
}

} // namespace

bool pirateBaseRoomHandler(RoutineId fn, bool run) {
    switch (fn) {
    // ---- ST3B (story 12) -----------------------------------------------------------------------------------------
    case RoutineId::St3BRoomHandler:
        if (run) st3bBase();
        return true;
    case RoutineId::St3BRoomHandlerA:
        if (run) st3bHall();
        return true;
    // ---- ST3C (story 12) -----------------------------------------------------------------------------------------
    case RoutineId::St3CSub1RoomHandler: // sub 1: flags 0x710 / 0x711 cleared, 0x712 until the key 0x3ec, its records
        return once(run, [] {
            clearStoryFlag(0x710);
            clearStoryFlag(0x711);
            if (!storyFlag(0x3ec)) setStoryFlag(0x712);
            else clearStoryFlag(0x712);
            spawnRecords(stateAddress(TableId::PirateBaseRoomsSt3cRaceStartSpawns), 2);
            spawnRecords(stateAddress(TableId::PirateBaseRoomsSt3cSub1Spawns), 2);
        });
    case RoutineId::St3CRoomHandler:
        if (run) st3cStatueHall();
        return true;
    // ---- ST3C (story 13) -----------------------------------------------------------------------------------------
    case RoutineId::St3CRoomHandlerA:
        if (run) st3cRaceStart();
        return true;
    case RoutineId::St3CSub2RoomHandler: // sub 2
        return once(run, [] {
            spawnRecords(stateAddress(TableId::PirateBaseRoomsSt3cStatueHallSpawns), 2);
            clearStoryFlag(0x711);
        });
    case RoutineId::St3CSub3RoomHandler: // sub 3
        return once(run, [] {
            spawnRecords(stateAddress(TableId::PirateBaseRoomsSt3cSub3Spawns), 2);
            clearStoryFlag(0x711);
        });
    case RoutineId::St3CSub0RoomHandler: // sub 0
        return once(run, [] { clearStoryFlag(0x711); });
    default: return false;
    }
}

} // namespace od::port
