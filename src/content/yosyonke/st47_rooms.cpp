// The rooms of ST47 in the room scripts (see st47_rooms.hpp), one routine per exe routine: the handlers [[0x8cccf4 +
// story * 4] + sub * 4]. Each keeps its step in the room script's handler byte (game/room_scripts
// RoomScriptState::handlerStep).
#include "content/yosyonke/st47_rooms.hpp"

#include "game/audio_logic.hpp"
#include "game/room_scripts.hpp"
#include "game/script_engine.hpp"
#include "game/spawn.hpp"
#include "game/story_flags.hpp"

namespace od::content {

using namespace od::port;

namespace {

constexpr uint8_t kHandlerDone = 1;
constexpr uint8_t kHandlerIdle = 2; // the handlers whose work is over for the visit
constexpr uint32_t kFlagExitSeen = 0x5b0; // script 0x12 ran (story 1 sub 2)
constexpr uint8_t kRuinExitScript = 0x12; // out of the Abandoned Mine (content/yosyonke/abandoned_mine_exit)
constexpr uint32_t kCastCount = 3;
constexpr uint16_t kArrivalSound = 0x16;

// story 0 subs 0 / 3 and story 1 sub 0 (the light and the cast), story 1 sub 1, story 0 subs 1 / 2 (nothing); story 1
// sub 2: the first time script 0x12, then its record and, one tick later, the arrival's sound
bool st47RoomHandler(RoutineId fn, bool run) {
    RoomScriptState& room = roomScriptState();
    switch (fn) {
    case RoutineId::St47RoomHandler: case RoutineId::St47RoomHandlerI: case RoutineId::St47RoomHandlerJ:
        if (!run) return true;
        if (room.handlerStep == 0) {
            spawnRecords(stateAddress(TableId::St47RoomsLightRecord), 1);
            spawnRecords(stateAddress(TableId::St47RoomsCastRecords), kCastCount);
            ++room.handlerStep;
        }
        return true;
    case RoutineId::St47RoomHandlerA:
        if (!run) return true;
        if (room.handlerStep == 0) {
            spawnRecords(stateAddress(TableId::St47RoomsSub1Record), 1);
            ++room.handlerStep;
        }
        return true;
    case RoutineId::St47RoomHandlerB:
        if (!run) return true;
        if (room.handlerStep == 0) room.handlerStep = kHandlerDone;
        return true;
    case RoutineId::St47RoomHandlerC:
        if (!run) return true;
        if (room.handlerStep == 0) {
            if (!storyFlag(kFlagExitSeen)) {
                setStoryFlag(kFlagExitSeen);
                scriptRun(kRuinExitScript);
                room.handlerStep = kHandlerDone;
                spawnRecords(stateAddress(TableId::St47RoomsSub2Record), 1);
                return true;
            }
            room.handlerStep = kHandlerIdle;
            spawnRecords(stateAddress(TableId::St47RoomsSub2Record), 1);
        } else if (room.handlerStep == 1) {
            soundPlayFlatForced(kArrivalSound);
            room.handlerStep = kHandlerIdle;
        }
        return true;
    default: return false;
    }
}

} // namespace

void registerSt47Rooms(ContentRegistry& r) { r.roomHandlers.push_back(&st47RoomHandler); }

} // namespace od::content
