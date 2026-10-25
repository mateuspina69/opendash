// The room-script handlers of ST51 (see st51_rooms.hpp), one case per exe routine.
#include "content/subruins/st51_rooms.hpp"

#include "game/actor_pools.hpp"
#include "game/room_scripts.hpp"
#include "game/spawn.hpp"
#include "game/story_flags.hpp"

namespace od::content {

using namespace od::port;

namespace {

constexpr uint32_t kFlagGuardianBeaten = 0xb3; // ST51's guardian beaten (for good)
constexpr uint32_t kFlagItemTaken = 0xb4;      // the item of sub 2 taken

} // namespace

bool st51RoomHandler(RoutineId fn, bool run) {
    RoomScriptState& room = roomScriptState();
    switch (fn) {
    case RoutineId::St51Room1Handler:
        // sub 1 (the guardian's hall): the guardian (the room's first actor) locks the exit until its hit
        // points drop below 0 (then its flag); the door test
        if (!run) return true;
        if (room.handlerStep == 0) {
            if (!storyFlag(kFlagGuardianBeaten)) {
                spawnRecords(stateAddress(TableId::St51RoomsGuardianRecord), 1);
                setStoryFlag(kFlagLocked);
                ++room.handlerStep;
            } else {
                room.handlerStep = 2;
            }
        } else if (room.handlerStep == 1 && actorLayoutAt(room.actors[0]).hitPoints < 0) {
            clearStoryFlag(kFlagLocked);
            setStoryFlag(kFlagGuardianBeaten);
            ++room.handlerStep;
        }
        roomDoorScript();
        return true;
    case RoutineId::St51Room2Handler: // sub 2: the item on its pedestal until it is taken; the door test
        if (!run) return true;
        if (room.handlerStep == 0 && !storyFlag(kFlagItemTaken)) {
            spawnRecords(stateAddress(TableId::St51RoomsItemRecord), 1);
            ++room.handlerStep;
        }
        roomDoorScript();
        return true;
    default: return false;
    }
}

} // namespace od::content
