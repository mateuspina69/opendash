// The rooms of the sub-ruins ST4D / ST4E / ST4F / ST50 (see subruin_rooms.hpp): the room helpers, the room scripts'
// stage cases and their handlers (their state: game/room_scripts), one case per exe function. Branches that are
// unreachable or whose callees are not ported throw notPorted.
#include "content/subruins/subruin_rooms.hpp"

#include "engine/data_source.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/breakable_walls.hpp"
#include "game/display.hpp"
#include "game/encounter.hpp"
#include "game/game_state.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/room_scripts.hpp"
#include "game/spawn.hpp"
#include "game/story_flags.hpp"
#include "game/visited_rooms.hpp"

namespace od::port {

namespace {

constexpr const char* kSubruinPort = "subruin port"; // the area of its port-gap errors

// the stages
constexpr uint8_t kStageST4D = 0x4d, kStageST4E = 0x4e, kStageST4F = 0x4f, kStageST50 = 0x50;
// the stage setups' layout flags (ST4D's three layouts, the rolls of ST4F / ST50)
constexpr uint32_t kFlagLayoutA = 0xb0, kFlagLayoutB = 0xb1, kFlagLayoutC = 0xb2;
constexpr uint32_t kFlagST4FRoll = 0xbb, kFlagST50Roll = 0xb8;
// the rooms' own flags
constexpr uint32_t kFlagST4DCellA = 0xbd, kFlagST4DCellB = 0xbe; // the cells opened (ST4D subs 4 / 7)
constexpr uint32_t kFlagST4ECell = 0xbc;                          // the cell opened (ST4E sub 4)
constexpr uint32_t kFlagST4FSub11Cleared = 0xb9, kFlagST4FSub12Done = 0xba;
constexpr uint32_t kFlagST50Sub9Cleared = 0xb6, kFlagST50Sub10Done = 0xb7;
constexpr uint32_t kFlagVisitedST4D = 0x7bb, kFlagVisitedST4E = 0x7ab, kFlagVisitedST4F = 0x7d1, kFlagVisitedST50 = 0x7e1;
// the cells their handlers watch (column, row)
constexpr uint32_t kST4DCellAColumn = 0x3f, kST4DCellARow = 0x3e, kST4DCellBColumn = 0x41, kST4DCellBRow = 0x3c;
constexpr uint32_t kST4ECellColumn = 0x40, kST4ECellRow = 0x44;
// the records they spawn (exe data) and the ids they wait for
constexpr uint32_t kST4FSub7Id = 0x40, kST50Sub9Ids[] = {0x49, 0x4a, 0x4b};
// ST4D sub 6's object-state group (pool 0x20, type 0x15, variant 3)
constexpr uint32_t kST4DSub6GroupClass = 0x20, kST4DSub6GroupType = 0x15, kST4DSub6GroupVariant = 3;
constexpr uint8_t kST50Pace = 3; // ST50 runs at 20 game ticks a second

// the handler tables by sub-room (exe 0x8cd624 / 0x8cd648 / 0x8cd66c / 0x8cd690 / 0x8cd6b8 / 0x8cd748: the named
// routines of engine/routines.csv)
using SubruinHandlers = DataTable<RoutineTable<0x20>>;
const SubruinHandlers st4dLayoutAHandlers{TableId::St4dLayoutAHandlers};
const SubruinHandlers st4dLayoutBHandlers{TableId::St4dLayoutBHandlers};
const SubruinHandlers st4dLayoutCHandlers{TableId::St4dLayoutCHandlers};
const SubruinHandlers st4eHandlers{TableId::St4eRoomHandlers}; // (game/transition's door test reads it too)
const SubruinHandlers st4fHandlers{TableId::St4fHandlers};
const SubruinHandlers st50Handlers{TableId::St50Handlers};
// the current room's handler: none (no stage case), none of the table (ST4D without a layout flag), or the table's
struct CurrentHandler {
    bool stageCase = false;
    RoutineRef routine; // null: the table has no handler
};

// the zone slot (the slot of the zones' kind)
EncounterSlot& zoneSlot() {
    EncounterState& e = encounterState();
    return e.slots[uint8_t(e.slotOfKind[uint32_t(EncounterKind::Zones)])];
}

// exe 0x58bc10 + g * 0x40 (g 0..7), 0x58be10 (8), 0x58ba30 (9), 0x58ba70 (10): step 0 -> encounter group g in the
// zone slot (exe 0x5e6640), step 1; the door test
int groupHandler(RoutineId fn) {
    switch (fn) {
    case RoutineId::SubruinGroup0: return 0;
    case RoutineId::SubruinGroup1: return 1;
    case RoutineId::SubruinGroup2: return 2;
    case RoutineId::SubruinGroup3: return 3;
    case RoutineId::SubruinGroup4: return 4;
    case RoutineId::SubruinGroup5: return 5;
    case RoutineId::SubruinGroup6: return 6;
    case RoutineId::SubruinGroup7: return 7;
    case RoutineId::SubruinGroup8: return 8;
    case RoutineId::SubruinGroup9: return 9;
    case RoutineId::SubruinGroup10: return 10;
    default: return -1;
    }
}

bool handlerKnown(RoutineId fn) {
    if (groupHandler(fn) >= 0) return true;
    switch (fn) {
    case RoutineId::St4dSub0Locks: case RoutineId::St4dLockCleared: case RoutineId::St4dCellAWatch:
    case RoutineId::St4dSub6Group: case RoutineId::St4dCellBWatch: case RoutineId::St4dSub8Lock:
    case RoutineId::St4eRoom4Handler: case RoutineId::St4fSub7Roll: case RoutineId::St4fSub11Records:
    case RoutineId::St4fSub12Record: case RoutineId::St50Sub9Group: case RoutineId::St50Sub10Record:
    case RoutineId::RoomDoorScript:
        return true;
    default: return false;
    }
}

// ST4D 0x58b610 / 0x58b760: step 0: the lock flag cleared; with the cell's flag the cell is switched to variant 1
// (opened before); step 1. Every tick: the flag clear and the cell's segment broken (opened) -> the flag set
void cellWatch(uint32_t flag, uint32_t column, uint32_t row) {
    RoomScriptState& room = roomScriptState();
    if (room.handlerStep == 0) {
        clearStoryFlag(kFlagLocked);
        if (storyFlag(flag)) switchGridCellVariant(column, row, 1);
        ++room.handlerStep;
    }
    if (!storyFlag(flag) && gridCellSegmentBroken(column, row)) setStoryFlag(flag);
}

void runHandler(const RoutineRef& handler) {
    RoomScriptState& room = roomScriptState();
    const RoutineId fn = handler.id();
    if (const int g = groupHandler(fn); g >= 0) {
        if (room.handlerStep == 0) {
            encounterSpawnGroupZoneSlot(g);
            ++room.handlerStep;
        }
        roomDoorScript();
        return;
    }
    switch (fn) {
    case RoutineId::St4dSub0Locks: // ST4D sub 0: the lock flags 0 / 1 cleared
        clearStoryFlag(kFlagLocked);
        clearStoryFlag(kFlagLocked + 1);
        return;
    case RoutineId::St4dLockCleared: // ST4D subs 1..3 / 5: the lock flag cleared
        clearStoryFlag(kFlagLocked);
        return;
    case RoutineId::St4dCellAWatch: // ST4D sub 4: its cell and flag
        cellWatch(kFlagST4DCellA, kST4DCellAColumn, kST4DCellARow);
        return;
    case RoutineId::St4dCellBWatch: // ST4D sub 7: its cell and flag
        cellWatch(kFlagST4DCellB, kST4DCellBColumn, kST4DCellBRow);
        return;
    case RoutineId::St4dSub8Lock: // ST4D sub 8 (and sub 6 of layout B): step 0 -> the lock flag cleared, step 1
        if (room.handlerStep == 0) {
            clearStoryFlag(kFlagLocked);
            ++room.handlerStep;
        }
        return;
    case RoutineId::St4dSub6Group: { // ST4D sub 6 (layouts A / C): step 0 -> the lock flag cleared, step 1; step 1: once its
        // object-state group is marked -> encounter group 10 in the zone slot, step 2
        const uint8_t s = room.handlerStep;
        if (s == 0) {
            clearStoryFlag(kFlagLocked);
            ++room.handlerStep;
        } else if (s != 1) {
            return;
        }
        if (enemyGroupMarks(enemyGroupFind(kST4DSub6GroupClass, kST4DSub6GroupType, kST4DSub6GroupVariant),
                            EnemyGroupMarks::LastTick) == 0)
            return;
        EncounterSlot& zone = zoneSlot();
        if (zone.active == kEncounterSlotArmed) encounterSpawnGroupZoneSlot(10);
        else encounterSpawnGroup(zone, false, 10);
        ++room.handlerStep;
        return;
    }
    case RoutineId::St4eRoom4Handler: // ST4E sub 4: its flag once its cell is opened; the door test
        if (!storyFlag(kFlagST4ECell) && gridCellSegmentBroken(kST4ECellColumn, kST4ECellRow)) setStoryFlag(kFlagST4ECell);
        roomDoorScript();
        return;
    case RoutineId::RoomDoorScript: // the door test only
        roomDoorScript();
        return;
    case RoutineId::St4fSub7Roll: // ST4F sub 7: step 0 -> group 7 and, with the stage's roll while its id is not defeated, its record
        // (exe 0x41dce0), step 1; the door test
        if (room.handlerStep == 0) {
            encounterSpawnGroupZoneSlot(7);
            if (storyFlag(kFlagST4FRoll) && !enemyDefeated(kST4FSub7Id)) encounterSpawnRecord(stateAddress(TableId::SubruinRoomsST4FSub7Record));
            ++room.handlerStep;
        }
        roomDoorScript();
        return;
    case RoutineId::St4fSub11Records: { // ST4F sub 11: step 0: cleared before -> step 2; else its 2 records, the doors locked, step 1;
        // step 1: both record actors gone -> unlocked, the cleared flag, step 2. The door test.
        const uint8_t s = room.handlerStep;
        if (s == 0) {
            if (storyFlag(kFlagST4FSub11Cleared)) {
                room.handlerStep = 2;
            } else {
                spawnRecords(stateAddress(TableId::SubruinRoomsST4FSub11Records), 2);
                setStoryFlag(kFlagLocked);
                room.handlerStep = 1;
            }
        } else if (s == 1 && !(actorHeaderAt(room.actors[0]).flags & kActorLive) &&
                   !(actorHeaderAt(room.actors[1]).flags & kActorLive)) {
            clearStoryFlag(kFlagLocked);
            setStoryFlag(kFlagST4FSub11Cleared);
            room.handlerStep = 2;
        }
        roomDoorScript();
        return;
    }
    case RoutineId::St4fSub12Record: // ST4F sub 12: step 0 without its flag -> its record, step 1; the door test
        if (room.handlerStep == 0 && !storyFlag(kFlagST4FSub12Done)) {
            spawnRecords(stateAddress(TableId::SubruinRoomsST4FSub12Record), 1);
            ++room.handlerStep;
        }
        roomDoorScript();
        return;
    case RoutineId::St50Sub9Group: { // ST50 sub 9: step 0: the stage's roll -> group 10; not cleared before: group 9, the doors locked,
        // step 1, else step 2; step 1: its three ids defeated -> unlocked, the cleared flag, step 2. The door test.
        const uint8_t s = room.handlerStep;
        if (s == 0) {
            if (storyFlag(kFlagST50Roll)) encounterSpawnGroupZoneSlot(10);
            if (!storyFlag(kFlagST50Sub9Cleared)) {
                encounterSpawnGroupZoneSlot(9);
                setStoryFlag(kFlagLocked);
                room.handlerStep = 1;
            } else {
                room.handlerStep = 2;
            }
        } else if (s == 1 && enemyDefeated(kST50Sub9Ids[0]) && enemyDefeated(kST50Sub9Ids[1]) &&
                   enemyDefeated(kST50Sub9Ids[2])) {
            clearStoryFlag(kFlagLocked);
            setStoryFlag(kFlagST50Sub9Cleared);
            room.handlerStep = 2;
        }
        roomDoorScript();
        return;
    }
    case RoutineId::St50Sub10Record: // ST50 sub 10: step 0 without its flag -> its record, step 1; the door test
        if (room.handlerStep == 0 && !storyFlag(kFlagST50Sub10Done)) {
            spawnRecords(stateAddress(TableId::SubruinRoomsST50Sub10Record), 1);
            ++room.handlerStep;
        }
        roomDoorScript();
        return;
    default: notPortedAt(kSubruinPort, "sub-ruin room-script handler", handler.pointerValue());
    }
}

// the handler of the current room for the stages 0x4d..0x50 (a null routine: case 0x4d without a layout flag)
CurrentHandler currentHandler() {
    const GameState& g = gameState();
    const uint8_t sub = g.room;
    switch (g.stage) {
    case kStageST4D:
        if (storyFlag(kFlagLayoutA)) return {true, st4dLayoutAHandlers[sub]};
        if (storyFlag(kFlagLayoutB)) return {true, st4dLayoutBHandlers[sub]};
        if (storyFlag(kFlagLayoutC)) return {true, st4dLayoutCHandlers[sub]};
        return {true, RoutineRef()};
    case kStageST4E: return {true, st4eHandlers[sub]};
    case kStageST4F: return {true, st4fHandlers[sub]};
    case kStageST50: return {true, st50Handlers[sub]};
    default: return {};
    }
}

} // namespace

// exe 0x58b830, the lock flag cleared: ST14's room-script table (its stories 10..) points at it too, so it is a handler
// of the content's chain as well
bool subruinLockRoomHandler(RoutineId fn, bool run) {
    if (fn != RoutineId::St4dSub8Lock) return false;
    if (run) runHandler(RoutineRef(fn, 0));
    return true;
}

bool subruinRoomHelper(RoutineId fn, bool run) {
    switch (fn) {
    case RoutineId::St4DRoomHelper: // ST4D: sub 0 -> its visit flag
    case RoutineId::St4FRoomHelper: // ST4F: sub 0 -> its visit flag
    case RoutineId::St50RoomHelper:
        // ST50: sub 0 -> its visit flag; records 0x8cd778 x 2 (pool 0xa0 type 0xa, pool 0xe0 type 0x20)
    case RoutineId::St4ERoomHelper:
        // ST4E: sub 4 with its cell's flag -> the cell to variant 1; sub 0 -> its visit flag
    {
        if (!run) return true;
        roomScriptState().clearHandlerWord();
        roomListSetup(kNoRoomList, 0, 0); // exe 0x55c000(-1, 0, 0)
        const GameState& g = gameState();
        if (fn == RoutineId::St4ERoomHelper && g.room == 4 && storyFlag(kFlagST4ECell))
            switchGridCellVariant(kST4ECellColumn, kST4ECellRow, 1);
        if (g.room == 0) {
            switch (fn) {
            case RoutineId::St4DRoomHelper: setStoryFlag(kFlagVisitedST4D); break;
            case RoutineId::St4ERoomHelper: setStoryFlag(kFlagVisitedST4E); break;
            case RoutineId::St4FRoomHelper: setStoryFlag(kFlagVisitedST4F); break;
            default: setStoryFlag(kFlagVisitedST50); break;
            }
        }
        if (fn == RoutineId::St50RoomHelper) spawnRecords(stateAddress(TableId::SubruinRoomsST50Records), 2);
        return true;
    }
    default: return false;
    }
}

bool subruinRoomScripts() {
    const CurrentHandler h = currentHandler();
    if (!h.stageCase) return false;
    const uint8_t stage = gameState().stage;
    if (!h.routine.null() || stage != kStageST4D) runHandler(h.routine); // a null entry elsewhere: a call through null
    switch (gameState().stage) {
    case kStageST4D: roomDoorScript(); break; // the case's own door test (0x58b5c5)
    case kStageST50: // after the handler (0x58bbf3): the pace 3 (20 fps; see the 30 fps cap notes) and the player in water
        display().pace = kST50Pace;
        player().inWater = 1;
        break;
    default: break;
    }
    return true;
}

bool subruinRoomScriptsKnown() {
    const CurrentHandler h = currentHandler();
    if (!h.stageCase) return false;
    return (h.routine.null() && gameState().stage == kStageST4D) || handlerKnown(h.routine.id());
}

} // namespace od::port

// ST4F / ST50 group handlers 0x58bc10 + g * 0x40 (groupHandler) kHandlersTranscribed: 0x58bc10 0x58bc50 0x58bc90 0x58bcd0 0x58bd10 0x58bd50 0x58bd90
// 0x58bdd0
