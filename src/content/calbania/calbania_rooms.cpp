// Calbania's and Pokte's rooms in the stage / room setup and the room scripts (see calbania_rooms.hpp), one routine per
// exe routine.
#include "content/calbania/calbania_rooms.hpp"
#include "content/calbania/st32_quiz.hpp"

#include "engine/data_source.hpp"
#include "game/patched_data.hpp"
#include "game/breakable_walls.hpp"
#include "game/game_state.hpp"
#include "game/player/player_weapons.hpp"
#include "game/room_scripts.hpp"
#include "game/script_engine.hpp"
#include "game/spawn.hpp"
#include "game/stage_state.hpp"
#include "game/story_flags.hpp"
#include "game/transition_state.hpp"
#include "game/visited_rooms.hpp"

namespace od::content {

using namespace od::port;

namespace {

// ---- the room lists: Calbania (list 5, by the story) and Pokte (list 3) -------------------------------------------
constexpr int8_t kCalbaniaList = 5, kPokteList = 3;
constexpr uint8_t kCalbaniaLateStory = 0x11; // Calbania's later lists from this story
struct StoryLists {
    RoutineId helper;
    TableId rooms, links, laterRooms, laterLinks; // the room lists and their links, early / from the later story
};
// the room helpers that only set Calbania's list by the story (the handler word cleared first)
constexpr StoryLists kListHelpers[] = {
    {RoutineId::St21RoomHelper, TableId::CalbaniaRoomsSt21Rooms, TableId::CalbaniaRoomsSt21Links, TableId::CalbaniaRoomsSt21LaterRooms, TableId::CalbaniaRoomsSt21LaterLinks}, // ST21
    {RoutineId::St22RoomHelper, TableId::CalbaniaRoomsSt22Rooms, TableId::CalbaniaRoomsSt22Links, TableId::CalbaniaRoomsSt22LaterRooms, TableId::CalbaniaRoomsSt22LaterLinks}, // ST22
};

// story flags
constexpr uint32_t kFlagDoorLock1 = kFlagLocked + 1;
constexpr uint32_t kFlagSt24Arrival = 0x580;   // story 3: Pokte's first visit (script 0x56) done
constexpr uint32_t kFlagSt24Departure = 0x581; // story 4: Pokte's exit back toward ST24 sub 1 closed
constexpr uint32_t kFlagSt24ExitWatch = 0xb5;  // cleared by ST24's room function
constexpr uint32_t kFlagSt1fGateBroken = 0x590; // story 9: the square's gate cells open
constexpr uint32_t kFlagSt1fWallOpen = 0x1f;    // the square's side wall open
constexpr uint32_t kFlagSt1fFieldEnds = 0x586;  // story 8: the square's field weapons end
constexpr uint32_t kFlagSt23Event = 0x5b0;      // story 8: ST23 sub 0's cell kept as it is
constexpr uint32_t kFlagSt30EntryVisited = 0x7d3; // ST30 sub 0's map flag
constexpr uint32_t kFlagSt30BoxesOpen = 0x21;   // Glyde's base's six cells open
constexpr uint32_t kFlagSt32Quiz = kFlagGroupStage + 0; // ST32: the quiz asked (script 0x2b)
constexpr uint32_t kFlagSt22Keys[4] = {0x3d4, 0x3d5, 0x3d6, 0x3d8}; // ST22: any of them unlocks the door

// scripts
constexpr uint8_t kScriptPokteArrival = 0x56, kScriptQuiz = 0x2b;

// the room callbacks (their routines are not ported): Pokte's (ST24 / ST25), the square's two (ST1F sub 0)
constexpr RoutineId kPokteRoomCallback = RoutineId::CalbaniaRoomsPokteRoomCallback;
constexpr RoutineId kSquareRoomCallback = RoutineId::CalbaniaRoomsSquareRoomCallback,
                    kSquareSecondCallback = RoutineId::CalbaniaRoomsSquareSecondCallback;
// the exits ST24's room function rewrites: sub 0's first (its destination room by the story) and sub 2's first
// (toward ST24 sub 1 at story 4 before the departure, else ST12 sub 0)
PatchedRecord<ExitEntry> st24FirstExit{TableId::CalbaniaRoomsSt24FirstExit};
PatchedRecord<ExitEntry> st24Room2Exit{TableId::CalbaniaRoomsSt24Room2Exit};
constexpr uint8_t kPokteStory = 3, kPokteDepartureStory = 4;
constexpr uint8_t kSquareEventStory = 8, kSquareGateStory = 9;
// ST1F's gate cells (exe data: 14 shorts, x << 8 | z)
const DataTable<ScalarTable<uint16_t, 14>> kSt1fGateCells{TableId::CalbaniaRoomsSt1fGateCells};
constexpr uint8_t kHandlerDone = 2;

// ---- the room helpers --------------------------------------------------------------------------------------------

bool calbaniaRoomHelper(RoutineId fn, bool run) {
    const GameState& g = gameState();
    RoomScriptState& room = roomScriptState();
    const bool early = g.story < kCalbaniaLateStory;
    for (const StoryLists& l : kListHelpers) {
        if (l.helper != fn) continue;
        if (!run) return true;
        room.clearHandlerWord();
        if (early) roomListSetup(kCalbaniaList, stateAddress(l.rooms), stateAddress(l.links));
        else roomListSetup(kCalbaniaList, stateAddress(l.laterRooms), stateAddress(l.laterLinks));
        return true;
    }
    switch (fn) {
    case RoutineId::St25RoomHelper: // ST25: Pokte's list
        if (!run) return true;
        roomListSetup(kPokteList, stateAddress(TableId::CalbaniaRoomsSt25Rooms), stateAddress(TableId::CalbaniaRoomsSt25Links));
        room.clearHandlerWord();
        return true;
    case RoutineId::St24RoomHelper:
        // ST24: Pokte's list; story 3 sub 0 before the first visit: a cell switched; the doors unlocked
        if (!run) return true;
        roomListSetup(kPokteList, stateAddress(TableId::CalbaniaRoomsSt24Rooms), stateAddress(TableId::CalbaniaRoomsSt24Links));
        room.clearHandlerWord();
        if (g.room == 0 && g.story == kPokteStory && !storyFlag(kFlagSt24Arrival)) switchGridCellVariant(0x4c, 0x4a, 1);
        clearLockFlags(kLockFlags);
        return true;
    case RoutineId::St1FRoomHelper: {
        // ST1F (the square): Calbania's list by the story with the cell (0x40, 0x3b) to 1 / 0 and (0x3b,
        // 0x3f) to 0 / 1; from story 9 (9: only with the gate broken) the gate's 14 cells to 1, else with the side wall
        // open (0x46, 0x3f) to 1
        if (!run) return true;
        room.clearHandlerWord();
        uint8_t v;
        if (early) {
            roomListSetup(kCalbaniaList, stateAddress(TableId::CalbaniaRoomsSt1fRooms), stateAddress(TableId::CalbaniaRoomsSt1fLinks));
            switchGridCellVariant(0x40, 0x3b, 1);
            v = 0;
        } else {
            roomListSetup(kCalbaniaList, stateAddress(TableId::CalbaniaRoomsSt1fLaterRooms), stateAddress(TableId::CalbaniaRoomsSt1fLaterLinks));
            switchGridCellVariant(0x40, 0x3b, 0);
            v = 1;
        }
        switchGridCellVariant(0x3b, 0x3f, v);
        if (g.story > kSquareGateStory || (g.story == kSquareGateStory && storyFlag(kFlagSt1fGateBroken))) {
            for (size_t i = 0; i < kSt1fGateCells.size(); ++i) {
                const uint16_t cell = kSt1fGateCells[i];
                switchGridCellVariant(cell >> 8, cell & 0xff, 1);
            }
        } else if (storyFlag(kFlagSt1fWallOpen)) {
            switchGridCellVariant(0x46, 0x3f, 1);
        }
        return true;
    }
    case RoutineId::St20RoomHelper: // ST20 (Calbania's town): Calbania's list by the story, two doors unlocked
        if (!run) return true;
        room.clearHandlerWord();
        if (early) roomListSetup(kCalbaniaList, stateAddress(TableId::CalbaniaRoomsSt20Rooms), stateAddress(TableId::CalbaniaRoomsSt20Links));
        else roomListSetup(kCalbaniaList, stateAddress(TableId::CalbaniaRoomsSt20LaterRooms), stateAddress(TableId::CalbaniaRoomsSt20LaterLinks));
        clearLockFlags(2);
        return true;
    case RoutineId::St23RoomHelper:
        // ST23: Calbania's list by the story; sub 0: the cell (0x40, 0x49) to variant 0 before story 0x11
        // (not 8), 1 at story 8 before its event and from story 0x11 (story 8 after it: unchanged)
        if (!run) return true;
        room.clearHandlerWord();
        if (early) roomListSetup(kCalbaniaList, stateAddress(TableId::CalbaniaRoomsSt23Rooms), stateAddress(TableId::CalbaniaRoomsSt23Links));
        else roomListSetup(kCalbaniaList, stateAddress(TableId::CalbaniaRoomsSt23LaterRooms), stateAddress(TableId::CalbaniaRoomsSt23LaterLinks));
        if (g.room != 0) return true;
        if (g.story == kSquareEventStory) {
            if (storyFlag(kFlagSt23Event)) return true;
        } else if (early) {
            switchGridCellVariant(0x40, 0x49, 0);
            return true;
        }
        switchGridCellVariant(0x40, 0x49, 1);
        return true;
    case RoutineId::St30RoomHelper:
        // ST30 (Glyde's base): no room list; sub 0's map flag; with its boxes open six cells to variant 1
        if (!run) return true;
        room.clearHandlerWord();
        roomListSetup(kNoRoomList, 0, 0);
        if (g.room == 0) setStoryFlag(kFlagSt30EntryVisited);
        if (storyFlag(kFlagSt30BoxesOpen)) {
            switchGridCellVariant(0x3b, 0x41, 1);
            switchGridCellVariant(0x3b, 0x43, 1);
            switchGridCellVariant(0x3b, 0x45, 1);
            switchGridCellVariant(0x3d, 0x43, 1);
            switchGridCellVariant(0x3d, 0x45, 1);
            switchGridCellVariant(0x3f, 0x45, 1);
        }
        return true;
    case RoutineId::St31RoomHelper: { // ST31 (Glyde's base, inside): no room list; its room scripts' counters cleared
        if (!run) return true;
        room.clearHandlerWord();
        roomListSetup(kNoRoomList, 0, 0);
        GlydeBaseRoomState& s = glydeBaseRoomState();
        s.ticks = 0;
        s.boxLatchA = 0;
        s.boxLatchB = 0;
        s.scriptLatch = 0;
        return true;
    }
    case RoutineId::St32RoomHelper: { // ST32: Pokte's list; the quiz's block marked empty
        if (!run) return true;
        roomListSetup(kPokteList, stateAddress(TableId::CalbaniaRoomsSt32Rooms), stateAddress(TableId::CalbaniaRoomsSt32Links));
        room.clearHandlerWord();
        QuizShowBlock& show = stageBlock().part<QuizShowBlock>(); // the quiz's block words marked empty (-1)
        show.course = show.questionSet = 0xff;
        show.headPad = {0xff, 0xff};
        show.state = show.subState = show.step = show.subStep = 0xff;
        return true;
    }
    default: return false;
    }
}

// ---- the room functions' parts ------------------------------------------------------------------------------------

// ST1F (exe 0x5d9910): the field's weapons before story 8 and at story 8 until its field ends; sub 0's two callbacks
void st1fWeaponMode() {
    const GameState& g = gameState();
    const bool field =
        g.story < kSquareEventStory || (g.story == kSquareEventStory && !storyFlag(kFlagSt1fFieldEnds));
    weaponMode(field ? kWeaponModeField : kWeaponModeTown);
}
void st1fRoomCallbacks() {
    if (gameState().room != 0) return;
    StageState& st = stage();
    st.roomCallback = kSquareRoomCallback;
    st.secondRoomCallback = kSquareSecondCallback;
}
// ST20 (exe 0x5d9ab0, after the weapon mode): the town's room callback in sub 0
void st20RoomCallback() { stage().roomCallback = gameState().room == 0 ? kTownRoomCallback : RoutineId::None; }
// ST24 (exe 0x5da170, after the encounter slots): sub 0's first exit to sub 5 at story 3 (else sub 0), the exit watch
// flag cleared, sub 2's first exit to ST24 sub 1 at story 4 before the departure (else ST12 sub 0)
void st24Exits() {
    const GameState& g = gameState();
    st24FirstExit->destRoom = g.story != kPokteStory ? 0u : 5u;
    clearStoryFlag(kFlagSt24ExitWatch);
    ExitEntry& exit = *st24Room2Exit;
    if (g.story == kPokteDepartureStory && !storyFlag(kFlagSt24Departure)) {
        exit.destStage = 0x24;
        exit.destRoom = 1;
    } else {
        exit.destStage = 0x12;
        exit.destRoom = 0;
    }
}
// ST24 / ST25 (after the weapon mode): Pokte's callback (ST24: but in sub 2; ST25: in sub 0 only)
void st24RoomCallback() { stage().roomCallback = gameState().room != 2 ? kPokteRoomCallback : RoutineId::None; }
void st25RoomCallback() { stage().roomCallback = gameState().room == 0 ? kPokteRoomCallback : RoutineId::None; }
// ST25 (exe 0x5da380): the field's weapons in sub 6 (the fight with Tron's tank)
void st25WeaponMode() { weaponMode(gameState().room == 6 ? kWeaponModeField : kWeaponModeTown); }

// ---- the stage cases ---------------------------------------------------------------------------------------------

// ST32 (exe 0x5864c0, after the handler): unless a transition is armed, the quiz asked -> script 0x2b
void st32Quiz() {
    if (!storyFlag(kFlagTransitionArmed) && storyFlag(kFlagSt32Quiz)) {
        scriptRun(kScriptQuiz);
        clearStoryFlag(kFlagSt32Quiz);
    }
}

// ---- the room scripts --------------------------------------------------------------------------------------------

bool calbaniaRoomScriptHandler(RoutineId fn, bool run) {
    RoomScriptState& room = roomScriptState();
    switch (fn) {
    case RoutineId::St21RoomCase: // ST21 (the stage case): only the door test (game/transition)
        return true;
    case RoutineId::St22RoomCase:
        // ST22 (the stage case): step 0: the door locked, step 1; 1: one of the keys -> unlocked, step 2
        if (!run) return true;
        if (room.handlerStep == 0) {
            setStoryFlag(kFlagDoorLock1);
            room.handlerStep = 1;
        } else if (room.handlerStep == 1 &&
                   (storyFlag(kFlagSt22Keys[0]) || storyFlag(kFlagSt22Keys[1]) || storyFlag(kFlagSt22Keys[2]) ||
                    storyFlag(kFlagSt22Keys[3]))) {
            clearStoryFlag(kFlagDoorLock1);
            room.handlerStep = kHandlerDone;
        }
        return true;
    case RoutineId::St24Story3Sub0RoomHandler:
        // ST24 story 3 sub 0: the first visit: one actor of the records and script 0x56; later the six
        if (!run || room.handlerStep != 0) return true;
        if (!storyFlag(kFlagSt24Arrival)) {
            spawnRecords(stateAddress(TableId::PokteRoomsVillagersSpawns), 1);
            setStoryFlag(kFlagSt24Arrival);
            scriptRun(kScriptPokteArrival);
            return true;
        }
        spawnRecords(stateAddress(TableId::PokteRoomsVillagersSpawns), 6);
        ++room.handlerStep;
        return true;
    default: return false;
    }
}

} // namespace

void registerCalbaniaRooms(ContentRegistry& r) {
    r.roomHelpers.push_back(&calbaniaRoomHelper);
    r.roomHandlers.push_back(&calbaniaRoomScriptHandler);
    r.roomFunctions[0x1f] = {.weaponMode = &st1fWeaponMode, .tail = &st1fRoomCallbacks};
    r.roomFunctions[0x20] = {.tail = &st20RoomCallback};
    r.roomFunctions[0x24] = {.afterEncounterSlots = &st24Exits, .tail = &st24RoomCallback};
    r.roomFunctions[0x25] = {.weaponMode = &st25WeaponMode, .tail = &st25RoomCallback};
    r.roomCaseTails[0x32] = &st32Quiz;
}

} // namespace od::content
