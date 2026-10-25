// Forbidden Island's rooms in the stage / room setup and the room scripts (see island_rooms.hpp), one routine per exe
// routine.
#include "content/forbidden_island/island_rooms.hpp"

#include "game/patched_data.hpp"
#include "game/actor_pools.hpp"
#include "game/audio_logic.hpp"
#include "game/breakable_walls.hpp"
#include "game/game_state.hpp"
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

// ---- the island's room list (list 1), by the story ----------------------------------------------------------------
constexpr int8_t kIslandList = 1;
constexpr uint8_t kStormLastStory = 1; // the stormy island's lists through story 1, the clear island's after

// story flags
constexpr uint32_t kFlagDoorLock1 = kFlagLocked + 1, kFlagDoorLock2 = kFlagLocked + 2;
constexpr uint32_t kFlagSt3dRoom2 = kFlagGroupStage + 0;  // ST3D: in sub 2 / sub 3 (the stage's room flags)
constexpr uint32_t kFlagSt3dRoom3 = kFlagGroupStage + 1;
constexpr uint32_t kFlagSt3dGateOpen = 0xe1;              // ST3D sub 0's door stays unlocked
constexpr uint32_t kFlagSt3dKeyReported = 0x3d2;          // ... (the third ruin key reported)
constexpr uint32_t kFlagSt3dPassage = 0xe4;               // ST3D sub 1's passage open
constexpr uint32_t kFlagSt3eRoom4 = kFlagGroupStage + 4;  // ST3E: in sub 4 / sub 5
constexpr uint32_t kFlagSt3eRoom5 = kFlagGroupStage + 5;
constexpr uint32_t kFlagSt3eRoomOther = kFlagGroupStage + 6;
constexpr uint32_t kFlagSt3eStory2Talk = 0x5a1;           // script 0x64 ran at story 2
constexpr uint32_t kFlagKeysReported[3] = {0x3d0, 0x3d1, 0x3d2}; // the ruin keys reported (story 4 / 0xa / 0xd)
constexpr uint32_t kFlagFourthKeyReported = 0x3d3;        // ... at story 0x10
constexpr uint32_t kFlagSt1dCastPlaced = kFlagGroupStage + 0; // ST1D sub 1's cast spawned
constexpr uint32_t kFlagSt1dTalkAsked = kFlagGroupStage + 1;  // ... the talk asked (script 0x28)
constexpr uint32_t kFlagSt1dBossDown = kFlagGroupStage + 2;   // the elephant down
constexpr uint32_t kFlagSt1dCastShown = kFlagGroupStage + 3;
constexpr uint32_t kFlagElephantDone = 0x590;              // the elephant's chapter done
constexpr uint32_t kFlagSt49SceneA = 0x5c0, kFlagSt49SceneB = 0x5c1; // ST49: either -> script 0x18

// scripts
constexpr uint8_t kScriptSt3eTalks[4] = {0x64, 0x65, 0x66, 0x67}; // ST3E sub 6: story 2 / 4 / 0xa / 0xd
constexpr uint8_t kScriptSt1dTalk = 0x28, kScriptSt1dBossDown = 0x29, kScriptSt49Scene = 0x18;
constexpr uint8_t kStoryKeyReports[3] = {4, 0xa, 0xd};
constexpr uint8_t kStory2 = 2, kFourthKeyStory = 0x10;

// the room callback of ST3D / ST3E / ST3F (its routine is not ported)
constexpr RoutineId kSt3eRoomCallback = RoutineId::IslandRoomsSt3eRoomCallback;
// ST3E sub 0's first exit and its three versions (exe data): toward sub 1, toward sub 6 while a story event is
// pending there, and the story 0x10 one
PatchedRecord<ExitEntry> st3eFirstExitRecord{TableId::IslandRoomsSt3eFirstExit};
const DataTable<ExitEntry> st3eExitToRoom1{TableId::IslandRoomsSt3eExitToRoom1};
const DataTable<ExitEntry> st3eExitToRoom6{TableId::IslandRoomsSt3eExitToRoom6};
const DataTable<ExitEntry> st3eExitStory10{TableId::IslandRoomsSt3eExitStory10};
// ST1D sub 0's first exit (its destination room by the story)
PatchedRecord<ExitEntry> st1dFirstExit{TableId::IslandRoomsSt1dFirstExit};
// ST3E sub 2's music dip
constexpr int16_t kSt3eDipStep = 0x111;
// the elephant arena's exit light (the room's first actor): its lit state
constexpr uint8_t kExitLightLit = 2;
constexpr uint8_t kHandlerDone = 0xff;

// ---- the room helpers --------------------------------------------------------------------------------------------

bool islandRoomHelper(RoutineId fn, bool run) {
    const GameState& g = gameState();
    RoomScriptState& room = roomScriptState();
    const bool storm = g.story <= kStormLastStory;
    switch (fn) {
    case RoutineId::St1DRoomHelper: // ST1D: the island's list by the story; sub 0: three cells switched to variant 0
        if (!run) return true;
        if (storm) roomListSetup(kIslandList, stateAddress(TableId::IslandRoomsSt1dRoomsStorm), stateAddress(TableId::IslandRoomsSt1dLinksStorm));
        else roomListSetup(kIslandList, stateAddress(TableId::IslandRoomsSt1dRooms), stateAddress(TableId::IslandRoomsSt1dLinks));
        if (g.room == 0) {
            switchGridCellVariant(0x3b, 0x4f, 0);
            switchGridCellVariant(0x3f, 0x4d, 0);
            switchGridCellVariant(0x44, 0x50, 0);
        }
        room.clearHandlerWord();
        return true;
    case RoutineId::St10RoomHelper:
        // ST10: the island's list by the story; from story 1, sub 0 only: the cells (0x44, 0x25) /
        // (0x44, 0x26) to variant 0 at story 1, 1 later (the landing pad hypothesis)
        if (!run) return true;
        if (storm) roomListSetup(kIslandList, stateAddress(TableId::IslandRoomsSt10RoomsStorm), stateAddress(TableId::IslandRoomsSt10LinksStorm));
        else roomListSetup(kIslandList, stateAddress(TableId::IslandRoomsSt10Rooms), stateAddress(TableId::IslandRoomsSt10Links));
        room.clearHandlerWord();
        if (g.story != 0 && g.room == 0) {
            const uint8_t v = g.story == 1 ? 0 : 1;
            switchGridCellVariant(0x44, 0x25, v);
            switchGridCellVariant(0x44, 0x26, v);
        }
        return true;
    case RoutineId::St11RoomHelper: // ST11: the island's list by the story
        if (!run) return true;
        if (storm) roomListSetup(kIslandList, stateAddress(TableId::IslandRoomsSt11RoomsStorm), stateAddress(TableId::IslandRoomsSt11LinksStorm));
        else roomListSetup(kIslandList, stateAddress(TableId::IslandRoomsSt11Rooms), stateAddress(TableId::IslandRoomsSt11Links));
        room.clearHandlerWord();
        return true;
    case RoutineId::St3DRoomHelper:
        // ST3D: no room list; the room flags and a door cleared; by sub: 0 the door lock 1 before story 0x11
        // until its gate opens (else unlocked), the door lock 2; 1 the passage with the key reported or from story 0xe;
        // 2 / 3 their room flag
        if (!run) return true;
        room.clearHandlerWord();
        roomListSetup(kNoRoomList, 0, 0);
        clearStoryFlag(kFlagSt3dRoom2);
        clearStoryFlag(kFlagSt3dRoom3);
        clearStoryFlag(kFlagLocked);
        switch (g.room) {
        case 0:
            if (g.story < 0x11 && !storyFlag(kFlagSt3dGateOpen)) setStoryFlag(kFlagDoorLock1);
            else clearStoryFlag(kFlagDoorLock1);
            setStoryFlag(kFlagDoorLock2);
            break;
        case 1:
            if (storyFlag(kFlagSt3dKeyReported) || g.story >= 0xe) setStoryFlag(kFlagSt3dPassage);
            break;
        case 2: setStoryFlag(kFlagSt3dRoom2); break;
        case 3: setStoryFlag(kFlagSt3dRoom3); break;
        default: break;
        }
        return true;
    case RoutineId::St3ERoomHelper:
        // ST3E: no room list; the room flags of subs 4 / 5; the music channels dipped in sub 2, else
        // restored; the door lock 0 cleared
        if (!run) return true;
        room.clearHandlerWord();
        roomListSetup(kNoRoomList, 0, 0);
        clearStoryFlag(kFlagSt3eRoom4);
        clearStoryFlag(kFlagSt3eRoom5);
        if (g.room == 4) setStoryFlag(kFlagSt3eRoom4);
        else if (g.room == 5) setStoryFlag(kFlagSt3eRoom5);
        clearStoryFlag(kFlagSt3eRoomOther);
        if (g.room == 2) soundFadeDip(kMusicFades, kSt3eDipStep, 0, 0);
        else soundFadeRestore(kMusicFades, kSt3eDipStep, 0);
        clearStoryFlag(kFlagLocked);
        return true;
    case RoutineId::St3FRoomHelper: // ST3F: no room list, the door lock 0 cleared
        if (!run) return true;
        room.clearHandlerWord();
        roomListSetup(kNoRoomList, 0, 0);
        clearStoryFlag(kFlagLocked);
        return true;
    default: return false;
    }
}

// ---- the room functions' parts ------------------------------------------------------------------------------------

// ST1D (exe 0x5d9570, after the encounter slots): sub 0's first exit leads to sub 2 after story 1, else sub 1
void st1dExitRoom() { st1dFirstExit->destRoom = gameState().story > 1 ? 2u : 1u; }
// ST3E (exe 0x5dcf00, after the encounter slots): sub 0's first exit by the story: toward sub 6 while the story's
// ST3E event is pending (story 2 before its event, 4 / 0xa / 0xd with a key reported), the story 0x10 version with the
// fourth key, else toward sub 1
void st3eFirstExit() {
    const uint8_t story = gameState().story;
    const DataTable<ExitEntry>* src = &st3eExitToRoom1;
    if ((story == kStory2 && !storyFlag(kFlagSt3eStory2Event)) ||
        (story == kStoryKeyReports[0] && storyFlag(kFlagKeysReported[0])) ||
        (story == kStoryKeyReports[1] && storyFlag(kFlagKeysReported[1])) ||
        (story == kStoryKeyReports[2] && storyFlag(kFlagKeysReported[2])))
        src = &st3eExitToRoom6;
    else if (story == kFourthKeyStory && storyFlag(kFlagFourthKeyReported))
        src = &st3eExitStory10;
    *st3eFirstExitRecord = src->get(); // the exe's six dword copies of the entry
}
// the room callback of ST3D (sub 0), ST3E (subs 0..2), ST3F (sub 0) (after the weapon mode)
void st3dRoomCallback() { stage().roomCallback = gameState().room == 0 ? kSt3eRoomCallback : RoutineId::None; }
void st3eRoomCallback() { stage().roomCallback = gameState().room < 3 ? kSt3eRoomCallback : RoutineId::None; }
void st3fRoomCallback() { stage().roomCallback = gameState().room < 1 ? kSt3eRoomCallback : RoutineId::None; }

// ---- the room scripts --------------------------------------------------------------------------------------------

// the once-per-visit handlers: the records spawned when the handler's step is 0, then the step + 1
bool spawnOnce(bool run, uint32_t list, uint32_t n, uint32_t list2 = 0, uint32_t n2 = 0) {
    RoomScriptState& room = roomScriptState();
    if (!run || room.handlerStep != 0) return true;
    spawnRecords(list, n);
    if (list2) spawnRecords(list2, n2);
    ++room.handlerStep;
    return true;
}

bool islandRoomScriptHandler(RoutineId fn, bool run) {
    const GameState& g = gameState();
    RoomScriptState& room = roomScriptState();
    const uint8_t story = g.story;
    switch (fn) {
    // ST3D (table 0x8cc2e8 by story; the sub tables of stories 5..0xa 0x8cc2a8, 0xb..0xd 0x8cc2b8, 0xe..0x10
    // 0x8cc2c8): the room's records, once per visit
    case RoutineId::St3DRoomHandler: // stories 0..1 sub 4 / 2..4 sub 0
    case RoutineId::St3DStory5OnSub0RoomHandler:
    case RoutineId::St3DStory5OnSub0RoomHandlerB:
    case RoutineId::St3DStory5OnSub0RoomHandlerC: return spawnOnce(run, stateAddress(TableId::IslandRoomsSt3DLateSub0Spawns), 1); // stories 5.. sub 0
    case RoutineId::St3DRoomHandlerA: // stories 0..1 sub 5 / 2..4 sub 1
    case RoutineId::St3DStory5OnSub1RoomHandler:
    case RoutineId::St3DStory5OnSub1RoomHandlerB:
    case RoutineId::St3DStory5OnSub1RoomHandlerC: return spawnOnce(run, stateAddress(TableId::IslandRoomsSt3DLateSub1Spawns), 2); // stories 5.. sub 1
    case RoutineId::St3DRoomHandlerB: return spawnOnce(run, stateAddress(TableId::IslandRoomsSt3DSub3Spawns), 1); // stories 0..1 sub 7 / 2..4 sub 3
    case RoutineId::St3DStory5ToASub3RoomHandler: return spawnOnce(run, stateAddress(TableId::IslandRoomsSt3DMiddleSub3Spawns), 1); // stories 5..0xa sub 3
    case RoutineId::St3DStoryBToDSub2RoomHandler: return spawnOnce(run, stateAddress(TableId::IslandRoomsSt3DLateSub2Spawns), 1); // stories 0xb..0xd sub 2
    // ST3F (table 0x8cc4f0 by story): the stage's person and the story's visitors, once per visit
    case RoutineId::St3FSub1RoomHandler: return spawnOnce(run, stateAddress(TableId::IslandRoomsSt3FSub1Spawns), 1, stateAddress(TableId::IslandRoomsSt3FSub1MoreSpawns), 1);  // sub 1
    case RoutineId::St3FStory2OnSub3RoomHandler: return spawnOnce(run, stateAddress(TableId::IslandRoomsSt3FSub1Spawns), 1, stateAddress(TableId::IslandRoomsSt3FSub3MoreSpawns), 1);
        // sub 3 (stories 2..)
    case RoutineId::St3FStoryBToDSub1RoomHandler: return spawnOnce(run, stateAddress(TableId::IslandRoomsSt3FSub1Spawns), 1, stateAddress(TableId::IslandRoomsSt3FMiddleSub1MoreSpawns), 1);
        // stories 0xb..0xd sub 1
    case RoutineId::St3FStoryETo10Sub1RoomHandler: return spawnOnce(run, stateAddress(TableId::IslandRoomsSt3FSub1Spawns), 1, stateAddress(TableId::IslandRoomsSt3FLateSub1MoreSpawns), 3);
        // stories 0xe..0x10 sub 1
    case RoutineId::St3FStory11OnSub1RoomHandler: return spawnOnce(run, stateAddress(TableId::IslandRoomsSt3FSub1Spawns), 1, stateAddress(TableId::IslandRoomsSt3FLastSub1MoreSpawns), 3);
        // stories 0x11.. sub 1
    case RoutineId::St3FRoomHandler: return spawnOnce(run, stateAddress(TableId::IslandRoomsSt3FSpawns), 2);
    // ST3E (table 0x8cc3c8 by sub)
    case RoutineId::St3ESub0RoomHandler:
        // sub 0: story 2 after its event and before the talk -> script 0x64 (the step stays 0), else on
        if (!run || room.handlerStep != 0) return true;
        if (story == kStory2 && storyFlag(kFlagSt3eStory2Event) && !storyFlag(kFlagSt3eStory2Talk)) {
            scriptRun(kScriptSt3eTalks[0]);
            return true;
        }
        ++room.handlerStep;
        return true;
    case RoutineId::St3ESub1RoomHandler: return spawnOnce(run, stateAddress(TableId::IslandRoomsSt3ESub1Spawns), 3); // sub 1
    case RoutineId::St3ESub2RoomHandler: // sub 2: below story 0x11 the visitors of the story (from 5, 0xb, 0xe), once
        if (!run || room.handlerStep != 0) return true;
        if (story < 0x11) {
            if (story >= 5) spawnRecordTracked(stateAddress(TableId::IslandRoomsSt3ESub2FirstVisitor));
            if (story >= 0xb) spawnRecordTracked(stateAddress(TableId::IslandRoomsSt3ESub2SecondVisitor));
            if (story >= 0xe) spawnRecordTracked(stateAddress(TableId::IslandRoomsSt3ESub2ThirdVisitor));
        }
        ++room.handlerStep;
        return true;
    case RoutineId::St3ESub3RoomHandler: // sub 3: story 2 before its event
        if (!run || room.handlerStep != 0) return true;
        if (story == kStory2 && !storyFlag(kFlagSt3eStory2Event)) spawnRecords(stateAddress(TableId::IslandRoomsSt3ESub3Spawns), 1);
        ++room.handlerStep;
        return true;
    case RoutineId::St3ESub4RoomHandler: // sub 4: story 0xb..0xd
        if (!run || room.handlerStep != 0) return true;
        if (story >= 0xb && story < 0xe) spawnRecords(stateAddress(TableId::IslandRoomsSt3ESub4Spawns), 1);
        ++room.handlerStep;
        return true;
    case RoutineId::St3ESub5RoomHandler: // sub 5: story 2..4 / 5..0xa
        if (!run || room.handlerStep != 0) return true;
        if (story >= 2 && story < 5) spawnRecords(stateAddress(TableId::IslandRoomsSt3ESub5EarlySpawns), 1);
        else if (story >= 5 && story < 0xb) spawnRecords(stateAddress(TableId::IslandRoomsSt3ESub5LaterSpawns), 1);
        ++room.handlerStep;
        return true;
    case RoutineId::St3ESub6RoomHandler:
        // sub 6, every tick (no step): the story's talk -- story 2 before its event, 4 / 0xa / 0xd with a
        // key reported
        if (!run) return true;
        if (story == kStory2) {
            if (!storyFlag(kFlagSt3eStory2Event)) scriptRun(kScriptSt3eTalks[0]);
            return true;
        }
        for (size_t k = 0; k < 3; ++k) {
            if (story != kStoryKeyReports[k]) continue;
            if (storyFlag(kFlagKeysReported[k])) scriptRun(kScriptSt3eTalks[k + 1]);
            return true;
        }
        return true;
    // ST1D (table 0x8c7f0c by story)
    case RoutineId::St1DStory1Sub1RoomHandler:
        // story 1 sub 1: step 0 spawns the cast (and its flags); 1: the elephant's chapter done -> 2, else
        // the talk asked -> script 0x28, done
        if (!run) return true;
        if (room.handlerStep == 0) {
            spawnRecords(stateAddress(TableId::IslandRoomsSt1DCastSpawns), 1);
            setStoryFlag(kFlagSt1dCastPlaced);
            spawnRecords(stateAddress(TableId::IslandRoomsSt1DCastMoreSpawns), 1);
            spawnRecords(stateAddress(TableId::IslandRoomsSt1DCastGroupSpawns), 3);
            spawnRecords(stateAddress(TableId::IslandRoomsSt1DCastLastSpawns), 1);
            setStoryFlag(kFlagSt1dCastShown);
            ++room.handlerStep;
        } else if (room.handlerStep == 1) {
            if (storyFlag(kFlagElephantDone)) {
                room.handlerStep = 2;
            } else if (storyFlag(kFlagSt1dTalkAsked)) {
                scriptRun(kScriptSt1dTalk);
                room.handlerStep = kHandlerDone;
            }
        }
        return true;
    case RoutineId::St1DStory1Sub2RoomHandler:
        // story 1 sub 2 (the elephant's arena): step 0 -> 1; 1: the boss down -> the exit light (the room's
        // first actor) lit, script 0x29, done
        if (!run) return true;
        if (room.handlerStep == 0) {
            room.handlerStep = 1;
        } else if (room.handlerStep == 1 && storyFlag(kFlagSt1dBossDown)) {
            actorHeaderAt(room.actors[0]).state = kExitLightLit;
            scriptRun(kScriptSt1dBossDown);
            room.handlerStep = kHandlerDone;
        }
        return true;
    // ST49 (the stage case, every tick; no step, no door test)
    case RoutineId::St49RoomCase:
        if (run && (storyFlag(kFlagSt49SceneB) || storyFlag(kFlagSt49SceneA))) scriptRun(kScriptSt49Scene);
        return true;
    default: return false;
    }
}

} // namespace

void registerIslandRooms(ContentRegistry& r) {
    r.roomHelpers.push_back(&islandRoomHelper);
    r.roomHandlers.push_back(&islandRoomScriptHandler);
    r.roomFunctions[0x1d] = {.afterEncounterSlots = &st1dExitRoom};
    r.roomFunctions[0x10] = {}; // the template; its sky's story branch is game/stage_setup roomSkyGradientTable's
    r.roomFunctions[0x11] = {};
    r.roomFunctions[0x3d] = {.tail = &st3dRoomCallback};
    r.roomFunctions[0x3e] = {.afterEncounterSlots = &st3eFirstExit, .tail = &st3eRoomCallback};
    r.roomFunctions[0x3f] = {.tail = &st3fRoomCallback};
}

} // namespace od::content
