// The room handlers of Calbania's story (chapter 3) on the island itself:
//   ST23 sub 0 (the Flutter's dock): story 8 (exe 0x581770) the first time (flag 0x5b0) record 0x8c8c80 (the Flutter) +
//   script 0x57, later records 0x8c8c80 x 2 once; stories 9..0x10 (exe 0x5817d0) records 0x8c8c80 x 2 once; ST20 sub 1
//   (stories 8..) / ST23 from story 0x11 (exe 0x581800): step 1, nothing else.
//   ST20 sub 0 (the town): story 8 (exe 0x5813b0): step 0 -> 1; 1: script 0x10 the first time (flag 0x590) -> 2; 2:
//   records 0x8c8734 x 4 with flag 0x586, else 0x8c875c x 2 -> 3. Story 9 (exe 0x581440): script 0x11 the first time
//   (flag 0x590) -> 1; 1: records 0x8c8720 x 4 without flag 0x13, else 0x8c8788 x 4 -> 2. Stories 0xa..0xf (exe
//   0x5814b0): those records once. From story 0x10 (exe 0x5814f0): records 0x8c87d8 x 4 once.
//   ST1F sub 0 at story 8 (exe 0x580ec0): the defence of the square. The door test; step 0: flag 0x711, the phase by
//   the flags 0x583..0x586; 1: flag 0x583 -> script 0x1d, then sound 0xe and the opening (exe 0x581180) -> 2 (byte 1
//   = 1); 2: sound 0xe; 0x584 -> script 0x1e (3), 0x585 -> flag 0x584 + script 0x1f (4); 3: 0x585 -> script 0x1f; 4:
//   0x586 -> script 0x20; 5: record 0x8c84c8, flag 0x711 cleared, with flag 0x1f cell (0x46, 0x3f) to variant 1 -> 6.
//   ST1F sub 2, stories 9..0x10 (exe 0x581270; sub 0 at 0x11 / 0x12): 0: records 0x8c84e0 x 4 -> 1; 1: flag 0x690 (the
//   departure accepted, ST1F message 17) without 0x150 and no event -> script 0x69 (no door test that tick); the door
//   test.
//   ST1F sub 0, stories 9..0x10 (exe 0x581230): the door test; with flag 0x23 (the escape from Glyde's base) script
//   0x26 and 0x23 cleared.
#include "content/calbania/ch3_rooms.hpp"

#include "game/actor_sweeps.hpp"
#include "game/audio_logic.hpp"
#include "game/breakable_walls.hpp"
#include "game/event.hpp"
#include "game/event_state.hpp"
#include "game/game_state.hpp"
#include "game/hud.hpp"
#include "game/player/player_state.hpp"
#include "game/room_scripts.hpp"
#include "game/script_engine.hpp"
#include "game/spawn.hpp"
#include "game/story_flags.hpp"

namespace od::port {

namespace {

constexpr RoutineId kFnDockFirst = RoutineId::CalbaniaStoryRoomsDockFirst,
                    kFnDockLater = RoutineId::CalbaniaStoryRoomsDockLater, kFnIdle = RoutineId::St47RoomHandlerB;
constexpr RoutineId kFnTownStory8 = RoutineId::CalbaniaStoryRoomsTownStory8,
                    kFnTownStory9 = RoutineId::CalbaniaStoryRoomsTownStory9,
                    kFnTownMiddle = RoutineId::CalbaniaStoryRoomsTownMiddle,
                   kFnTownLate = RoutineId::CalbaniaStoryRoomsTownLate;
constexpr RoutineId kFnSquareDefence = RoutineId::CalbaniaStoryRoomsSquareDefence,
                    kFnSquareDeparture = RoutineId::CalbaniaStoryRoomsSquareDeparture,
                    kFnSquareAfterBase = RoutineId::CalbaniaStoryRoomsSquareAfterBase;

constexpr uint32_t kFlagFlutterArrived = 0x5b0, kFlagTownVisited = 0x590, kFlagLastPhase = 0x586,
                   kFlagTownFolkMoved = 0x13;
constexpr uint8_t kArrivalScript = 0x57, kTownFirstScript = 0x10, kTownReturnScript = 0x11;

constexpr uint8_t kDefenceStory = 8;
constexpr uint32_t kFlagSquareBusy = 0x711, kFlagPhase1 = 0x583, kFlagPhase2 = 0x584, kFlagPhase3 = 0x585,
                   kFlagPhase4 = 0x586, kFlagWallBroken = 0x1f;
constexpr uint8_t kPhase1Script = 0x1d, kPhase2Script = 0x1e, kPhase3Script = 0x1f, kPhase4Script = 0x20;
constexpr uint16_t kSquareChime = 0xe;
constexpr uint32_t kFlagDepartureAccepted = 0x690, kFlagDepartureBlocked = 0x150, kFlagBaseEscape = 0x23;
constexpr uint8_t kDepartureScript = 0x69, kBaseExplosionScript = 0x26;

constexpr uint32_t kSweepAll = 0xffe, kSweepFreeze = 1, kSweepThaw = 2;
constexpr uint32_t kOpeningEventFlags = 8;
constexpr uint16_t kFightMessage = 10;
constexpr uint16_t kBannerX = 0x54, kBannerY = 0x30;

enum class TownStep : uint8_t { Start = 0, Script = 1, Spawn = 2, Done = 3 };
enum class ReturnStep : uint8_t { Script = 0, Spawn = 1, Done = 2 };
// the square's steps: the phase reached; byte 1 the phase's own sub-step
enum class DefenceStep : uint8_t { Start = 0, Phase1 = 1, Phase2 = 2, Phase3 = 3, Phase4 = 4, Won = 5, Done = 6 };
enum class Phase1Sub : uint8_t { WaitFlag = 0, Chime = 1, Opening = 2 };
enum class OpeningStep : uint8_t { Banner = 0, Freeze = 1, WaitEvent = 2, Done = 3 };
enum class DepartureStep : uint8_t { Spawn = 0, Wait = 1 };

// exe 0x581180(state): the fight's opening -- the banner and the event message 10, the actors 0xffe frozen, then once
// no event runs: the banner hidden, the player's input back, the actors thawed -> 1 (done); the handler's byte 3 is
// its step
int fightOpening() {
    uint8_t& step = roomScriptState().handlerByte3;
    switch (OpeningStep(step)) {
    case OpeningStep::Banner:
        hudBannerShow(kBannerX, kBannerY, HudBannerStyle::MissionStart);
        eventStartAt(0, nullptr, kOpeningEventFlags, 0, kFightMessage);
        ++step;
        return 0;
    case OpeningStep::Freeze:
        actorsSweep(kSweepAll, kSweepFreeze);
        ++step;
        return 0;
    case OpeningStep::WaitEvent:
        if (eventState().active != 0) return 0;
        ++step;
        hudBanner().hide(); // exe 0x5f2680
        player().inputMode = 0;
        actorsSweep(kSweepAll, kSweepThaw);
        return 1;
    case OpeningStep::Done: return 1;
    default: return 0;
    }
}

void squareDefence() { // exe 0x580ec0
    if (roomDoorScript()) return;
    if (gameState().story != kDefenceStory) return;
    RoomScriptState& room = roomScriptState();
    uint8_t& sub = room.handlerByte1;
    auto chime = [&]() {
        if (sub == 0) {
            soundPlayFlatForced(kSquareChime);
            sub = 1;
        }
    };
    switch (DefenceStep(room.handlerStep)) {
    case DefenceStep::Start:
        setStoryFlag(kFlagSquareBusy);
        room.enterHandlerStep(uint8_t(DefenceStep::Phase1));
        if (storyFlag(kFlagPhase1)) room.enterHandlerStep(uint8_t(DefenceStep::Phase2));
        if (storyFlag(kFlagPhase2)) room.enterHandlerStep(uint8_t(DefenceStep::Phase3));
        if (storyFlag(kFlagPhase3)) room.enterHandlerStep(uint8_t(DefenceStep::Phase4));
        if (storyFlag(kFlagPhase4)) room.enterHandlerStep(uint8_t(DefenceStep::Won));
        return;
    case DefenceStep::Phase1:
        if (Phase1Sub(sub) == Phase1Sub::WaitFlag) {
            if (storyFlag(kFlagPhase1)) {
                scriptRun(kPhase1Script);
                sub = uint8_t(Phase1Sub::Chime);
            }
            return;
        }
        if (Phase1Sub(sub) == Phase1Sub::Chime) {
            soundPlayFlatForced(kSquareChime);
            sub = uint8_t(Phase1Sub::Opening);
        } else if (Phase1Sub(sub) != Phase1Sub::Opening) {
            return;
        }
        if (fightOpening()) room.enterHandlerStep(uint8_t(DefenceStep::Phase2), 1);
        return;
    case DefenceStep::Phase2:
        chime();
        if (storyFlag(kFlagPhase2)) {
            scriptRun(kPhase2Script);
            room.enterHandlerStep(uint8_t(DefenceStep::Phase3));
        } else if (storyFlag(kFlagPhase3)) {
            setStoryFlag(kFlagPhase2);
            scriptRun(kPhase3Script);
            room.enterHandlerStep(uint8_t(DefenceStep::Phase4));
        }
        return;
    case DefenceStep::Phase3:
        chime();
        if (storyFlag(kFlagPhase3)) {
            scriptRun(kPhase3Script);
            room.enterHandlerStep(uint8_t(DefenceStep::Phase4));
        }
        return;
    case DefenceStep::Phase4:
        chime();
        if (storyFlag(kFlagPhase4)) {
            scriptRun(kPhase4Script);
            room.enterHandlerStep(uint8_t(DefenceStep::Won));
        }
        return;
    case DefenceStep::Won:
        spawnRecords(stateAddress(TableId::CalbaniaStoryRoomsSquareDefenceWonSpawns), 1);
        clearStoryFlag(kFlagSquareBusy);
        if (storyFlag(kFlagWallBroken)) switchGridCellVariant(0x46, 0x3f, 1);
        room.enterHandlerStep(uint8_t(DefenceStep::Done));
        return;
    default: return;
    }
}

} // namespace

bool calbaniaStoryRoomHandler(RoutineId fn, bool run) {
    RoomScriptState& room = roomScriptState();
    switch (fn) {
    case kFnDockFirst:
        if (!run || room.handlerStep != 0) return true;
        if (!storyFlag(kFlagFlutterArrived)) {
            spawnRecords(stateAddress(TableId::CalbaniaStoryRoomsFlutterRecord), 1);
            setStoryFlag(kFlagFlutterArrived);
            scriptRun(kArrivalScript);
            return true;
        }
        spawnRecords(stateAddress(TableId::CalbaniaStoryRoomsFlutterRecord), 2);
        ++room.handlerStep;
        return true;
    case kFnDockLater:
        if (run && room.handlerStep == 0) {
            spawnRecords(stateAddress(TableId::CalbaniaStoryRoomsFlutterRecord), 2);
            ++room.handlerStep;
        }
        return true;
    case kFnIdle:
        if (run && room.handlerStep == 0) room.handlerStep = 1;
        return true;
    case kFnTownStory8:
        if (!run) return true;
        switch (TownStep(room.handlerStep)) {
        case TownStep::Start: room.handlerStep = uint8_t(TownStep::Script); return true;
        case TownStep::Script:
            if (!storyFlag(kFlagTownVisited)) {
                scriptRun(kTownFirstScript);
                setStoryFlag(kFlagTownVisited);
            }
            room.handlerStep = uint8_t(TownStep::Spawn);
            return true;
        case TownStep::Spawn:
            spawnRecords(storyFlag(kFlagLastPhase) ? stateAddress(TableId::CalbaniaStoryRoomsTownLastPhaseSpawns) : stateAddress(TableId::CalbaniaStoryRoomsTownStory8Spawns), storyFlag(kFlagLastPhase) ? 4 : 2);
            room.handlerStep = uint8_t(TownStep::Done);
            return true;
        default: return true;
        }
    case kFnTownStory9:
        if (!run) return true;
        if (ReturnStep(room.handlerStep) == ReturnStep::Script) {
            if (!storyFlag(kFlagTownVisited)) {
                scriptRun(kTownReturnScript);
                setStoryFlag(kFlagTownVisited);
            }
            room.handlerStep = uint8_t(ReturnStep::Spawn);
        } else if (ReturnStep(room.handlerStep) == ReturnStep::Spawn) {
            spawnRecords(storyFlag(kFlagTownFolkMoved) ? stateAddress(TableId::CalbaniaStoryRoomsTownFolkMovedSpawns) : stateAddress(TableId::CalbaniaStoryRoomsTownFolkSpawns), 4);
            room.handlerStep = uint8_t(ReturnStep::Done);
        }
        return true;
    case kFnTownMiddle:
        if (run && room.handlerStep == 0) {
            spawnRecords(storyFlag(kFlagTownFolkMoved) ? stateAddress(TableId::CalbaniaStoryRoomsTownFolkMovedSpawns) : stateAddress(TableId::CalbaniaStoryRoomsTownFolkSpawns), 4);
            room.handlerStep = 1;
        }
        return true;
    case kFnTownLate:
        if (run && room.handlerStep == 0) {
            spawnRecords(stateAddress(TableId::CalbaniaStoryRoomsTownLateSpawns), 4);
            room.handlerStep = 1;
        }
        return true;
    case kFnSquareDefence:
        if (run) squareDefence();
        return true;
    case kFnSquareDeparture:
        if (!run) return true;
        if (DepartureStep(room.handlerStep) == DepartureStep::Spawn) {
            spawnRecords(stateAddress(TableId::CalbaniaStoryRoomsSquareDepartureSpawns), 4); // exe 0x511090
            ++room.handlerStep;
        } else if (DepartureStep(room.handlerStep) == DepartureStep::Wait && !storyFlag(kFlagDepartureBlocked) &&
                   storyFlag(kFlagDepartureAccepted) && eventState().active == 0) {
            scriptRun(kDepartureScript);
            return true;
        }
        roomDoorScript();
        return true;
    case kFnSquareAfterBase:
        if (!run) return true;
        if (roomDoorScript()) return true;
        if (storyFlag(kFlagBaseEscape)) {
            scriptRun(kBaseExplosionScript);
            clearStoryFlag(kFlagBaseEscape);
        }
        return true;
    default: return false;
    }
}

} // namespace od::port
