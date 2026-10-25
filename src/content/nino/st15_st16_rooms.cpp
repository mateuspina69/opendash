// The room scripts of ST15 / ST16: the whole stage cases 0x57eeb0 / 0x57f040 of the room scripts (exe 0x512890; their
// handlers 0x57ef70 / 0x57f150 called directly). The handler's step (room script state +0) and its second byte (+1:
// the mission opening's step). One case per exe routine (addresses in the comments).
#include "content/nino/st15_st16_rooms.hpp"

#include "game/actor_sweeps.hpp"
#include "game/event.hpp"
#include "game/event_state.hpp"
#include "game/hud.hpp"
#include "game/message.hpp"
#include "game/player/player_state.hpp"
#include "game/room_scripts.hpp"
#include "game/script_engine.hpp"
#include "game/story_flags.hpp"
#include "game/transition_state.hpp"

namespace od::port {

namespace {

// story flags
constexpr uint32_t kFlagMissionRunning = 0x710;  // set when the mission's room starts
constexpr uint32_t kFlagSt15MissionWon = 0x28;   // ST15: the mission's win (script 0x35)
constexpr uint32_t kFlagSt16MissionWon = 0x27;   // ST16: the mission lever pulled (script 0x34)
constexpr uint32_t kFlagMissionFailed = 0x2f;    // the mission failed (its message)
constexpr uint32_t kFlagSt16CraneScene = 0x26;   // ST16: script 0x33 asked
// scripts
constexpr uint8_t kScriptSt15Won = 0x35, kScriptSt16Won = 0x34, kScriptSt16Crane = 0x33;
// messages
constexpr uint16_t kMsgSt15Opening = 0x10, kMsgSt16Opening = 0x13, kMsgMissionFailed = 0x1c;
constexpr uint8_t kOpeningWindow = 8, kFailedWindow = 0xb, kSt16MissionWindow = 4;
// the HUD banner's place
constexpr uint16_t kBannerX = 0x54, kBannerY = 0x30;
// the enemy classes frozen during the opening (exe 0x5e05d0's mask)
constexpr uint32_t kEnemyClasses = 0xffe;
constexpr uint32_t kSweepFreeze = 1, kSweepRelease = 2;

// the handler's steps
enum class MissionRoomStep : uint8_t { Start = 0, Opening = 1, Running = 2, Over = 3 };
// the mission opening's steps (the handler's second byte)
enum class OpeningStep : uint8_t { Banner = 0, Freeze = 1, WaitEvent = 2, Done = 3 };

// exe 0x57ef70 / 0x57f150 (the mission opening): the "MISSION START" banner, the event message `msg` of window 8, every
// enemy class frozen, then once no event runs the banner hidden, the player's input back and the classes released;
// true when done
bool missionOpening(uint16_t msg) {
    RoomScriptState& room = roomScriptState();
    switch (OpeningStep(room.handlerByte1)) {
    case OpeningStep::Banner:
        hudBannerShow(kBannerX, kBannerY, HudBannerStyle::MissionStart);
        eventStartAt(0, nullptr, kOpeningWindow, 0, msg); // exe 0x543b40
        room.handlerByte1 += 1u;
        return false;
    case OpeningStep::Freeze:
        actorsSweep(kEnemyClasses, kSweepFreeze);
        room.handlerByte1 += 1u;
        return false;
    case OpeningStep::WaitEvent:
        if (eventState().active != 0) return false;
        room.handlerByte1 += 1u;
        hudBanner().hide(); // exe 0x5f2680
        player().inputMode = 0;
        actorsSweep(kEnemyClasses, kSweepRelease);
        return true;
    case OpeningStep::Done: return true;
    default: return false;
    }
}

} // namespace

bool st15St16RoomHandler(RoutineId fn, bool run) {
    RoomScriptState& room = roomScriptState();
    switch (fn) {
    // ---- ST15 (case 0x57eeb0, the handler 0x57ef70 called directly) -----------------------------------------
    case RoutineId::St15RoomHandler:
        // Start: the mission flag; Opening: the mission opening (message 0x10); Running: the win (flag 0x28,
        // no event) -> script 0x35, the failure cleared; else the failure -> its event message (window 0xb); the door
        if (!run) return true;
        switch (MissionRoomStep(room.handlerStep)) {
        case MissionRoomStep::Start:
            setStoryFlag(kFlagMissionRunning);
            room.handlerStep = uint8_t(MissionRoomStep::Opening);
            [[fallthrough]];
        case MissionRoomStep::Opening:
            if (missionOpening(kMsgSt15Opening)) room.handlerStep = uint8_t(MissionRoomStep::Running);
            break;
        case MissionRoomStep::Running:
            if (storyFlag(kFlagSt15MissionWon) && eventState().active == 0) {
                scriptRun(kScriptSt15Won);
                clearStoryFlag(kFlagMissionFailed);
                room.handlerStep = uint8_t(MissionRoomStep::Over);
            } else if (storyFlag(kFlagMissionFailed)) {
                eventStartAt(0, nullptr, kFailedWindow, 0, kMsgMissionFailed);
                room.handlerStep = uint8_t(MissionRoomStep::Over);
            }
            break;
        default: break;
        }
        roomDoorScript();
        return true;
    // ---- ST16 (case 0x57f040, the handler 0x57f150 called directly) -----------------------------------------
    case RoutineId::St16RoomHandler:
        // Start: the mission flag, the opening's step cleared; Opening: the mission opening (message 0x13);
        // Running: the lever (flag 0x27, no event) -> script 0x34, flags 0x26 / 0x2f cleared; else the failure with the
        // player alive -> window 4 closed, its event message. Then flag 0x26, no event, no transition -> script 0x33;
        // the door
        if (!run) return true;
        switch (MissionRoomStep(room.handlerStep)) {
        case MissionRoomStep::Start:
            setStoryFlag(kFlagMissionRunning);
            room.enterHandlerStepWord(uint8_t(MissionRoomStep::Opening));
            [[fallthrough]];
        case MissionRoomStep::Opening:
            if (missionOpening(kMsgSt16Opening)) room.handlerStep = uint8_t(MissionRoomStep::Running);
            break;
        case MissionRoomStep::Running:
            if (storyFlag(kFlagSt16MissionWon) && eventState().active == 0) {
                scriptRun(kScriptSt16Won);
                clearStoryFlag(kFlagSt16CraneScene);
                clearStoryFlag(kFlagMissionFailed);
                room.handlerStep = uint8_t(MissionRoomStep::Over);
            } else if (storyFlag(kFlagMissionFailed) && player().hitPoints >= 0) {
                windowClose(kSt16MissionWindow, WindowClose::Force); // exe 0x5576b0(4, 1)
                eventStartAt(0, nullptr, kFailedWindow, 0, kMsgMissionFailed);
                room.handlerStep = uint8_t(MissionRoomStep::Over);
            }
            break;
        default: break;
        }
        if (storyFlag(kFlagSt16CraneScene) && eventState().active == 0 && transitionRequest().state == 0) {
            scriptRun(kScriptSt16Crane);
            return true;
        }
        roomDoorScript();
        return true;
    default: return false;
    }
}

} // namespace od::port

// the handlers the whole cases call directly kHandlersTranscribed: 0x57ef70 0x57f150
