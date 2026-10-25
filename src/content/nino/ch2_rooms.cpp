// Story chapter 2 after the Manda key: the room-script handlers and stage cases of the flight to the next island and
// its missions (see content/nino/ch2_rooms.hpp); the scripts they start are content/nino/ch2_scripts.
#include "content/nino/ch2_rooms.hpp"
#include "content/nino/stage_light.hpp"

#include "game/loaded_data.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actor_sweeps.hpp"
#include "game/audio_logic.hpp"
#include "game/breakable_walls.hpp"
#include "game/event.hpp"
#include "game/event_state.hpp"
#include "game/game_state.hpp"
#include "game/hud.hpp"
#include "game/message.hpp"
#include "game/player/player_state.hpp"
#include "game/room_grid.hpp"
#include "game/room_scripts.hpp"
#include "game/script_engine.hpp"
#include "game/script_state.hpp"
#include "game/spawn.hpp"
#include "game/story_flags.hpp"
#include "game/transition_state.hpp"

namespace od::port {

namespace {

// the chapter's story flags
constexpr uint32_t kFlagRoomActive = 0x710;   // the stage's mission / room logic runs (its gate locks closed)
constexpr uint32_t kFlagTakeOff = 0x24;       // script 0x31 played
constexpr uint32_t kFlagCityGate = 0x25;      // the city's gate opened (script 0x32)
constexpr uint32_t kFlagCraneMission = 0x27;  // ST16's mission done (the lever)
constexpr uint32_t kFlagCannonMission = 0x28; // ST15's mission done
constexpr uint32_t kFlagShipMission = 0x29;   // ST17's mission done (the enemy ships)
constexpr uint32_t kFlagShipFight = 0x2a;     // ST18's ship fight survived
constexpr uint32_t kFlagGunshipDown = 0x2b;   // ST18's gunship shot down
constexpr uint32_t kFlagMissionFailed = 0x2f;
constexpr uint32_t kFlagCaught = 0x26;        // ST16: the box carrier caught the player (script 0x33)
constexpr uint32_t kFlagLookAround = 0x30, kFlagLanded = 0x31;
constexpr uint32_t kFlagLockA = 0x711, kFlagLockB = 0x712, kFlagLockC = 0x713, kFlagLockD = 0x714, kFlagLockE = 0x715;
constexpr uint32_t kFlagKeyA = 0x580, kFlagKeyB = 0x581;
constexpr uint32_t kFlagPlantA = 0x38, kFlagPlantB = 0x39;
// the scripts
constexpr uint8_t kScriptTakeOff = 0x31, kScriptCityGate = 0x32, kScriptCaught = 0x33, kScriptCraneDone = 0x34,
                  kScriptCannonDone = 0x35, kScriptShipsDone = 0x36, kScriptShipFight = 0x37, kScriptGunshipDown = 0x38,
                  kScriptSt46Sub3 = 0x3c, kScriptLookAround = 0x3d, kScriptLanding = 0x3e;
constexpr uint8_t kSt15 = 0x15, kSt16 = 0x16;
// the events: the mission's failure (back to the Sulphur-Bottom) and its parameter
constexpr uint16_t kMessageFailed = 0x1c, kMessageGoBack = 0x3b;
constexpr uint32_t kEventGoBack = 0xb;
constexpr uint32_t kMissionEventParam = 8;
constexpr uint16_t kMessageCannon = 0x10, kMessageCrane = 0x13, kMessageShips = 0x1e;
constexpr int8_t kMissionWindow = 4;
// the lock lights of ST19's gate (exe data: the light settings)
constexpr uint8_t kLockLightSize = 0x30;

// The mission opening's steps (the room handler's second byte)
enum class OpeningStep : uint8_t { Banner = 0, Freeze = 1, WaitEvent = 2, Done = 3 };

// exe 0x57f510 (ST17) / 0x57ef70 (ST15) / 0x57f150 (ST16): a mission's opening -- the banner + the event message
// `message`, the actors frozen; once the event is over the banner hidden, the player's input back, the actors run (1)
int missionOpening(uint16_t message) {
    RoomScriptState& room = roomScriptState();
    switch (OpeningStep(room.handlerByte1)) {
    case OpeningStep::Banner:
        hudBannerShow(0x54, 0x30, HudBannerStyle::MissionStart); // exe 0x5f25e0
        eventStartAt(0, nullptr, kMissionEventParam, 0, message);
        room.handlerByte1 += 1u;
        return 0;
    case OpeningStep::Freeze:
        actorsSweep(kSweepActors, kSweepToggleRun);
        room.handlerByte1 += 1u;
        return 0;
    case OpeningStep::WaitEvent:
        if (eventState().active != 0) return 0;
        room.handlerByte1 += 1u;
        hudBanner().hide(); // exe 0x5f2680
        player().inputMode = 0;
        actorsSweep(kSweepActors, kSweepSetRun);
        return 1;
    case OpeningStep::Done: return 1;
    default: return 0;
    }
}
// the four cells of ST18's floor (the gunship's landing) to the segment variant
void st18FloorCells(uint8_t variant) {
    switchCellSegmentVariant(ninoGridCell(0x40, 0x40), variant); // exe 0x57fa10(variant)
    switchCellSegmentVariant(ninoGridCell(0x3f, 0x40), variant);
    switchCellSegmentVariant(ninoGridCell(0x40, 0x3f), variant);
    switchCellSegmentVariant(ninoGridCell(0x3f, 0x3f), variant);
}
bool noEvent() { return eventState().active == 0; }

} // namespace

void ninoLockEffect(uint32_t setting) {
    const uint32_t e = allocActorA0(kActorStageClass); // exe 0x5e13a0
    if (!e) return;
    EffectActor& light = effectActorAt(e);
    StageLightFields fields(effectActorAt(e));
    const StageLightSetting s = dataRecord<StageLightSetting>(setting, StageLightSetting::kBytes);
    light.flags = kActorLive;
    light.type = kStageLightType;
    light.variant = 0;
    fields.size = kLockLightSize;
    fields.mode = 0;
    fields.colourA = s.colourA;
    fields.colourB = s.colourB;
    fields.pulse = 0;
}
uint32_t ninoGridCell(uint32_t column, uint32_t row) { return GridCell(gridCellAtIndex(column, row)).head; }

bool ch2RoomCaseKnown(uint8_t stage) { return stage == kSt15 || stage == kSt16; }
// The inline stage cases of the room scripts' tick (story independent; the room handler's word): ST15 0x57eeb0 /
// ST16 0x57f040 -- the missions of the next island. Step 0: flag 0x710; 1: the opening (message 0x10 / 0x13) -> 2: the
// mission's flag (0x28 / 0x27) and no event -> script 0x35 / 0x34 (ST16 also clears 0x26), flag 0x2f cleared, 3; else
// flag 0x2f (the mission failed; ST16 only while the player lives, window 4 closed) -> the event message 0x1c (param
// 0xb: back to the Sulphur-Bottom), 3. Then ST16: flag 0x26, no event, no request -> script 0x33 (no door test that
// tick); the flag-0x700 door test (game/transition).
bool ch2RoomCase() {
    const uint8_t stage = gameState().stage;
    if (!ch2RoomCaseKnown(stage)) return false;
    const bool st16 = stage == kSt16;
    RoomScriptState& room = roomScriptState();
    switch (room.handlerStep) {
    case 0:
        setStoryFlag(kFlagRoomActive);
        if (st16) room.enterHandlerStepWord(1); // ST16 writes the low word
        else room.handlerStep = 1;
        [[fallthrough]];
    case 1:
        if (missionOpening(st16 ? kMessageCrane : kMessageCannon)) room.handlerStep = 2;
        break;
    case 2:
        if (storyFlag(st16 ? kFlagCraneMission : kFlagCannonMission) && noEvent()) {
            scriptRun(st16 ? kScriptCraneDone : kScriptCannonDone);
            if (st16) clearStoryFlag(kFlagCaught);
            clearStoryFlag(kFlagMissionFailed);
            room.handlerStep = 3;
        } else if (storyFlag(kFlagMissionFailed) && (!st16 || player().hitPoints >= 0)) {
            if (st16) windowClose(kMissionWindow, WindowClose::Force);
            eventStartAt(0, nullptr, kEventGoBack, 0, kMessageFailed);
            room.handlerStep = 3;
        }
        break;
    default: break;
    }
    if (st16 && storyFlag(kFlagCaught) && noEvent() && transitionRequest().state == 0) scriptRun(kScriptCaught);
    return true;
}

bool ch2RoomHandler(RoutineId fn, bool run) {
    RoomScriptState& room = roomScriptState();
    switch (fn) {
    case RoutineId::St46Story5To9Sub0RoomHandler:
        // ST46 sub 0, stories 5..9: once (flag 0x24) the story + 1 (exe 0x512800) and script 0x31
        if (!run) return true;
        if (!storyFlag(kFlagTakeOff)) {
            gameState().nextStory += 1u;
            scriptRun(kScriptTakeOff);
            setStoryFlag(kFlagTakeOff);
        }
        return true;
    case RoutineId::St46Story5To9Sub3RoomHandler:
        // ST46 sub 3, stories 5..9: the player's hit points 0; step 0 -> script 0x3c (not ported), step 1
        if (!run) return true;
        player().hitPoints = 0;
        if (room.handlerStep == 0) {
            scriptRun(kScriptSt46Sub3);
            room.handlerStep = 1;
        }
        return true;
    case RoutineId::St17Story6Sub0RoomHandler: // ST17 sub 0, story 6
        if (!run) return true;
        switch (room.handlerStep) {
        case 0:
            setStoryFlag(kFlagRoomActive);
            if (!storyFlag(kFlagLanded)) { // the first visit: script 0x3e (the landing)
                scriptRun(kScriptLanding);
                setStoryFlag(kFlagLanded);
                room.enterHandlerStep(1);
            } else if (!storyFlag(kFlagLookAround) && noEvent()) {
                soundPlayFlatForced(0xd);
                scriptRun(kScriptLookAround);
                setStoryFlag(kFlagLookAround);
                room.enterHandlerStep(2);
            }
            return true;
        case 2:
            if (scriptState().flags == 0 && missionOpening(kMessageShips)) room.handlerStep = 3;
            return true;
        case 3:
            if (storyFlag(kFlagShipMission) && noEvent()) {
                scriptRun(kScriptShipsDone);
                clearStoryFlag(kFlagMissionFailed);
                room.enterHandlerStep(4);
            } else if (storyFlag(kFlagMissionFailed)) {
                eventStartAt(0, nullptr, kEventGoBack, 0, kMessageGoBack); // exe 0x543b40
                room.enterHandlerStep(4);
            }
            return true;
        default: return true;
        }
    // ST19 (table 0x8c7978 by story; 4 subs; story 6 = 0x8c7928, 7 / 8 = 0x8c7938)
    case RoutineId::St19Story6Sub0RoomHandler:
        // sub 0, story 6: once records 0x8c7b18 / 0x8c7b2c / 0x8c7b40, the locks 0x711..0x713 set (0x710 /
        // 0x714 / 0x715 cleared); by flag 0x29 / 0x28 / 0x27 / 0x25 one lock opened + its light
        if (!run) return true;
        if (room.handlerStep != 0) return true;
        room.handlerStep = 1;
        spawnRecordTracked(stateAddress(TableId::Ch2RoomsSt19FirstLockRecord));
        spawnRecordTracked(stateAddress(TableId::Ch2RoomsSt19SecondLockRecord));
        spawnRecordTracked(stateAddress(TableId::Ch2RoomsSt19ThirdLockRecord));
        clearStoryFlag(kFlagRoomActive);
        clearStoryFlag(kFlagLockD);
        clearStoryFlag(kFlagLockE);
        setStoryFlag(kFlagLockA);
        setStoryFlag(kFlagLockB);
        setStoryFlag(kFlagLockC);
        if (storyFlag(kFlagShipMission)) return true;
        if (storyFlag(kFlagCannonMission)) {
            setStoryFlag(kFlagLockA);
            setStoryFlag(kFlagLockB);
            clearStoryFlag(kFlagLockC);
            ninoLockEffect(stateAddress(TableId::Ch2RoomsLockLightC));
        } else if (storyFlag(kFlagCraneMission)) {
            setStoryFlag(kFlagLockA);
            clearStoryFlag(kFlagLockB);
            setStoryFlag(kFlagLockC);
            ninoLockEffect(stateAddress(TableId::Ch2RoomsLockLightB));
        } else if (storyFlag(kFlagCityGate)) {
            clearStoryFlag(kFlagLockA);
            setStoryFlag(kFlagLockB);
            setStoryFlag(kFlagLockC);
            ninoLockEffect(stateAddress(TableId::Ch2RoomsLockLightA));
        }
        return true;
    case RoutineId::St19RoomHandler:
        // subs 1 (stories 6..8) / 5: step 0 -> 1 (2 with flag 0x25; 0x710 cleared); 1: flags 0x580 + 0x581,
        // no script / event -> flag 0x25 + script 0x32 once
        if (!run) return true;
        if (room.handlerStep == 0) {
            clearStoryFlag(kFlagRoomActive);
            room.enterHandlerStep(storyFlag(kFlagCityGate) ? 2 : 1);
        } else if (room.handlerStep == 1 && room.handlerByte2 == 0 && storyFlag(kFlagKeyA) && storyFlag(kFlagKeyB) &&
                   scriptState().flags == 0 && noEvent()) {
            setStoryFlag(kFlagCityGate);
            scriptRun(kScriptCityGate);
            room.handlerByte2 = 1;
        }
        return true;
    case RoutineId::St19Story6Sub3RoomHandler: // sub 3, story 6: without flag 0x25 -> flag 0x25 + script 0x32 once
        if (!run) return true;
        if (room.handlerStep == 0 && !storyFlag(kFlagCityGate)) {
            setStoryFlag(kFlagCityGate);
            scriptRun(kScriptCityGate);
            room.handlerStep += 1u;
        }
        return true;
    case RoutineId::St19Story7To9Sub0RoomHandler:
        // sub 0, stories 7..9: once 0x710 / 0x713..0x715 cleared, 0x711 / 0x712 set, the third lock's light
        if (!run) return true;
        if (room.handlerStep != 0) return true;
        clearStoryFlag(kFlagRoomActive);
        setStoryFlag(kFlagLockA);
        setStoryFlag(kFlagLockB);
        clearStoryFlag(kFlagLockC);
        clearStoryFlag(kFlagLockD);
        clearStoryFlag(kFlagLockE);
        ninoLockEffect(stateAddress(TableId::Ch2RoomsLockLightC));
        room.handlerStep = 1;
        return true;
    // ST18 (table 0x8c76e0; story 6): sub 0 0x57f620: 0: flag 0x710 cleared, the floor's 4 cells to variant 0; flag 0x2a
    // -> 0x710 set, step 4; without 0x2b with 0x29 (the three missions done) -> 0x710 set, records 0x8c7738 x 3
    // (Glyde's ship), sound 0x21, the opening (message 0x1e), step 2; else record 0x8c77a0, step 1. 2: the opening -> 3:
    // flag 0x2a, no event -> script 0x37, 0x2f cleared, 4; flag 0x2f -> the event message 0x3b (param 0xb), 1. 4: flag
    // 0x2b, no event -> story + 1, script 0x38, 5.
    case RoutineId::St18RoomHandler:
        if (!run) return true;
        switch (room.handlerStep) {
        case 0:
            clearStoryFlag(kFlagRoomActive);
            st18FloorCells(0);
            if (storyFlag(kFlagShipFight)) {
                setStoryFlag(kFlagRoomActive);
                room.handlerStep = 4;
            } else if (!storyFlag(kFlagGunshipDown) && storyFlag(kFlagShipMission)) {
                setStoryFlag(kFlagRoomActive);
                spawnRecords(stateAddress(TableId::Ch2RoomsSt18ShipFightSpawns), 3);
                soundPlayFlatForced(0x21);
                missionOpening(kMessageShips);
                room.enterHandlerStepWord(2);
            } else {
                spawnRecords(stateAddress(TableId::NinoReturnRoomsShoreSpawns), 1);
                room.handlerStep = 1;
            }
            return true;
        case 2:
            if (missionOpening(kMessageShips)) room.handlerStep = 3;
            return true;
        case 3:
            if (storyFlag(kFlagShipFight) && noEvent()) {
                scriptRun(kScriptShipFight);
                clearStoryFlag(kFlagMissionFailed);
                room.enterHandlerStepWord(4);
            } else if (storyFlag(kFlagMissionFailed)) {
                eventStartAt(0, nullptr, kEventGoBack, 0, kMessageGoBack);
                room.handlerStep = 1;
            }
            return true;
        case 4:
            if (storyFlag(kFlagGunshipDown) && noEvent()) {
                gameState().nextStory += 1u; // exe 0x512800
                scriptRun(kScriptGunshipDown);
                room.handlerStep = 5;
            }
            return true;
        default: return true;
        }
    case RoutineId::St18Story7OnSub0RoomHandler:
        // ST18 sub 0, stories 7..: once the floor's 4 cells to variant 0, 0x710 cleared, record 0x8c77a0 x 1
        if (!run) return true;
        if (room.handlerStep == 0) {
            st18FloorCells(0);
            clearStoryFlag(kFlagRoomActive);
            spawnRecords(stateAddress(TableId::NinoReturnRoomsShoreSpawns), 1);
            room.handlerStep = 1;
        }
        return true;
    case RoutineId::St17RoomHandler:
        // ST17 sub 0 at stories 7 / 8 (sub 3 at story 6): once 0x710 cleared, records 0x8c7468 x 2 +
        // 0x8c75d8 x 1, step 1 (the word)
        if (!run) return true;
        if (room.handlerStep == 0) {
            clearStoryFlag(kFlagRoomActive);
            spawnRecords(stateAddress(TableId::NinoReturnRoomsReturnRoomSpawns), 2);
            spawnRecords(stateAddress(TableId::Ch2RoomsSt17LateSpawns), 1);
            room.enterHandlerStep(1);
        }
        return true;
    case RoutineId::St19Story7OnSub5RoomHandler:
        // ST19 sub 5, stories 7..: once 0x710 cleared (step 1); flag 0x38 -> flag 0x39
        if (!run) return true;
        if (room.handlerStep == 0) {
            clearStoryFlag(kFlagRoomActive);
            room.enterHandlerStep(1);
        }
        if (storyFlag(kFlagPlantA)) setStoryFlag(kFlagPlantB);
        return true;
    case RoutineId::St18Story6Sub2RoomHandler: // ST18 sub 2, story 6: once 0x710 cleared, records 0x8c7778 x 2, step 1
        if (!run) return true;
        if (room.handlerStep == 0) {
            clearStoryFlag(kFlagRoomActive);
            spawnRecords(stateAddress(TableId::NinoReturnRoomsMachineSpawns), 2);
            room.enterHandlerStep(1);
        }
        return true;
    // ST1B (table 0x8c7d08 by story; story 6 = 0x8c7cb8: subs 0..3)
    case RoutineId::St4dLockCleared: // sub 0: flag 0x710 cleared
        if (run) clearStoryFlag(kFlagRoomActive);
        return true;
    case RoutineId::St1BStory6Sub2RoomHandler:
        // sub 2, story 6 (the arrival from script 0x32): once records 0x8c7ea8 x 2 (0x710 cleared); then
        // without flag 0x25, with 0x580 + 0x581, no script / event: the event message 0x3b (param 0xb), step 2
        if (!run) return true;
        if (room.handlerStep == 0) {
            spawnRecords(stateAddress(TableId::Ch2RoomsSt1BArrivalSpawns), 2);
            clearStoryFlag(kFlagRoomActive);
            room.handlerStep += 1u;
        } else if (room.handlerStep != 1) {
            return true;
        }
        if (!storyFlag(kFlagCityGate) && storyFlag(kFlagKeyA) && storyFlag(kFlagKeyB) && scriptState().flags == 0 &&
            noEvent()) {
            eventStartAt(0, nullptr, kEventGoBack, 0, kMessageGoBack); // exe 0x543b40
            room.handlerStep += 1u;
        }
        return true;
    case RoutineId::St1BStory6Sub3RoomHandler: // sub 3, story 6: once 0x710 cleared, with flag 0x39 record 0x8c7ed0
        if (!run) return true;
        if (room.handlerStep == 0) {
            clearStoryFlag(kFlagRoomActive);
            if (storyFlag(kFlagPlantB)) spawnRecords(stateAddress(TableId::Ch2RoomsSt1BPlantSpawns), 1);
            room.handlerStep += 1u;
        }
        return true;
    default: return false;
    }
}

} // namespace od::port
