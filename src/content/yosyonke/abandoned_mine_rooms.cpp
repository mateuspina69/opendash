// the Abandoned Mine's rooms in the room scripts (see abandoned_mine_rooms.hpp), one routine per exe routine: the handlers [[table +
// story * 4] + sub * 4] of ST0F (story 0: table 0x8c682c; story 1). Each keeps its step in the room script's handler
// byte (game/room_scripts RoomScriptState::handlerStep).
#include "content/yosyonke/abandoned_mine_rooms.hpp"

#include "content/yosyonke/abandoned_mine_scripts.hpp"

#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/audio_logic.hpp"
#include "game/encounter.hpp"
#include "game/event_state.hpp"
#include "game/player/player_state.hpp"
#include "game/room_scripts.hpp"
#include "game/script_engine.hpp"
#include "game/script_state.hpp"
#include "game/spawn.hpp"
#include "game/story_flags.hpp"

namespace od::content {

using namespace od::port;

namespace {

constexpr uint8_t kHandlerDone = 1;
constexpr uint8_t kHandlerIdle = 2;        // the handlers whose work is over for the visit
constexpr int16_t kDeepRoomDipStep = 0xa3; // sub 11's music dip (and sub 10's restore)

// ---- ST0F story 0 -----------------------------------------------------------------------------------------------
// the story flags of the Abandoned Mine's visit
constexpr uint32_t kFlagRodeLift = 0x580;       // the ride of script 0x50 started (sub 1: Roll waits below)
constexpr uint32_t kFlagHallSeen = 0x581;       // script 0x51 ran (sub 11)
constexpr uint32_t kFlagGuardianMet = 0x582;    // script 0x52 ran (sub 12)
constexpr uint32_t kFlagGuardianBeaten = 0x583; // script 0x53 ran
constexpr uint32_t kFlagDeviceShown = 0x584;    // script 0x54 ran (sub 11 after the guardian)
constexpr uint32_t kFlagRollMet = 0x689;        // Roll's first record spawned (sub 1)
constexpr uint32_t kFlagSub8Switch = 0x2b3;     // sub 8's encounter switch (hypothesis: the room's chest opened)
// Roll by the lift of sub 1: the first visit, later visits, after the ride
constexpr uint32_t kHallCastCount = 3;
constexpr int16_t kLiftTriggerX = 0xc00; // sub 1: from this x the ride starts
// sub 8's encounter groups: with the switch already set / once it is set
constexpr int kSub8GroupSet = 8, kSub8GroupLater = 7;

// the handlers' flag-driven script start: the transition flags cleared, the flag set, the script started
void startRuinScript(uint32_t flag, uint8_t script) {
    clearStoryFlag(kFlagTransitionArmed);
    clearStoryFlag(kFlagTransitionArriving);
    setStoryFlag(flag);
    scriptRun(script);
}

bool abandonedMineRoomHandler(RoutineId fn, bool run) {
    RoomScriptState& room = roomScriptState();
    AbandonedMineStageBlock& blk = abandonedMineStageBlock();
    switch (fn) {
    case RoutineId::St0FStory0Sub1RoomHandler:
        // ST0F story 0 sub 1: Roll by the lift; the player from x 0xc00 -> the ride (script 0x50)
        if (!run) return true;
        switch (room.handlerStep) {
        case 0:
            blk.roll = nullptr;
            ++room.handlerStep;
            return true;
        case 1:
            if (scriptState().inUse()) return true;
            if (storyFlag(kFlagRodeLift)) {
                pointAtWord(blk.roll, spawnRecordTracked(stateAddress(TableId::AbandonedMineRoomsRollAfterRideRecord)));
                room.handlerStep += 2u;
                return true;
            }
            if (!storyFlag(kFlagRollMet)) {
                pointAtWord(blk.roll, spawnRecordTracked(stateAddress(TableId::AbandonedMineRoomsRollFirstRecord)));
                setStoryFlag(kFlagRollMet);
            } else {
                pointAtWord(blk.roll, spawnRecordTracked(stateAddress(TableId::AbandonedMineRoomsRollRecord)));
            }
            ++room.handlerStep;
            return true;
        case 2:
            if (player().pos.x.whole < kLiftTriggerX) return true;
            setStoryFlag(kFlagRodeLift);
            scriptRun(kRuinLiftScript);
            ++room.handlerStep;
            return true;
        default: return true;
        }
    case RoutineId::St0FStory0Sub8RoomHandler:
        // ST0F story 0 sub 8: the switch already set -> group 8 at once; else wait for it with no event
        // running -> group 7 (exe 0x5e6640)
        if (!run) return true;
        if (room.handlerStep == 0) {
            if (storyFlag(kFlagSub8Switch)) {
                encounterSpawnGroupZoneSlot(kSub8GroupSet);
                room.handlerStep = kHandlerIdle;
                return true;
            }
        } else {
            if (room.handlerStep != 1) return true;
            if (!storyFlag(kFlagSub8Switch) || eventState().active != 0) return true;
            encounterSpawnGroupZoneSlot(kSub8GroupLater);
        }
        ++room.handlerStep;
        return true;
    case RoutineId::St0FStory0Sub10RoomHandler: // ST0F story 0 sub 10: the music back (exe 0x5d58a0)
        if (!run) return true;
        if (room.handlerStep == 0) {
            soundFadeRestore(kMusicFades, kDeepRoomDipStep, 0);
            room.handlerStep = kHandlerDone;
        }
        return true;
    case RoutineId::St0FStory0Sub11RoomHandler:
        // ST0F story 0 sub 11: the music dipped; script 0x51 the first time, later the device / glow / relic;
        // after the guardian script 0x54 once
        if (!run) return true;
        if (room.handlerStep != 0) return true;
        soundFadeDip(kMusicFades, kDeepRoomDipStep, 0, 0);
        if (!storyFlag(kFlagGuardianBeaten)) {
            if (!storyFlag(kFlagHallSeen)) {
                startRuinScript(kFlagHallSeen, kRuinHallScript);
                room.handlerStep = kHandlerDone;
                return true;
            }
            spawnRecords(stateAddress(TableId::AbandonedMineRoomsHallCastRecords), kHallCastCount);
        } else if (!storyFlag(kFlagDeviceShown)) {
            startRuinScript(kFlagDeviceShown, kRuinDeviceScript);
            room.handlerStep = kHandlerDone;
            return true;
        }
        room.handlerStep = kHandlerDone;
        return true;
    case RoutineId::St0FStory0Sub12RoomHandler:
        // ST0F story 0 sub 12: script 0x52 the first time (the doors locked); its guardian's hit points
        // below 0 -> script 0x53, the doors unlocked
        if (!run) return true;
        if (room.handlerStep == 0) {
            if (storyFlag(kFlagGuardianBeaten)) {
                room.handlerStep = kHandlerIdle;
                return true;
            }
            if (storyFlag(kFlagGuardianMet)) {
                room.handlerStep = 1;
                return true;
            }
            clearStoryFlag(kFlagTransitionArmed);
            clearStoryFlag(kFlagTransitionArriving);
            setStoryFlag(kFlagGuardianMet);
            setStoryFlag(kFlagLocked);
            scriptRun(kGuardianEntranceScript);
            ++room.handlerStep;
        } else if (room.handlerStep == 1 && blk.guardian && blk.guardian->hitPoints < 0) {
            scriptRun(kGuardianEndScript);
            setStoryFlag(kFlagGuardianBeaten);
            clearStoryFlag(kFlagLocked);
            room.handlerStep = kHandlerIdle;
        }
        return true;
    case RoutineId::St0FStory1Sub0RoomHandler: // ST0F story 1 sub 0: the ruin's doors unlocked once
        if (!run || room.handlerStep != 0) return true;
        clearLockFlags(kLockFlags);
        room.handlerStep = kHandlerDone;
        return true;
    case RoutineId::St0FStory1Sub11RoomHandler: // ST0F story 1 sub 11: the music channels dipped once
        if (!run || room.handlerStep != 0) return true;
        soundFadeDip(kMusicFades, kDeepRoomDipStep, 0, 0);
        room.handlerStep = kHandlerDone;
        return true;
    default: return false;
    }
}

} // namespace

void registerAbandonedMineRooms(ContentRegistry& r) { r.roomHandlers.push_back(&abandonedMineRoomHandler); }

} // namespace od::content
