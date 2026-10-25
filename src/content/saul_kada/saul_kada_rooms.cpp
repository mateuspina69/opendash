// Saul Kada's rooms in the stage / room setup and the room scripts (see saul_kada_rooms.hpp), one routine per exe
// routine.
#include "content/saul_kada/saul_kada_rooms.hpp"

#include "engine/data_source.hpp"
#include "game/breakable_walls.hpp"
#include "game/display.hpp"
#include "game/encounter.hpp"
#include "game/game_state.hpp"
#include "game/player/player_state.hpp"
#include "game/player/player_weapons.hpp"
#include "game/room_scripts.hpp"
#include "game/script_engine.hpp"
#include "game/stage_state.hpp"
#include "game/story_flags.hpp"
#include "game/visited_rooms.hpp"

namespace od::content {

using namespace od::port;

namespace {

// ---- the island's room list (list 6, by the story) ------------------------------------------------------------------
constexpr int8_t kSaulKadaList = 6;
constexpr uint8_t kSaulKadaLateStory = 0xd; // the island's later lists from this story
struct StoryLists {
    RoutineId helper;
    TableId rooms, links, laterRooms, laterLinks; // the room lists and their links, early / from the later story
};
constexpr StoryLists kListHelpers[] = {
    {RoutineId::St2ARoomHelper, TableId::SaulKadaRoomsSt2ARooms, TableId::SaulKadaRoomsSt2ALinks, TableId::SaulKadaRoomsSt2ALaterRooms, TableId::SaulKadaRoomsSt2ALaterLinks}, // ST2A
    {RoutineId::St2BRoomHelper, TableId::SaulKadaRoomsSt2BRooms, TableId::SaulKadaRoomsSt2BLinks, TableId::SaulKadaRoomsSt2BLaterRooms, TableId::SaulKadaRoomsSt2BLaterLinks}, // ST2B
    {RoutineId::St2CRoomHelper, TableId::SaulKadaRoomsSt2CRooms, TableId::SaulKadaRoomsSt2CLinks, TableId::SaulKadaRoomsSt2CLaterRooms, TableId::SaulKadaRoomsSt2CLaterLinks}, // ST2C
    {RoutineId::St3BRoomHelper, TableId::SaulKadaRoomsSt3BRooms, TableId::SaulKadaRoomsSt3BLinks, TableId::SaulKadaRoomsSt3BLaterRooms, TableId::SaulKadaRoomsSt3BLaterLinks}, // ST3B
};
constexpr StoryLists kSt29Lists = {RoutineId::St29RoomHelper, TableId::SaulKadaRoomsSt29Rooms, TableId::SaulKadaRoomsSt29Links, TableId::SaulKadaRoomsSt29LaterRooms, TableId::SaulKadaRoomsSt29LaterLinks};
constexpr StoryLists kSt3cLists = {RoutineId::St3CRoomHelper, TableId::SaulKadaRoomsSt3cRooms, TableId::SaulKadaRoomsSt3cLinks, TableId::SaulKadaRoomsSt3cLaterRooms, TableId::SaulKadaRoomsSt3cLaterLinks};
constexpr StoryLists kSt48Lists = {RoutineId::St48RoomHelper, TableId::SaulKadaRoomsSt48Rooms, TableId::SaulKadaRoomsSt48Links, TableId::SaulKadaRoomsSt48LaterRooms, TableId::SaulKadaRoomsSt48LaterLinks};
// ST41 / ST42's script groups by sub-room (exe data, s16 each; < 0: none)
const DataTable<ScalarTable<int16_t, 0x10>> kSt41ScriptGroups{TableId::SaulKadaRoomsSt41ScriptGroups};
const DataTable<ScalarTable<int16_t, 0x10>> kSt42ScriptGroups{TableId::SaulKadaRoomsSt42ScriptGroups};

// story flags
constexpr uint32_t kFlagDoorLock1 = kFlagLocked + 1;
constexpr uint32_t kFlagSt26EntryVisited = 0x7e2; // ST26 sub 0's map flag
constexpr uint32_t kFlagSt26CellA = 0x62, kFlagSt26CellB = 0x63; // ST26 sub 5's two passages
constexpr uint32_t kFlagSt27Collapse = 0x4c;      // ST27 sub 8's floor fallen
constexpr uint32_t kFlagSt27Passage = 0x57;       // ST27 sub 4's passage open
constexpr uint32_t kFlagSt29FieldEnds = 0x5e3;    // cleared by ST29's room function
constexpr uint32_t kFlagSt48Gate = 0x580;         // story 0xc: ST48 sub 0's gate kept shut
constexpr uint32_t kFlagSt48Keys[3] = {0x3d4, 0x3d5, 0x3d8}; // any of them: ST48's door unlocked
constexpr uint32_t kFlagCarryHeavy = 0x155;       // ST41 / ST42 / ST44: the player carries the heavy load / the light
constexpr uint32_t kFlagCarryLight = 0x153;       // one

constexpr uint8_t kSt48GateStory = 0xc;
constexpr int8_t kHeavyBurden = 0xc, kLightBurden = -8;
constexpr uint8_t kFloodedPace = 2; // ST48: the room's pace (30 ticks a second)
// ST27 / ST34's vertex-colour routine of the strip renderers (the lava glow; only render families 2 / 3 call it, and
// every ST27 / ST34 room is family 0 or 1)
constexpr RoutineId kLavaGlowRoutine = RoutineId::SaulKadaRoomsLavaGlowRoutine;
// ST48's room function's second helper: the encounter slots re-armed (after the encounter slots' room: its part)
constexpr RoutineId kSt48RearmHelper = RoutineId::SaulKadaRoomsSt48RearmHelper;
constexpr uint32_t kSt48EncounterKinds = 1u << uint32_t(EncounterKind::Field);

// the island's list by the story (the handler word cleared first)
void storyList(const StoryLists& l) {
    roomScriptState().clearHandlerWord();
    if (gameState().story < kSaulKadaLateStory) roomListSetup(kSaulKadaList, stateAddress(l.rooms), stateAddress(l.links));
    else roomListSetup(kSaulKadaList, stateAddress(l.laterRooms), stateAddress(l.laterLinks));
}

// ---- the room helpers --------------------------------------------------------------------------------------------

bool saulKadaRoomHelper(RoutineId fn, bool run) {
    const GameState& g = gameState();
    RoomScriptState& room = roomScriptState();
    for (const StoryLists& l : kListHelpers) {
        if (l.helper != fn) continue;
        if (run) storyList(l);
        return true;
    }
    switch (fn) {
    case RoutineId::St41RoomHelper:
        // ST41 / ST42: no room list, the script slot's group of the sub-room (exe 0x5e66b0), 16 doors
    case RoutineId::St42RoomHelper: { // unlocked
        if (!run) return true;
        room.clearHandlerWord();
        roomListSetup(kNoRoomList, 0, 0);
        const int16_t group =
            fn == RoutineId::St41RoomHelper ? int16_t(kSt41ScriptGroups[g.room]) : int16_t(kSt42ScriptGroups[g.room]);
        if (group >= 0) encounterSpawnGroupScriptSlot(group);
        clearLockFlags(0x10);
        return true;
    }
    case RoutineId::St26RoomHelper: // ST26: no room list; sub 0's map flag; sub 5 opens its two passages' cells
        if (!run) return true;
        room.clearHandlerWord();
        roomListSetup(kNoRoomList, 0, 0);
        if (g.room == 0) setStoryFlag(kFlagSt26EntryVisited);
        if (g.room == 5) {
            if (storyFlag(kFlagSt26CellA)) switchGridCellVariant(0x44, 0x40, 1);
            if (storyFlag(kFlagSt26CellB)) switchGridCellVariant(0x3c, 0x40, 1);
        }
        return true;
    case RoutineId::St27RoomHelper:
        // ST27: no room list; sub 8 with its floor fallen opens five cells (the exe opens two of them
        // twice), sub 4 with its passage open two
        if (!run) return true;
        room.clearHandlerWord();
        roomListSetup(kNoRoomList, 0, 0);
        if (g.room == 8 && storyFlag(kFlagSt27Collapse)) {
            switchGridCellVariant(0x3d, 0x3c, 1);
            switchGridCellVariant(0x3d, 0x3c, 1);
            switchGridCellVariant(0x3e, 0x3d, 1);
            switchGridCellVariant(0x3e, 0x3d, 1);
            switchGridCellVariant(0x3d, 0x40, 1);
        }
        if (g.room == 4 && storyFlag(kFlagSt27Passage)) {
            switchGridCellVariant(0x3f, 0x44, 1);
            switchGridCellVariant(0x3f, 0x40, 1);
        }
        return true;
    case RoutineId::St28RoomHelper: // ST28: no room list; outside sub 0 the field encounters suspended (exe 0x5e6870)
        if (!run) return true;
        room.clearHandlerWord();
        roomListSetup(kNoRoomList, 0, 0);
        if (g.room != 0) encounterState().control |= kFieldSuspended;
        return true;
    case RoutineId::St29RoomHelper:
        // ST29: the island's list by the story; sub 1 after story 0xc opens the cell (0x40, 0x49)
        if (!run) return true;
        storyList(kSt29Lists);
        if (g.room == 1 && g.story >= kSaulKadaLateStory) switchGridCellVariant(0x40, 0x49, 1);
        return true;
    case RoutineId::St3CRoomHelper:
        // ST3C: the island's list by the story; sub 3: the cell (0x40, 0x40), variant 1 before story 0xd,
        // else 0; sub 2 after story 0xc opens the four cells (0x3f..0x40, 0x3f..0x40)
        if (!run) return true;
        storyList(kSt3cLists);
        if (g.room == 3) switchGridCellVariant(0x40, 0x40, g.story < kSaulKadaLateStory ? 1 : 0);
        if (g.room == 2 && g.story >= kSaulKadaLateStory) {
            switchGridCellVariant(0x3f, 0x3f, 1);
            switchGridCellVariant(0x3f, 0x40, 1);
            switchGridCellVariant(0x40, 0x3f, 1);
            switchGridCellVariant(0x40, 0x40, 1);
        }
        return true;
    case RoutineId::St48RoomHelper: {
        // ST48: the island's list by the story; sub 0: the gate cell (0x45, 0x3c), shut (0) at story 0xc
        // with its flag, else open; the room's pace, out of water, the door locked unless one of the keys
        if (!run) return true;
        storyList(kSt48Lists);
        if (g.room == 0)
            switchGridCellVariant(0x45, 0x3c, g.story == kSt48GateStory && storyFlag(kFlagSt48Gate) ? 0 : 1);
        display().pace = kFloodedPace;
        player().inWater = 0;
        if (!storyFlag(kFlagSt48Keys[0]) && !storyFlag(kFlagSt48Keys[1]) && !storyFlag(kFlagSt48Keys[2]))
            setStoryFlag(kFlagDoorLock1);
        return true;
    }
    case kSt48RearmHelper: // its part runs after the encounter slots' room
        return true;
    case RoutineId::St44RoomHelper: // ST44: no room list; the loads' flags cleared
        if (!run) return true;
        room.clearHandlerWord();
        roomListSetup(kNoRoomList, 0, 0);
        clearStoryFlag(kFlagCarryHeavy);
        clearStoryFlag(kFlagCarryLight);
        return true;
    default: return false;
    }
}

// ---- the room functions' parts ------------------------------------------------------------------------------------

// ST27 / ST34 (exe 0x5da6a0 / 0x5dbc40): the room render parameters by sub (render output) and the lava glow routine
void lavaGlowRoutine() { stage().vertexColourHook = kLavaGlowRoutine; }
// ST29 (exe 0x5daa40): the field's weapons before story 0xd; then its flag cleared
void st29WeaponMode() {
    weaponMode(gameState().story < kSaulKadaLateStory ? kWeaponModeField : kWeaponModeTown);
    clearStoryFlag(kFlagSt29FieldEnds);
}
// ST2A / ST2B (exe 0x5dac20 / 0x5dadf0): the field's weapons and the town's callback in subs 0..1
void st2aWeaponMode() { weaponMode(gameState().room < 2 ? kWeaponModeField : kWeaponModeTown); }
void st2aRoomCallback() { stage().roomCallback = gameState().room < 2 ? kTownRoomCallback : RoutineId::None; }
// ST3B (exe 0x5dca00): the field's weapons in sub 0
void st3bWeaponMode() { weaponMode(gameState().room == 0 ? kWeaponModeField : kWeaponModeTown); }
// ST3C (exe 0x5dcbc0): the field's weapons before story 0xd outside sub 1
void st3cWeaponMode() {
    const GameState& g = gameState();
    weaponMode(g.story < kSaulKadaLateStory && g.room != 1 ? kWeaponModeField : kWeaponModeTown);
}
// ST48 (exe 0x5ddf60): the encounter slots re-armed (exe 0x5e67f0) after their room; the field's weapons outside sub 0;
// its stage setup's encounter kinds: the field encounters through story 0xc
void st48Rearm() { encounterSlotsRearm(); }
void st48WeaponMode() { weaponMode(gameState().room != 0 ? kWeaponModeField : kWeaponModeTown); }
int32_t st48EncounterKinds() {
    return gameState().story <= kSt48GateStory ? int32_t(kSt48EncounterKinds) : 0;
}

// ---- the stage cases ---------------------------------------------------------------------------------------------

// ST41 / ST42 / ST44 (exe 0x589d80 / 0x58a1a0 / 0x58a6f0, before the handler): the player's burden by the load
// carried
void carryBurden() {
    player().burden = storyFlag(kFlagCarryHeavy) ? kHeavyBurden : (storyFlag(kFlagCarryLight) ? kLightBurden : 0);
}

// ---- the room scripts --------------------------------------------------------------------------------------------

bool saulKadaRoomScriptHandler(RoutineId fn, bool run) {
    switch (fn) {
    case RoutineId::RoomDoorScript: // a transition armed -> script 0 (the door: game/transition)
        if (run && storyFlag(kFlagTransitionArmed)) scriptRun(0);
        return true;
    default: return false;
    }
}

} // namespace

void registerSaulKadaRooms(ContentRegistry& r) {
    r.roomHelpers.push_back(&saulKadaRoomHelper);
    r.roomHandlers.push_back(&saulKadaRoomScriptHandler);
    r.roomFunctions[0x27] = {.afterEncounterSlots = &lavaGlowRoutine};
    r.roomFunctions[0x34] = {.afterEncounterSlots = &lavaGlowRoutine};
    r.roomFunctions[0x28] = {}; // the room render parameters by sub (render output)
    r.roomFunctions[0x29] = {.weaponMode = &st29WeaponMode};
    r.roomFunctions[0x2a] = {.weaponMode = &st2aWeaponMode, .tail = &st2aRoomCallback};
    r.roomFunctions[0x2b] = {.weaponMode = &st2aWeaponMode, .tail = &st2aRoomCallback};
    r.roomFunctions[0x3b] = {.weaponMode = &st3bWeaponMode};
    r.roomFunctions[0x3c] = {.weaponMode = &st3cWeaponMode};
    r.roomFunctions[0x48] = {.afterEncounterSlots = &st48Rearm, .weaponMode = &st48WeaponMode};
    r.stageEncounterKinds[0x48] = &st48EncounterKinds;
    r.roomCaseHeads[0x41] = &carryBurden;
    r.roomCaseHeads[0x42] = &carryBurden;
    r.roomCaseHeads[0x44] = &carryBurden;
}

} // namespace od::content
