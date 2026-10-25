// The room handlers of the return to Nino island and Glyde's last attack (chapter 3):
//   ST17 sub 0, story 9 (exe 0x57f3f0): the return. Step 0: records 0x8c7468 x 1 + 0x8c7654 x 1 (the talk NPC), flag
//   0x710 cleared, 0x584 set; with flag 0x2c records 0x8c7618 x 3 -> 2, else 0x8c75f0 x 3 -> 1 (falls into 1). 1: no
//   script and no event, flag 0x582 or 0x583 (the guildmaster talked to) -> flags 0x2c / 0x582 / 0x583 set, script
//   0x39, 2.
//   ST18 (table 0x8c76e0) at stories 9 / 0xa (0x8c76b0), Glyde's last attack on Nino island:
//   sub 0 (exe 0x57f7e0): once, flag 0x710 cleared, the four cells to variant 0, record 0x8c77a0 x 1 (static type 0x2d
//   sub 1).
//   sub 1 (exe 0x57f810, the attack): nothing while a script runs. 0: flag 0x710 set; with flag 0x2d (Roll at the
//   machine, script 0x3a) the four cells to variant 2, records 0x8c77b8 x 2 (Glyde's ship type 0x5e sub 0 + the static
//   type 0x38), sound 0x22, the opening (message 0x28) -> 1; else 3. 1: the opening -> 2. 2: flag 0x2e (the ship
//   down), no event -> story + 1, script 0x3b, flag 0x2f cleared, 3; flag 0x2f with the player alive -> event message
//   0x3b (flags 0xb), 3.
//   sub 2 (exe 0x57f8f0, Roll's machine): once, flag 0x710 cleared; without flag 0x2c records 0x8c7778 x 2 (type 0x5e
//   sub 1 + type 0 variant 6); else without 0x2d and no event: 0x2d set, script 0x3a. The step as a dword: 1.
#include "content/calbania/ch3_rooms.hpp"

#include <utility>

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
#include "game/script_state.hpp"
#include "game/spawn.hpp"
#include "game/story_flags.hpp"

namespace od::port {

namespace {

constexpr RoutineId kFnReturn = RoutineId::NinoReturnRoomsReturn, kFnShore = RoutineId::NinoReturnRoomsShore,
                    kFnAttack = RoutineId::NinoReturnRoomsAttack, kFnMachine = RoutineId::NinoReturnRoomsMachine;
constexpr uint32_t kFlagNinoBusy = 0x710, kFlagHintGiven = 0x584, kFlagReturned = 0x2c, kFlagTalkedA = 0x582,
                   kFlagTalkedB = 0x583, kFlagRollAtMachine = 0x2d, kFlagShipDown = 0x2e, kFlagMachineLost = 0x2f;
constexpr uint8_t kReturnScript = 0x39, kRollScript = 0x3a, kAttackEndScript = 0x3b;
constexpr uint16_t kAttackMessage = 0x28, kLostMessage = 0x3b;
constexpr uint32_t kLostEventFlags = 0xb;
constexpr uint32_t kSweepAll = 0xffe;
constexpr uint32_t kSweepFreeze = 1, kSweepThaw = 2;
constexpr uint32_t kOpeningEventFlags = 8;
constexpr uint16_t kBannerX = 0x54, kBannerY = 0x30;

enum class ReturnStep : uint8_t { Spawn = 0, WaitTalk = 1, Done = 2 };
enum class AttackStep : uint8_t { Start = 0, Opening = 1, Fight = 2, Done = 3 };
enum class OpeningStep : uint8_t { Banner = 0, Freeze = 1, WaitEvent = 2, Done = 3 };

// a script or an event runs
bool scriptRuns() { return scriptState().inUse(); }
bool eventRuns() { return eventState().active != 0; }

// exe 0x57f960(state, message): ST18's mission opening (story 9) -- the banner and the event message, the actors 0xffe
// frozen, then once no event runs: the banner hidden, the player's input back, the actors thawed -> 1 (done); the
// handler's byte 1 is its step
int attackOpening(uint16_t message) {
    uint8_t& step = roomScriptState().handlerByte1;
    switch (OpeningStep(step)) {
    case OpeningStep::Banner:
        hudBannerShow(kBannerX, kBannerY, HudBannerStyle::MissionStart);
        eventStartAt(0, nullptr, kOpeningEventFlags, 0, message);
        ++step;
        return 0;
    case OpeningStep::Freeze:
        actorsSweep(kSweepAll, kSweepFreeze);
        ++step;
        return 0;
    case OpeningStep::WaitEvent:
        if (eventRuns()) return 0;
        ++step;
        hudBanner().hide(); // exe 0x5f2680
        player().inputMode = 0;
        actorsSweep(kSweepAll, kSweepThaw);
        return 1;
    case OpeningStep::Done: return 1;
    default: return 0;
    }
}

void returnRoom() { // exe 0x57f3f0
    RoomScriptState& room = roomScriptState();
    switch (ReturnStep(room.handlerStep)) {
    case ReturnStep::Spawn:
        spawnRecords(stateAddress(TableId::NinoReturnRoomsReturnRoomSpawns), 1);
        spawnRecords(stateAddress(TableId::NinoReturnRoomsReturnRoomSecondSpawns), 1);
        clearStoryFlag(kFlagNinoBusy);
        setStoryFlag(kFlagHintGiven);
        if (storyFlag(kFlagReturned)) {
            spawnRecords(stateAddress(TableId::NinoReturnRoomsReturnRoomReturnedSpawns), 3);
            room.enterHandlerStep(uint8_t(ReturnStep::Done));
            return;
        }
        spawnRecords(stateAddress(TableId::NinoReturnRoomsReturnRoomWaitingSpawns), 3);
        room.enterHandlerStep(uint8_t(ReturnStep::WaitTalk));
        [[fallthrough]];
    case ReturnStep::WaitTalk:
        if (scriptRuns() || eventRuns()) return;
        if (!storyFlag(kFlagTalkedA) && !storyFlag(kFlagTalkedB)) return;
        setStoryFlag(kFlagReturned);
        setStoryFlag(kFlagTalkedA);
        setStoryFlag(kFlagTalkedB);
        scriptRun(kReturnScript);
        room.enterHandlerStep(uint8_t(ReturnStep::Done));
        return;
    default: return;
    }
}

void attackRoom() { // exe 0x57f810
    RoomScriptState& room = roomScriptState();
    if (scriptRuns()) return;
    switch (AttackStep(room.handlerStep)) {
    case AttackStep::Start:
        setStoryFlag(kFlagNinoBusy);
        if (!storyFlag(kFlagRollAtMachine)) {
            room.handlerStep = uint8_t(AttackStep::Done);
            return;
        }
        st18CellsToVariant(2);
        spawnRecords(stateAddress(TableId::NinoReturnRoomsAttackRoomSpawns), 2);
        soundPlayFlatForced(0x22);
        attackOpening(kAttackMessage);
        room.enterHandlerStepWord(uint8_t(AttackStep::Opening));
        return;
    case AttackStep::Opening:
        if (attackOpening(kAttackMessage)) room.handlerStep = uint8_t(AttackStep::Fight);
        return;
    case AttackStep::Fight:
        if (storyFlag(kFlagShipDown) && !eventRuns()) {
            ++gameState().nextStory; // exe 0x512800
            scriptRun(kAttackEndScript);
            clearStoryFlag(kFlagMachineLost);
            room.handlerStep = uint8_t(AttackStep::Done);
        } else if (storyFlag(kFlagMachineLost) && player().hitPoints >= 0) {
            eventStartAt(0, nullptr, kLostEventFlags, 0, kLostMessage);
            room.handlerStep = uint8_t(AttackStep::Done);
        }
        return;
    default: return;
    }
}

} // namespace

void st18CellsToVariant(uint32_t v) {
    for (const auto& c : {std::pair{0x40u, 0x40u}, {0x3fu, 0x40u}, {0x40u, 0x3fu}, {0x3fu, 0x3fu}})
        switchGridCellVariant(c.first, c.second, uint8_t(v));
}

bool ninoReturnRoomHandler(RoutineId fn, bool run) {
    RoomScriptState& room = roomScriptState();
    switch (fn) {
    case kFnReturn:
        if (run) returnRoom();
        return true;
    case kFnShore:
        if (!run) return true;
        if (room.handlerStep == 0) {
            clearStoryFlag(kFlagNinoBusy);
            st18CellsToVariant(0);
            spawnRecords(stateAddress(TableId::NinoReturnRoomsShoreSpawns), 1);
            room.handlerStep = 1;
        }
        return true;
    case kFnAttack:
        if (run) attackRoom();
        return true;
    case kFnMachine:
        if (!run) return true;
        if (room.handlerStep == 0) {
            clearStoryFlag(kFlagNinoBusy);
            if (!storyFlag(kFlagReturned)) {
                spawnRecords(stateAddress(TableId::NinoReturnRoomsMachineSpawns), 2);
            } else if (!storyFlag(kFlagRollAtMachine) && !eventRuns()) {
                setStoryFlag(kFlagRollAtMachine);
                scriptRun(kRollScript);
            }
            room.enterHandlerStep(1);
        }
        return true;
    default: return false;
    }
}

} // namespace od::port
