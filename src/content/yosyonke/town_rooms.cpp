// The town's rooms in the stage / room setup and the room scripts (see town_rooms.hpp), one routine per exe routine.
#include "content/yosyonke/town_rooms.hpp"

#include "content/yosyonke/dev_room.hpp"

#include "game/loaded_data.hpp"
#include "game/patched_data.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/audio_logic.hpp"
#include "game/breakable_walls.hpp"
#include "game/encounter.hpp"
#include "game/game_state.hpp"
#include "game/port_gap.hpp"
#include "game/room_scripts.hpp"
#include "game/room_uv_animation.hpp"
#include "game/script_engine.hpp"
#include "game/spawn.hpp"
#include "game/stage_state.hpp"
#include "game/story_flags.hpp"
#include "game/transition_state.hpp"
#include "game/visited_rooms.hpp"
#include "game/vram_moves.hpp"

namespace od::content {

using namespace od::port;

namespace {

constexpr const char* kTownRoomsPort = "town rooms"; // the area of its port-gap errors

// ---- the town's room list (list 0) and its stages' constants --------------------------------------------------------
constexpr int8_t kTownList = 0;

// story flags
constexpr uint32_t kFlagFlutterRoomPatched = 0x12f; // ST05 sub 1 shows its patched texture
constexpr uint32_t kFlagFlutterLanded = 0x5e4;      // the Flutter came down in ST08 (script 0x55 ran)
constexpr uint32_t kFlagRollAtLab = 0x5e1;          // Roll waits at Joe's lab (ST08's door stays open)
constexpr uint32_t kFlagJunkShopVisited = 0x5c1;    // the junk shop's scene ran (script 0x4d)
constexpr uint32_t kFlagRollRanOff = 0x5c2;         // script 0x4e ran (Roll runs off to Joe)
constexpr uint32_t kFlagSt09SceneDone = 0x5b0;      // ST09's script 0x72 ran
constexpr uint32_t kFlagDoorLock1 = kFlagLocked + 1;
// the Flutter's cabin (ST04)
constexpr uint32_t kFlagCabinArrived = 0x5f0;   // the first arrival's scene ran (script 0x4c)
constexpr uint32_t kFlagCabinPending = 0xd0;    // one of the cabin's events below is pending (exe 0x57bc40 keeps it)
constexpr uint32_t kFlagCabinScenesSeen = 0xd1; // set by the first arrival: the cabin's cast settled
constexpr uint32_t kFlagCabinEvent = 0xd2;      // the cabin's event, pending until 0xd4..0xd6 are all set
constexpr uint32_t kFlagCabinEventParts[3] = {0xd4, 0xd5, 0xd6};
constexpr uint32_t kFlagCabinExtraEvents[3] = {0x43e, 0x43f, 0x440};
constexpr uint32_t kFlagCabinGuestA = 0xd3;      // the cabin's second cast (with the tool kit tier 0, until 0xde)
constexpr uint32_t kFlagCabinGuestAOff = 0xde;
constexpr uint32_t kFlagCabinGuestB = 0x127;     // ... (with the tool kit tier 2, until 0x128)
constexpr uint32_t kFlagCabinGuestBDone = 0x128;
constexpr uint32_t kFlagDockNotice = 0xdf;       // the dock counter's notice asked (then 0x134: given)
constexpr uint32_t kFlagDockNoticeGiven = 0x134;
constexpr uint8_t kCabinStory = 2;               // the cabin's cast by its flags from this story

// scripts
constexpr uint8_t kScriptCabinArrival = 0x4c;
constexpr uint8_t kScriptFlutterLanding = 0x55;
constexpr uint8_t kScriptRollRunsOff = 0x4e;
constexpr uint8_t kScriptSt09Scene = 0x72;

// sounds
constexpr uint16_t kSoundShopBell = 0x16;

// the dock counter the cabin's notice sets
constexpr size_t kCabinDockCounter = 0;
constexpr uint8_t kDockNoticeCount = 1;

// ---- the room helpers --------------------------------------------------------------------------------------------

// the Flutter's door (ST04 sub 1's exit entry, which the door's helper redirects) and the places it arrives at by the
// island docked at (exe data)
PatchedRecord<ExitEntry> flutterDoor{TableId::TownRoomsFlutterDoor};
const DataTable<RecordTable<RoomPlace, 10, 8>> dockPlaces{TableId::TownRoomsDockPlaces};

// exe 0x5d6a80 (ST04): the Flutter's door (ST04 sub 1's exit entry) toward the island docked at (and the story): the
// arrival place [kDockPlaces + place * 8], the stage and the sub-room
void flutterDoorExit() {
    const GameState& g = gameState();
    uint32_t place = 0;
    uint8_t stage = 0, room = 0;
    switch (g.flutterDock) {
    case 0: place = 0; stage = 0x08; break;
    case 1:
        place = g.story < 2 ? 1 : 7;
        stage = 0x10;
        break;
    case 2: place = 2; stage = 0x3f; break;
    case 3: place = 3; stage = 0x24; break;
    case 4: place = 4; stage = 0x17; break;
    case 5:
        if (g.story < 0x11) {
            place = 5;
            stage = 0x23;
        } else {
            place = 8;
            stage = 0x1f;
        }
        break;
    case 6:
        if (g.story < 0xd) {
            place = 6;
            stage = 0x48;
        } else {
            place = 9;
            stage = 0x3c;
            room = 3;
        }
        break;
    default: break;
    }
    ExitEntry& door = *flutterDoor;
    door.destination = dockPlaces[place];
    door.destStage = stage;
    door.destRoom = room;
}

// the town's room helpers
bool townRoomHelper(RoutineId fn, bool run) {
    const GameState& g = gameState();
    RoomScriptState& room = roomScriptState();
    switch (fn) {
    case RoutineId::St0CRoomHelper: // ST0C: the handler word cleared, the town's list
    case RoutineId::St0DRoomHelper: // ST0D
        if (!run) return true;
        room.clearHandlerWord();
        if (fn == RoutineId::St0CRoomHelper) roomListSetup(kTownList, stateAddress(TableId::TownRoomsSt0cRooms), stateAddress(TableId::TownRoomsSt0cLinks));
        else roomListSetup(kTownList, stateAddress(TableId::TownRoomsSt0dRooms), stateAddress(TableId::TownRoomsSt0dLinks));
        return true;
    case RoutineId::St05RoomHelper:
        // ST05: no room list (the handler word cleared after it); sub 1 with its patch flag: the VRAM copy
        // (0x2c0, 0x100) 0x10 x 0x60 -> (0x140, 0x140) (exe 0x57c320); the doors unlocked
        if (!run) return true;
        roomListSetup(kNoRoomList, 0, 0);
        room.clearHandlerWord();
        if (g.room == 1 && storyFlag(kFlagFlutterRoomPatched)) vramMove(0x2c0, 0x100, 0x10, 0x60, 0x140, 0x140);
        clearLockFlags(kLockFlags);
        return true;
    case RoutineId::St0BRoomHelper: // ST0B: the town's list; sub 1 from story 0x10 opens the cell (0x3f, 0x40)
        if (!run) return true;
        room.clearHandlerWord();
        roomListSetup(kTownList, stateAddress(TableId::TownRoomsSt0bRooms), stateAddress(TableId::TownRoomsSt0bLinks));
        if (g.room == 1 && g.story >= 0x10) switchGridCellVariant(0x3f, 0x40, 1);
        return true;
    case RoutineId::St0ERoomHelper:
        // ST0E: sub 2 no room list, else the town's list; sub 1 from story 0x10 opens the cell (0x29, 0x45)
        if (!run) return true;
        room.clearHandlerWord();
        if (g.room == 2) roomListSetup(kNoRoomList, 0, 0);
        else roomListSetup(kTownList, stateAddress(TableId::TownRoomsSt0eRooms), stateAddress(TableId::TownRoomsSt0eLinks));
        if (g.room == 1 && g.story >= 0x10) switchGridCellVariant(0x29, 0x45, 1);
        return true;
    case RoutineId::St08RoomHelper: // ST08: two doors unlocked, the town's list; sub 1 from story 1 one cell switched
        if (!run) return true;
        clearLockFlags(2);
        roomListSetup(kTownList, stateAddress(TableId::TownRoomsSt08Rooms), stateAddress(TableId::TownRoomsSt08Links));
        room.clearHandlerWord();
        if (g.room == 1 && g.story >= 1) switchGridCellVariant(0x3f, 0x40, 1);
        return true;
    case RoutineId::St09RoomHelper:
        // ST09: two doors unlocked, the town's list; past story 0xe (exe 0x57cb40) the repaired bridge
        // segments -- cell (0x47, 0x30), from story 0x10 also (0x49, 0x32) / (0x4a, 0x31) -- and their texture patch,
        // the VRAM copy (0x2f0, 0x1e0) w x 0x20 -> (0x32e, 0xc0), w = 4 before story 0x10 else 10
        if (!run) return true;
        clearLockFlags(2);
        room.clearHandlerWord();
        roomListSetup(kTownList, stateAddress(TableId::TownRoomsSt09Rooms), stateAddress(TableId::TownRoomsSt09Links));
        if (g.story > 0xe) {
            switchGridCellVariant(0x47, 0x30, 1);
            if (g.story >= 0x10) {
                switchGridCellVariant(0x49, 0x32, 1);
                switchGridCellVariant(0x4a, 0x31, 1);
            }
            vramMove(0x2f0, 0x1e0, g.story < 0x10 ? 4 : 10, 0x20, 0x32e, 0xc0);
        }
        return true;
    case RoutineId::St0ARoomHelper: // ST0A (the junk shop): the town's list; the shop bell unless sub 0 at story 0
        if (!run) return true;
        room.clearHandlerWord();
        roomListSetup(kTownList, stateAddress(TableId::TownRoomsSt0aRooms), stateAddress(TableId::TownRoomsSt0aLinks));
        if (g.room != 0 || g.story != 0) soundPlayFlatForced(kSoundShopBell);
        return true;
    case RoutineId::St04RoomHelper: // ST04 / ST06: no room list, the doors unlocked
        if (!run) return true;
        roomListSetup(kNoRoomList, 0, 0);
        room.clearHandlerWord();
        clearLockFlags(kLockFlags);
        return true;
    case RoutineId::St04RoomHelperA: // ST04: the Flutter's door
        if (run) flutterDoorExit();
        return true;
    case RoutineId::St07RoomHelper:
        // ST07 (the Flutter's hangar): no room list (the handler word cleared first), the doors unlocked
        if (!run) return true;
        room.clearHandlerWord();
        roomListSetup(kNoRoomList, 0, 0);
        clearLockFlags(kLockFlags);
        return true;
    case RoutineId::St47RoomHelper: // ST47 (where the first ruin's device leads): the town's list, two doors unlocked
        if (!run) return true;
        room.clearHandlerWord();
        roomListSetup(kTownList, stateAddress(TableId::TownRoomsSt47Rooms), stateAddress(TableId::TownRoomsSt47Links));
        clearLockFlags(2);
        return true;
    default: return false;
    }
}

// ---- the room functions' parts ------------------------------------------------------------------------------------

// ST08 (exe 0x5d70b0, after the weapon mode): the room callback in sub 0 only
void st08RoomCallback() { stage().roomCallback = gameState().room != 0 ? RoutineId::None : kTownRoomCallback; }
// ST0A (exe 0x5d73d0): sub 2's UV-animation list (exe 0x5f5bd0)
void st0aUvAnimations() {
    if (gameState().room == 2) roomUvAnimationSetup(stateAddress(TableId::TownRoomsSt0aUvAnimList), stateAddress(TableId::TownRoomsSt0aUvAnimTable));
}
// ST0B (exe 0x5d7580, after the weapon mode): the room callback in sub 0 only
void st0bRoomCallback() { stage().roomCallback = gameState().room == 0 ? kTownRoomCallback : RoutineId::None; }
// ST0E's event story (no field encounters then; game/stage_music plays the event's music)
constexpr uint8_t kSt0eEventStory = 0xf;
// ST0E (exe 0x5d7960): the field encounters (and the registered cells) except at the event story
int32_t st0eEncounterKinds() {
    constexpr uint32_t kSt0eKinds = (1u << uint32_t(EncounterKind::Field)) | kEncounterRefreshCells;
    return gameState().story != kSt0eEventStory ? int32_t(kSt0eKinds) : -1;
}

// ---- the room scripts --------------------------------------------------------------------------------------------

// exe 0x57c680(record): one pool-0x20 actor from a record without a room script's flag merge or actor slot: the flags,
// the type and parameter words, the position, the yaw, the rotation x / z cleared
void placeHangarActor(uint32_t record) {
    const uint32_t a = allocActor20();
    if (!a) return;
    const SpawnRecord r = dataRecord<SpawnRecord>(record, SpawnRecord::kBytes);
    AnimatedActor& actor = animatedActorAt(a);
    actor.flags = r.flags;
    actor.setTypeWord(r.typeWord);
    actor.setParamsWord(r.paramsWord);
    actor.pos.x = int32_t(uint32_t(int32_t(r.point.x)) << 16);
    actor.pos.y = int32_t(uint32_t(int32_t(r.point.y)) << 16);
    actor.pos.z = int32_t(uint32_t(int32_t(r.point.z)) << 16);
    actor.rotation.y = r.yaw;
    actor.rotation.z = 0;
    actor.rotation.x = 0;
}

// the tool kit tier 2's guest flag: set until its done flag, cleared at the other tiers
void cabinGuestBFlag(int8_t tier) {
    if (tier == int8_t(BalanceTier::Low)) {
        if (!storyFlag(kFlagCabinGuestBDone)) setStoryFlag(kFlagCabinGuestB);
        else if (storyFlag(kFlagCabinGuestB)) clearStoryFlag(kFlagCabinGuestB);
    } else if (storyFlag(kFlagCabinGuestB)) {
        clearStoryFlag(kFlagCabinGuestB);
    }
}

// exe 0x57be10 (ST04 subs 0 / 3, stories 1..): the dock notice; once per visit the cabin's cast (the pair, or the
// guest by the story / the tier flags); Roll's "Development Room" -> script 0x30
void cabinVisit() {
    const GameState& g = gameState();
    RoomScriptState& room = roomScriptState();
    if (storyFlag(kFlagDockNotice) && !storyFlag(kFlagDockNoticeGiven)) {
        setStoryFlag(kFlagDockNoticeGiven);
        gameState().dockCounters[kCabinDockCounter] = kDockNoticeCount;
    }
    if (room.handlerByte3 == 0) {
        bool pair = true;
        const uint8_t story = g.story;
        if (story == kCabinStory) {
            pair = storyFlag(kFlagSt3eStory2Event);
        } else if (story > kCabinStory) {
            if (!storyFlag(kFlagCabinScenesSeen)) {
                const int8_t tier = g.toolKitTier;
                if (tier == int8_t(BalanceTier::High)) {
                    if (!storyFlag(kFlagCabinGuestAOff)) setStoryFlag(kFlagCabinGuestA);
                } else {
                    if (storyFlag(kFlagCabinGuestA)) clearStoryFlag(kFlagCabinGuestA);
                    cabinGuestBFlag(tier);
                }
            }
            pair = !(storyFlag(kFlagCabinGuestA) || storyFlag(kFlagCabinGuestB));
        }
        if (pair) spawnRecords(stateAddress(TableId::TownRoomsCabinPair), 2);
        else spawnRecords(stateAddress(TableId::TownRoomsCabinGuest), 1);
        ++room.handlerByte3;
    }
    if (storyFlag(kFlagDevRoomAsked)) {
        clearStoryFlag(kFlagDevRoomAsked);
        scriptStart(kScriptDevRoom);
    }
}

// exe 0x57bc40 (ST04, every tick after the handler): the pending flag follows the cabin's events
void cabinPendingFlag() {
    const bool partsDone = storyFlag(kFlagCabinEventParts[0]) && storyFlag(kFlagCabinEventParts[1]) &&
                           storyFlag(kFlagCabinEventParts[2]);
    const bool extras = storyFlag(kFlagCabinExtraEvents[0]) || storyFlag(kFlagCabinExtraEvents[1]) ||
                        storyFlag(kFlagCabinExtraEvents[2]);
    if (!storyFlag(kFlagCabinPending)) {
        if (storyFlag(kFlagCabinEvent) && !partsDone) {
            setStoryFlag(kFlagCabinPending);
            return;
        }
        if (extras) setStoryFlag(kFlagCabinPending);
        return;
    }
    if (partsDone) clearStoryFlag(kFlagCabinEvent);
    if (!storyFlag(kFlagCabinEvent) && !extras) clearStoryFlag(kFlagCabinPending);
}

// the once-per-visit handlers: the records spawned when the handler's step is 0, then the step + 1
bool spawnOnce(bool run, uint32_t list, uint32_t n, uint32_t list2 = 0, uint32_t n2 = 0) {
    RoomScriptState& room = roomScriptState();
    if (!run || room.handlerStep != 0) return true;
    spawnRecords(list, n);
    if (list2) spawnRecords(list2, n2);
    ++room.handlerStep;
    return true;
}

// the town's room-script handlers
bool townRoomScriptHandler(RoutineId fn, bool run) {
    const GameState& g = gameState();
    RoomScriptState& room = roomScriptState();
    switch (fn) {
    // ST0A (the junk shop, table 0x8c5818): the clerk / the customers once per visit
    case RoutineId::St0ARoomHandler: case RoutineId::St0ARoomHandlerG: return spawnOnce(run, stateAddress(TableId::TownRoomsJunkShopClerkSpawns), 1);
    case RoutineId::St0ARoomHandlerA: case RoutineId::St0ARoomHandlerH: return spawnOnce(run, stateAddress(TableId::TownRoomsJunkShopCustomerSpawns), 1, stateAddress(TableId::TownRoomsJunkShopCustomerMoreSpawns), 3);
    case RoutineId::St0ARoomHandlerB: case RoutineId::St0ARoomHandlerI: return spawnOnce(run, stateAddress(TableId::TownRoomsJunkShopSecondCustomerSpawns), 2, stateAddress(TableId::TownRoomCasesSt0ASub4MoreSpawns), 3);
    case RoutineId::St0ARoomHandlerC: return spawnOnce(run, stateAddress(TableId::TownRoomsJunkShopCustomerSpawns), 2, stateAddress(TableId::TownRoomsJunkShopCustomerMoreSpawns), 3);
    case RoutineId::St0ARoomHandlerD: return spawnOnce(run, stateAddress(TableId::TownRoomsJunkShopThirdCustomerSpawns), 1, stateAddress(TableId::TownRoomCasesSt0ASub4MoreSpawns), 3);
    case RoutineId::St0AStory1Sub3RoomHandler: return spawnOnce(run, stateAddress(TableId::TownRoomCasesSt0ASub3Spawns), 1); // story 1 sub 3
    // ST08
    case RoutineId::St08Story1Sub0RoomHandler: return spawnOnce(run, stateAddress(TableId::TownRoomsFlutterCastSpawns), 4);
        // story 1 sub 0: the Flutter and its cast
    case RoutineId::St08Story1Sub1RoomHandler: return spawnOnce(run, stateAddress(TableId::TownRoomsJoesLabSpawns), 1); // story 1 sub 1
    case RoutineId::St04Story0Sub0RoomHandler:
        // story 0 sub 0 (after the crash landing): the Flutter and, the first time, script 0x55 (it comes
        // down; then ST04); later: the door lock unless Roll waits at the lab, the Flutter and its cast
        if (!run || room.handlerStep != 0) return true;
        if (!storyFlag(kFlagFlutterLanded)) {
            spawnRecords(stateAddress(TableId::TownRoomsFlutterCastSpawns), 1);
            setStoryFlag(kFlagFlutterLanded);
            scriptRun(kScriptFlutterLanding);
            return true;
        }
        if (!storyFlag(kFlagRollAtLab)) setStoryFlag(kFlagDoorLock1);
        spawnRecords(stateAddress(TableId::TownRoomsFlutterCastSpawns), 5);
        ++room.handlerStep;
        return true;
    // ST04 (the Flutter's cabin, table 0x8c5054 by story)
    case RoutineId::St04Story0Sub0RoomHandlerA:
        // story 0 sub 0: the first arrival: its flags, script 0x4c; later visits run exe 0x57be10's other
        // path (not ported)
        if (storyFlag(kFlagCabinArrived)) {
            if (run)
                notPortedAt(kTownRoomsPort, "ST04 room handler", routineStateValue(RoutineId::CabinLaterVisitsHandler));
            return false;
        }
        if (!run) return true;
        for (uint32_t f : {kFlagCabinArrived, kFlagCabinPending, kFlagCabinScenesSeen, kFlagCabinEvent})
            setStoryFlag(f);
        scriptRun(kScriptCabinArrival);
        return true;
    case RoutineId::St04RoomHandler: // subs 0 / 3, stories 1..: exe 0x57be10
    case RoutineId::St04RoomHandlerA:
        if (run) cabinVisit();
        return true;
    case RoutineId::St04Story2OnSub1RoomHandler: // sub 1 (stories 2..): the guests' flags by the tool kit tier
        if (!run) return true;
        if (g.toolKitTier != int8_t(BalanceTier::High) && storyFlag(kFlagCabinGuestA)) clearStoryFlag(kFlagCabinGuestA);
        cabinGuestBFlag(g.toolKitTier);
        return true;
    case RoutineId::St04Sub1RoomHandler: // sub 1: the door lock while the Flutter is docked at the island 1
        if (!run) return true;
        if (g.flutterDock == 1) setStoryFlag(kFlagDoorLock1);
        else clearStoryFlag(kFlagDoorLock1);
        return true;
    // ST07 (the Flutter's hangar, table 0x8c5200)
    case RoutineId::St07Sub1RoomHandler:
        // sub 1: from story 1 on, once per visit, the dropship (record 0x8c51e8: pool 0x20 type 0x6d at (0,
        // 0x98, 0xb0) yaw 0x400). Story 0: nothing (hypothesis: the craft is handed over by then)
        if (!run) return true;
        if (g.story > 0 && room.handlerByte1 == 0) {
            placeHangarActor(stateAddress(TableId::TownRoomsHangarDropshipRecord));
            ++room.handlerByte1;
        }
        return true;
    // ST09 (table 0x8c5554 by story): the walkers of the street
    case RoutineId::St09Story0Sub0RoomHandler:
        // story 0 sub 0: after the junk shop the walkers and, once, script 0x4e; the door lock
        if (!run || room.handlerStep != 0) return true;
        if (storyFlag(kFlagJunkShopVisited)) {
            spawnRecords(stateAddress(TableId::TownRoomsSt09WalkerSpawns), 2);
            if (!storyFlag(kFlagRollRanOff)) {
                scriptRun(kScriptRollRunsOff);
                setStoryFlag(kFlagRollRanOff);
            }
        }
        setStoryFlag(kFlagDoorLock1);
        ++room.handlerStep;
        return true;
    case RoutineId::St09Story1Sub0RoomHandler: // story 1 sub 0: the walkers, the door lock
        if (!run || room.handlerStep != 0) return true;
        spawnRecords(stateAddress(TableId::TownRoomsSt09WalkerSpawns), 2);
        setStoryFlag(kFlagDoorLock1);
        ++room.handlerStep;
        return true;
    case RoutineId::St09RoomHandler:
    case RoutineId::St09RoomHandlerA: return spawnOnce(run, stateAddress(TableId::TownRoomsSt09WalkerSpawns), 2);
    case RoutineId::St09RoomHandlerB: return spawnOnce(run, stateAddress(TableId::TownRoomsSt09OtherWalkerSpawns), 2);
    case RoutineId::St09RoomHandlerC: // the walkers, once script 0x72
        if (!run || room.handlerStep != 0) return true;
        spawnRecords(stateAddress(TableId::TownRoomsSt09WalkerSpawns), 2);
        if (!storyFlag(kFlagSt09SceneDone)) {
            scriptRun(kScriptSt09Scene);
            setStoryFlag(kFlagSt09SceneDone);
        }
        ++room.handlerStep;
        return true;
    default: return false;
    }
}

} // namespace

void registerYosyonkeRooms(ContentRegistry& r) {
    r.roomHelpers.push_back(&townRoomHelper);
    r.roomHandlers.push_back(&townRoomScriptHandler);
    r.roomFunctions[0x08] = {.afterEncounterSlots = &st08RoomCallback};
    r.roomFunctions[0x0a] = {.afterEncounterSlots = &st0aUvAnimations};
    r.roomFunctions[0x0b] = {.tail = &st0bRoomCallback};
    r.stageEncounterKinds[0x0e] = &st0eEncounterKinds;
    r.roomCaseTails[0x04] = &cabinPendingFlag;
}

} // namespace od::content
