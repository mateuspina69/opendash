// The room scripts of ST58: the handlers of the room scripts' stage case 0x58c950 [0x8cd890 + sub * 4] (their state:
// game/room_scripts). One case per exe function (addresses in the comments), transcribed from the disassembly.
#include "content/subruins/st58_rooms.hpp"

#include "game/room_scripts.hpp"
#include "game/spawn.hpp"
#include "game/story_flags.hpp"

namespace od::port {

namespace {

constexpr uint32_t kFlagDoorOpen = 0x179;      // the sub-room's door opened for good

} // namespace

bool st58RoomHandler(RoutineId fn, bool run) {
    RoomScriptState& room = roomScriptState();
    switch (fn) {
    // ---- ST58 (case 0x58c950, table 0x8cd890) -------------------------------------------------------------------
    case RoutineId::St58Sub1RoomHandler:
        // sub 1: the door; step 0 -> 1: shut (its lock flag and record) until its flag; the flag clears
        // the lock
        if (!run || roomDoorScript()) return true; // exe 0x582490
        switch (room.handlerStep) {
        case 0:
            room.handlerStep = 1;
            if (!storyFlag(kFlagDoorOpen)) {
                setStoryFlag(kFlagLocked);
                spawnRecords(stateAddress(TableId::St58RoomsDoorRoomRecord), 1);
            }
            break;
        case 1: break;
        default: return true;
        }
        if (storyFlag(kFlagDoorOpen)) clearStoryFlag(kFlagLocked);
        return true;
    default: return false;
    }
}

} // namespace od::port
