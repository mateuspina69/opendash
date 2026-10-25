// The ice ruin (ST2D..ST2F) in the room setup (see ice_ruin_room_helpers.hpp), one routine per exe routine.
#include "content/yosyonke/ice_ruin_room_helpers.hpp"

#include "engine/data_source.hpp"
#include "game/encounter.hpp"
#include "game/game_state.hpp"
#include "game/room_scripts.hpp"
#include "game/story_flags.hpp"
#include "game/visited_rooms.hpp"

namespace od::content {

using namespace od::port;

namespace {

// the script groups by sub-room and a flag (exe data, s8; < 0: none): ST2D [sub * 4 + flag 0x3ef], ST2E [sub * 2 +
// flag 0x73], ST2F [sub * 2]
const DataTable<ScalarTable<int8_t, 0x40>> kSt2dScriptGroups{TableId::IceRuinRoomHelpersSt2dScriptGroups};
const DataTable<ScalarTable<int8_t, 0x20>> kSt2eScriptGroups{TableId::IceRuinRoomHelpersSt2eScriptGroups};
const DataTable<ScalarTable<int8_t, 0x20>> kSt2fScriptGroups{TableId::IceRuinRoomHelpersSt2fScriptGroups};
// ST2F's field encounters by sub-room (exe data, s8: 0 suspends them)
const DataTable<ScalarTable<int8_t, 0x10>> kSt2fFieldRooms{TableId::IceRuinRoomHelpersSt2fFieldRooms};
constexpr uint32_t kFlagSt2dGroups = 0x3ef; // ST2D's second groups
constexpr uint32_t kFlagSt2eGroups = 0x73;  // ST2E's

bool iceRuinRoomHelper(RoutineId fn, bool run) {
    const GameState& g = gameState();
    RoomScriptState& room = roomScriptState();
    switch (fn) {
    case RoutineId::St2DRoomHelper:
        // ST2D / ST2E: the script slot spawns the group of the sub-room and the flag (exe 0x5e66b0), then
    case RoutineId::St2FRoomHelper: { // no room list
        if (!run) return true;
        int8_t group;
        if (fn == RoutineId::St2DRoomHelper)
            group = kSt2dScriptGroups[uint32_t(g.room) * 4 + (storyFlag(kFlagSt2dGroups) ? 1 : 0)];
        else group = kSt2eScriptGroups[uint32_t(g.room) * 2 + (storyFlag(kFlagSt2eGroups) ? 1 : 0)];
        if (group >= 0) encounterSpawnGroupScriptSlot(group);
        room.clearHandlerWord();
        roomListSetup(kNoRoomList, 0, 0);
        return true;
    }
    case RoutineId::St2FRoomHelperA: {
        // ST2F: the field encounters suspended in its sub-rooms of 0 (exe 0x5e6870), else resumed; the
        // script slot's group of the sub-room; no room list
        if (!run) return true;
        EncounterState& e = encounterState();
        if (kSt2fFieldRooms[g.room] == 0) e.control |= kFieldSuspended;
        else e.control &= ~kFieldSuspended;
        const int8_t group = kSt2fScriptGroups[uint32_t(g.room) * 2];
        if (group >= 0) encounterSpawnGroupScriptSlot(group);
        room.clearHandlerWord();
        roomListSetup(kNoRoomList, 0, 0);
        return true;
    }
    default: return false;
    }
}

} // namespace

void registerIceRuinRooms(ContentRegistry& r) { r.roomHelpers.push_back(&iceRuinRoomHelper); }

} // namespace od::content
