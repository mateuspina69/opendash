// Nino Island's rooms in the stage / room setup and the room scripts (see nino_rooms.hpp), one routine per exe routine.
#include "content/nino/nino_rooms.hpp"

#include "content/nino/st36_rooms.hpp"
#include "content/nino/st37_rooms.hpp"
#include "content/yosyonke/dev_room.hpp"
#include "game/audio_logic.hpp"
#include "game/breakable_walls.hpp"
#include "game/game_state.hpp"
#include "game/player/player_weapons.hpp"
#include "game/room_scripts.hpp"
#include "game/script_engine.hpp"
#include "game/stage_state.hpp"
#include "game/story_flags.hpp"
#include "game/visited_rooms.hpp"

namespace od::content {

using namespace od::port;

namespace {

// ---- the island's room list (list 4) --------------------------------------------------------------------------------
constexpr int8_t kNinoList = 4;
struct ListRooms {
    RoutineId helper;
    TableId rooms, links; // the room list and its links
};
// the room helpers that only set the island's list (the handler word cleared after it)
constexpr ListRooms kListHelpers[] = {
    {RoutineId::St15RoomHelper, TableId::NinoRoomsSt15Rooms, TableId::NinoRoomsSt15Links}, // ST15
    {RoutineId::St16RoomHelper, TableId::NinoRoomsSt16Rooms, TableId::NinoRoomsSt16Links}, // ST16
    {RoutineId::St18RoomHelper, TableId::NinoRoomsSt18Rooms, TableId::NinoRoomsSt18Links}, // ST18
    {RoutineId::St19RoomHelper, TableId::NinoRoomsSt19Rooms, TableId::NinoRoomsSt19Links}, // ST19
    {RoutineId::St1ARoomHelper, TableId::NinoRoomsSt1ARooms, TableId::NinoRoomsSt1ALinks}, // ST1A
    {RoutineId::St1BRoomHelper, TableId::NinoRoomsSt1BRooms, TableId::NinoRoomsSt1BLinks}, // ST1B
};

// story flags
constexpr uint32_t kFlagSt17FieldWeapons = 0x31;  // ST17 at story 6: the field's weapons
constexpr uint32_t kFlagSt18ShipFight = 0x29;     // ST18 sub 0 at story 6: the ship fight (the field's weapons)
constexpr uint32_t kFlagSt38CellOpen = 0x9f;      // ST38 sub 5's cell opened
constexpr uint32_t kFlagSt38Quiet = 0x92;         // ST38's machine sound off
constexpr uint32_t kFlagSt35EntryVisited = 0x7bc; // ST35 sub 0's map flag

constexpr uint8_t kShipFightStory = 6;
// ST18's room callback (subs 0..2; its routine is not ported)
constexpr RoutineId kSt18RoomCallback = RoutineId::NinoRoomsSt18RoomCallback;
constexpr uint8_t kSt18LastSetRoom = 5; // the subs whose weapon mode / callback the room function sets
// ST38's machine sound: exe 0x5d5380 gets no position argument; the exe reads the stale stack word (the return
// address 0x5dc5e3, i.e. the code bytes there) as x / y / z
constexpr uint16_t kSoundSt38Machine = 0x1e4;
constexpr int16_t kStaleX = 0x0d8a, kStaleY = 0x00a6, kStaleZ = 0x2d2f;

// ---- the room helpers --------------------------------------------------------------------------------------------

bool ninoRoomHelper(RoutineId fn, bool run) {
    const GameState& g = gameState();
    RoomScriptState& room = roomScriptState();
    for (const ListRooms& l : kListHelpers) {
        if (l.helper != fn) continue;
        if (!run) return true;
        roomListSetup(kNinoList, stateAddress(l.rooms), stateAddress(l.links));
        room.clearHandlerWord();
        return true;
    }
    switch (fn) {
    case RoutineId::St17RoomHelper: // ST17: the island's list; after story 6 sub 0: the cell (0x3c, 0x3e) to variant 2
        if (!run) return true;
        roomListSetup(kNinoList, stateAddress(TableId::NinoRoomsSt17Rooms), stateAddress(TableId::NinoRoomsSt17Links));
        room.clearHandlerWord();
        if (g.story > kShipFightStory && g.room == 0) switchGridCellVariant(0x3c, 0x3e, 2);
        return true;
    case RoutineId::St36RoomHelper: // ST36: no room list, then the cells / sound (st36_rooms)
        if (!run) return true;
        room.clearHandlerWord();
        roomListSetup(kNoRoomList, 0, 0);
        ninoSt36RoomHelperTail();
        return true;
    case RoutineId::St37RoomHelper: // ST37: likewise (st37_rooms)
        if (!run) return true;
        room.clearHandlerWord();
        roomListSetup(kNoRoomList, 0, 0);
        st37RoomHelperTail();
        return true;
    case RoutineId::St38RoomHelper:
        // ST38 (the Nino ruin's lowest floor): no room list; sub 5 with its cell opened: the cell (0x41,
        // 0x3e) to variant 1; the machine's sound until it is quiet
        if (!run) return true;
        room.clearHandlerWord();
        roomListSetup(kNoRoomList, 0, 0);
        if (g.room == 5 && storyFlag(kFlagSt38CellOpen)) switchGridCellVariant(0x41, 0x3e, 1);
        if (!storyFlag(kFlagSt38Quiet)) soundPlayAtXyz(kSoundSt38Machine, kStaleX, kStaleY, kStaleZ);
        return true;
    case RoutineId::St35RoomHelper: // ST35 (the Nino ruin): no room list; sub 0's map flag; the doors unlocked
        if (!run) return true;
        room.clearHandlerWord();
        roomListSetup(kNoRoomList, 0, 0);
        if (g.room == 0) setStoryFlag(kFlagSt35EntryVisited);
        clearLockFlags(kLockFlags);
        return true;
    default: return false;
    }
}

// ---- the room functions' parts ------------------------------------------------------------------------------------

// ST17 (exe 0x5d8a00): the field's weapons at story 6 once its flag is set
void st17WeaponMode() {
    const bool field = gameState().story == kShipFightStory && storyFlag(kFlagSt17FieldWeapons);
    weaponMode(field ? kWeaponModeField : kWeaponModeTown);
}
// ST18 (exe 0x5d8c30), subs 0..5: the field's weapons in sub 1 and in sub 0's ship fight; the callback in subs 0..2
void st18WeaponMode() {
    const GameState& g = gameState();
    if (g.room > kSt18LastSetRoom) return;
    const bool field = g.room == 1 || (g.room == 0 && g.story == kShipFightStory && storyFlag(kFlagSt18ShipFight));
    weaponMode(field ? kWeaponModeField : kWeaponModeTown);
}
void st18RoomCallback() {
    const GameState& g = gameState();
    if (g.room <= kSt18LastSetRoom) stage().roomCallback = g.room < 3 ? kSt18RoomCallback : RoutineId::None;
}

// ---- the stage cases ---------------------------------------------------------------------------------------------

// ST19 (exe 0x57fb40, after the handler): unless a transition is armed, Roll's "Development Room" -> script 0x30 (her
// development room's copy: content/yosyonke/dev_room)
void st19DevRoomRequest() {
    if (!storyFlag(kFlagTransitionArmed) && storyFlag(kFlagDevRoomAsked)) {
        clearStoryFlag(kFlagDevRoomAsked);
        scriptStart(kScriptDevRoom);
    }
}

} // namespace

void registerNinoRooms(ContentRegistry& r) {
    r.roomHelpers.push_back(&ninoRoomHelper);
    r.roomFunctions[0x17] = {.weaponMode = &st17WeaponMode};
    r.roomFunctions[0x18] = {.weaponMode = &st18WeaponMode, .tail = &st18RoomCallback};
    r.roomCaseTails[0x19] = &st19DevRoomRequest;
}

} // namespace od::content
