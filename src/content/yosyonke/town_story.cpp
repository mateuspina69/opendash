// The story chapter after the crash landing (story 0, ST08 / ST09 / ST0A / ST0D / ST0E / ST0F): the scripts 0x4d (the
// junk shop), 0x4e (Roll runs off to Joe), 0x4f (Joe's lab) and 0x71 (the failed train defence), and the chapter's
// room-script handlers (see town_story.hpp).
// test_story).
//
// The scripts 0x4d / 0x4e / 0x4f: state 0 sets up the camera script / the steps, the replay pad, the player's move bytes
// cleared, the script's own bytes cleared, and once the player is script driven the face tracks; state 1 = exe 0x531310
// (game/script_engine scriptStepsStateHeadTurn: the step routine, the skip, the player's head turned toward the
// script's head target, the camera script, the face tracks); state 2 the wrap up. The step routines keep their phases
// in the sub-state and the two phase bytes, a countdown / the player's turn and the door sounds' timer in the script's
// own bytes.
#include "content/yosyonke/town_story.hpp"
#include "content/yosyonke/town_story_state.hpp"

#include "game/actor_pools.hpp"
#include "game/actor_sweeps.hpp"
#include "game/audio_logic.hpp"
#include "game/draw_commands.hpp"
#include "game/event.hpp"
#include "game/event_state.hpp"
#include "game/fade_state.hpp"
#include "game/field_alias.hpp"
#include "game/hud.hpp"
#include "game/message.hpp"
#include "game/message_state.hpp"
#include "game/player/player_input.hpp"
#include "game/player/player_script_control.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/room_scripts.hpp"
#include "game/script_engine.hpp"
#include "game/script_state.hpp"
#include "game/spawn.hpp"
#include "game/story.hpp"
#include "game/story_flags.hpp"
#include "game/title.hpp"
#include "game/transition.hpp"

using namespace od::port::town_story_state;

namespace od::port {

namespace {

constexpr const char* kTownStoryPort = "town story port"; // the area of its port-gap errors

ScriptState& block() { return scriptState(); }
int32_t stepTick() { return block().stepTick; }

ChapterScriptBytes& chapterBytes() { return scriptState().part<ChapterScriptBytes>(); }
// the player's head target (its yaw and its turn a tick)
void headTarget(int16_t yaw, int16_t step) {
    ScriptHeadTarget& head = scriptState().part<ScriptHeadTarget>();
    head.yaw = yaw;
    head.step = step;
}

void message(int16_t id) { windowOpen(0, kMessageBank, id); } // exe 0x557390(0, the stage's bank, id)
bool messageClosed() { return windowStatus(0) == 0; }        // exe 0x557810(0) == 0
// the "next step" tail of the step routines: the sub-state and the phase bytes cleared, the list one step ahead
void nextStep() {
    block().subState = 0;
    scriptNextStep();
}
// exe 0x5e3df0(player, ..): the player turned toward the angle
int playerTurn(uint32_t angle, int step) { return turnYawTo(player().rotation.y, angle, step); }
// the end of the scripts' wrap ups: exe 0x5723c0, 0x511890, the player drawn, exe 0x5116a0
void playerBack() {
    playerBackToPlay();
    scriptReplayPadOff();
    player().flags |= kActorDrawn;
    scriptEndBlock();
}
// the player's move bytes cleared (the move directions, the direction's angles)
void clearPlayerMotion() {
    Player& p = player();
    p.setPrevMoveDir(MoveDir::Forward);
    p.setMoveDir(MoveDir::Forward);
    p.clearDirectionOffsetWords();
}
// the shared part of the states 0
void scriptSetup(uint32_t cameraScript, uint32_t steps) {
    scriptSequenceStart(cameraScript, steps);
    scriptReplayPadOn();
}
// the second phase of the states 0: once the player is script driven the face tracks on their tables, state 1
void scriptSetUpTail(uint32_t eyes, uint32_t mouth) {
    ScriptState& s = block();
    if (playerToScriptIfFree() == 0) {
        s.faceTracks.init(eyes, mouth);
        s.faceTracks.first.select(0, 0);
        s.faceTracks.second.select(0, 0);
        s.enterState(CutsceneState::Steps);
    }
    scriptSequenceTick();
}
// the actors the wrap ups sweep away (the classes of groups 0 / 2 but the controllers, the projectiles)
constexpr uint32_t kSweepScene = kSweepAnimatedGroup0 | kSweepAnimatedGroup2 | kSweepProjectiles |
                                 kSweepStaticsGroup2 | kSweepEffectsGroup2;
constexpr int16_t kDoorX = -0x1b0, kDoorZ = 0x100; // where the door sounds play
constexpr uint16_t kDoorKnock = 0xb8, kDoorOpen = 0xb9;
constexpr uint32_t kFlagShopSeen = 0x680; // the junk shop's clerk steps back (script 0x4d)

// the "Wait! Joe!" sounds of exe 0x5308e0 / 0x530ed0 (the second phase byte: arm the timer, the knock, the door)
void doorSounds(uint16_t first) {
    ScriptState& s = block();
    ChapterScriptBytes& f = chapterBytes();
    switch (s.stepSubPhase) {
    case 0:
        s.stepSubPhase = 1;
        f.soundTimer = first;
        return;
    case 1:
        --f.soundTimer;
        if (f.soundTimer != 0) return;
        f.soundTimer = first == 0x50 ? uint16_t(0x32) : uint16_t(0x1e);
        soundPlayAtXyz(kDoorKnock, kDoorX, 0, kDoorZ);
        ++s.stepSubPhase;
        return;
    case 2:
        --f.soundTimer;
        if (f.soundTimer != 0) return;
        soundPlayAtXyz(kDoorOpen, kDoorX, 0, kDoorZ);
        ++s.stepSubPhase;
        return;
    default: return;
    }
}

// ---- the wrap ups --------------------------------------------------------------------------------------------------
// exe 0x5306c0 (script 0x4d)
void junkShopWrapUp() {
    ScriptState& s = block();
    switch (s.subState) {
    case 0:
        padVibrate(0, 0x1000000);
        ++s.subState;
        if (!(s.flags & kScriptSkipTaken)) scriptFade(0x12, 0);
        s.subState = 2;
        return;
    case 1: s.subState = 2; return;
    case 2:
        if (fadeState().busy != 0) return;
        windowCloseAll(WindowClose::Force);
        actorsSweep(kSweepScene, kSweepFree);
        scriptActorSpawnRecord(stateAddress(TableId::TownStoryJunkShopSceneRecord));
        s.subState = 3;
        if (fadeState().covered != 0) scriptFade(1, 0);
        return;
    case 3:
        player().rotation.y = int16_t(3 * kQuarterTurn);
        playerBack();
        return;
    default: return;
    }
}
// exe 0x5310f0 (script 0x4e)
void rollRunsOffWrapUp() {
    ScriptState& s = block();
    switch (s.subState) {
    case 0:
        padVibrate(0, 0x1000000);
        s.subState = 2;
        return;
    case 1: s.subState = 2; return;
    case 2:
        if (fadeState().busy != 0) return;
        scriptActorFreeRecord(stateAddress(TableId::TownStoryRollRunsOffRecord));
        s.subState = 3;
        if (fadeState().covered != 0) scriptFade(1, 0);
        return;
    case 3: playerBack(); return;
    default: return;
    }
}
// exe 0x531400 (script 0x4f)
void joesLabWrapUp() {
    ScriptState& s = block();
    switch (s.subState) {
    case 0:
        padVibrate(0, 0x1000000);
        if (!(s.flags & kScriptSkipTaken)) scriptFade(0x12, 0);
        s.subState = 2;
        return;
    case 1: s.subState = 2; return;
    case 2:
        if (fadeState().busy != 0) return;
        windowCloseAll(WindowClose::Force);
        s.subState = 3;
        actorsSweep(kSweepScene, kSweepFree);
        scriptActorFreeRecord(stateAddress(TableId::TownStoryJoesLabSceneFirstRecord));
        scriptActorFreeRecord(stateAddress(TableId::TownStoryJoesLabSceneSecondRecord));
        scriptActorSpawnRecord(stateAddress(TableId::TownStoryJoesLabRollRecord)); // Roll and Maria stay in the lab (the town walkers 0x441950)
        scriptActorSpawnRecord(stateAddress(TableId::TownStoryJoesLabMariaRecord));
        if (fadeState().covered != 0) scriptFade(1, 0);
        return;
    case 3:
        player().rotation.y = kQuarterTurn;
        playerBack();
        soundPlayFlatForced(0x15);
        return;
    default: return;
    }
}

// ---- the steps of script 0x4d (0x8a9a60) ----------------------------------------------------------------------------
// exe 0x530790: Roll's message 0xc8, her face sequence 1
void rollGreetsStep() {
    ScriptState& s = block();
    if (s.subState == 0) {
        playerScriptAnimId(0, 0, 1);
        message(0xc8);
        s.subState = 1;
        s.faceTracks.first.select(1, 0);
    } else if (s.subState != 1) {
        return;
    }
    if (messageClosed()) nextStep();
}
// exe 0x530800: the messages 0xc9..0xcb, flag 0x680 (the clerk steps back), 0x10 ticks
void clerkTalksStep() {
    ScriptState& s = block();
    ChapterScriptBytes& f = chapterBytes();
    switch (s.subState) {
    case 0:
        message(0xc9);
        s.subState = 1;
        [[fallthrough]];
    case 1:
        if (!messageClosed()) return;
        s.subState = 2;
        message(0xca);
        return;
    case 2:
        if (!messageClosed()) return;
        s.subState = 3;
        message(0xcb);
        return;
    case 3:
        if (!messageClosed()) return;
        setStoryFlag(kFlagShopSeen);
        s.subState = 4;
        f.counter = 0x10;
        return;
    case 4:
        --f.counter;
        if (f.counter == 0) nextStep();
        return;
    default: return;
    }
}
// exe 0x5308e0: the message 0xcc; the door sounds
void doorKnockStep() {
    ScriptState& s = block();
    if (s.subState == 0) {
        message(0xcc);
        headTarget(0, 0x20);
        s.subState = 1;
    }
    if (s.subState == 1 && stepTick() >= 0x8c && messageClosed()) nextStep();
    doorSounds(0x50);
}
// exe 0x5309e0: the message 0xcd, the player turns (0x770), the messages 0xce..0xd1
void turnToDoorStep() {
    ScriptState& s = block();
    ChapterScriptBytes& f = chapterBytes();
    switch (s.subState) {
    case 0:
        message(0xcd);
        headTarget(0, 0);
        f.counter = 0;
        s.subState = 1;
        [[fallthrough]];
    case 1:
        switch (s.stepPhase) {
        case 0:
            scriptState().part<ScriptHeadTarget>().yaw = 0x970;
            f.counter = 4;
            scriptState().part<ScriptHeadTarget>().step = 0x80;
            s.stepPhase = 1;
            [[fallthrough]];
        case 1:
            --f.counter;
            if (f.counter != 0) return;
            f.turnYaw = 0x770;
            s.stepPhase = 2;
            [[fallthrough]];
        case 2:
            if (playerTurn(uint32_t(int32_t(f.turnYaw)), 0x40) == 0) s.stepPhase = 3;
            return;
        case 3:
            if (!messageClosed()) return;
            scriptActorFreeRecord(stateAddress(TableId::TownStoryTurnToDoorRecord));
            s.subState = 2;
            message(0xce);
            return;
        default: return;
        }
    case 2:
        if (!messageClosed()) return;
        s.subState = 3;
        message(0xcf);
        return;
    case 3:
        if (!messageClosed()) return;
        s.subState = 4;
        message(0xd0);
        return;
    case 4:
        if (!messageClosed()) return;
        s.subState = 5;
        s.stepPhase = 0;
        message(0xd1);
        return;
    case 5:
        if (s.stepPhase == 0) {
            headTarget(0x300, 0xa0);
            s.stepPhase = 1;
        } else if (s.stepPhase != 1) {
            return;
        }
        if (messageClosed()) nextStep();
        return;
    default: return;
    }
}
// exe 0x530bb0: the player walks (animation 2) toward the counter, then stops
void walkToCounterStep() {
    ScriptState& s = block();
    if (s.subState == 0) {
        headTarget(0x380, 0xa0);
        s.subState = 1;
    } else if (s.subState != 1) {
        return;
    }
    Player& p = player();
    // the counter's point {0x20, -0x10, -0xb0} (the low words 0) seen from the player
    const int ang = angleOf(int32_t(uint32_t(uint16_t(0x20)) << 16), int32_t(uint32_t(uint16_t(-0xb0)) << 16),
                            p.pos.x, p.pos.z);
    const int32_t t = stepTick();
    switch (s.stepPhase) {
    case 0:
        playerTurn(uint32_t(ang), 0x40);
        if (t == 5) {
            playerScriptAnimId(2, 0, 1);
            s.stepPhase = 1;
        }
        return;
    case 1:
        playerTurn(uint32_t(ang), 0x40);
        moveAlongYaw(p.pos, ang, 0, 0, -0x50);
        if (t == 0x2a) s.stepPhase = 2;
        return;
    case 2:
        playerTurn(kQuarterTurn, 0x10);
        moveAlongYaw(p.pos, ang, 0, 0, -0x50);
        if (t == 0x2d) {
            playerScriptAnimId(0, 0, 0);
            s.stepPhase = 3;
        }
        return;
    case 3:
        if (t == 0x6e) nextStep();
        return;
    default: return;
    }
}
// exe 0x530d00: the message 0xd2, the face sequence 1
void rollExplainsStep() {
    ScriptState& s = block();
    if (s.subState == 0) {
        message(0xd2);
        s.faceTracks.first.select(1, 0);
        s.subState = 1;
    } else if (s.subState != 1) {
        return;
    }
    if (stepTick() >= 0x50 && messageClosed()) nextStep();
}
// exe 0x530d70: the messages 0xd3..0xd6, the player turns (0x100)
void shopTalkStep() {
    ScriptState& s = block();
    ChapterScriptBytes& f = chapterBytes();
    switch (s.subState) {
    case 0:
        message(0xd3);
        s.subState = 1;
        [[fallthrough]];
    case 1:
        if (!messageClosed()) return;
        s.subState = 2;
        message(0xd4);
        return;
    case 2:
        if (!messageClosed()) return;
        s.subState = 3;
        message(0xd5);
        return;
    case 3:
        if (!messageClosed()) return;
        s.subState = 4;
        message(0xd6);
        return;
    case 4:
        switch (s.stepPhase) {
        case 0:
            headTarget(0xf00, 0x80);
            f.counter = 4;
            s.stepPhase = 1;
            [[fallthrough]];
        case 1:
            --f.counter;
            if (f.counter != 0) return;
            f.turnYaw = 0x100;
            s.stepPhase = 2;
            [[fallthrough]];
        case 2:
            if (playerTurn(uint32_t(int32_t(f.turnYaw)), 0x80) == 0) s.stepPhase = 3;
            return;
        case 3:
            if (messageClosed()) nextStep();
            return;
        default: return;
        }
    default: return;
    }
}
// exe 0x530ed0: "Roll! Wait!" (0xd7): the player turns; Roll freed, the wrap up
void rollLeavesStep() {
    ScriptState& s = block();
    ChapterScriptBytes& f = chapterBytes();
    switch (s.subState) {
    case 0:
        headTarget(int16_t(3 * kQuarterTurn), 0x20);
        f.counter = 4;
        message(0xd7);
        s.subState = 1;
        [[fallthrough]];
    case 1:
        --f.counter;
        if (f.counter != 0) break;
        f.turnYaw = 0x100;
        s.subState = 2;
        [[fallthrough]];
    case 2:
        if (playerTurn(uint32_t(int32_t(f.turnYaw.get())), 0x80) == 0) s.subState = 3;
        break;
    case 3:
        if (stepTick() >= 0x50 && messageClosed()) {
            scriptActorFreeRecord(stateAddress(TableId::TownStoryRollLeavesRecord));
            s.enterState(CutsceneState::WrapUp);
            junkShopWrapUp();
            return;
        }
        break;
    default: break;
    }
    doorSounds(0x23);
}
// ---- the step of script 0x4e (0x8a9b60) ----------------------------------------------------------------------------
// exe 0x531180: the player's head follows Roll leaving (its target keyed on the tick)
void watchRollLeaveStep() {
    ScriptState& s = block();
    if (s.subState == 0) {
        headTarget(0, 0x40);
        playerScriptAnimId(0, 0, 1);
        s.subState = 1;
    } else if (s.subState != 1) {
        return;
    }
    const int32_t t = stepTick();
    if (s.stepPhase == 0) {
        if (t == 0xf) {
            headTarget(0xd00, 0x10);
            s.stepPhase = 1;
        }
    } else if (s.stepPhase == 1 && t == 0x21) {
        headTarget(0xf60, 4);
        s.stepPhase = 2;
    }
    if (t == 0x96) nextStep();
}
// ---- the steps of script 0x4f (0x8a9d08) ---------------------------------------------------------------------------
// exe 0x5314f0: the player looks around the lab
void lookAroundLabStep() {
    ScriptState& s = block();
    if (s.subState == 0) {
        playerScriptAnimId(0, 0, 1);
        s.subState = 1;
    } else if (s.subState != 1) {
        return;
    }
    const int32_t t = stepTick();
    if (s.stepPhase == 0) {
        if (t == 0x1e) {
            headTarget(kHalfTurn, 0x80);
            s.stepPhase = 1;
        }
    } else if (s.stepPhase == 1 && t == 0x28) {
        headTarget(0x300, 0x20);
        s.stepPhase = 2;
    }
    if (t == 0x96) nextStep();
}
// exe 0x531570: the player walks in, the messages 0x32 / 0x33, Maria's record
void walkIntoLabStep() {
    ScriptState& s = block();
    ChapterScriptBytes& f = chapterBytes();
    Player& p = player();
    if (s.subState == 0) {
        p.pos.x.whole = -0xb0;
        p.pos.y.whole = 0;
        p.pos.z.whole = 0x150;
        p.rotation.y = 0xff0;
        headTarget(0xff0, 0x40);
        s.subState = 1;
        return;
    }
    if (s.subState != 1) return;
    const int32_t t = stepTick();
    switch (s.stepPhase) {
    case 0:
        if (t == 0x1e) {
            playerScriptAnimId(2, 6, 1);
            ++s.stepPhase;
        }
        return;
    case 1:
        moveAlongYaw(p.pos, 0xff0, 0, 0, -0x28);
        if (t == 0x6c) {
            playerScriptAnimId(0, 0, 1);
            headTarget(0x300, 0x40);
            message(0x32);
            ++s.stepPhase;
        }
        return;
    case 2:
        if (!messageClosed()) return;
        ++s.stepPhase;
        message(0x33);
        return;
    case 3:
        if (!messageClosed()) return;
        f.counter = 5;
        ++s.stepPhase;
        headTarget(0xd00, 0x60);
        return;
    case 4:
        --f.counter;
        if (f.counter != 0) return;
        s.subState = 0;
        s.clearStepPhase();
        scriptActorSpawnRecord(stateAddress(TableId::TownStoryJoesLabSceneSecondRecord));
        scriptSequenceSkip(1);
        return;
    default: return;
    }
}
// exe 0x5316f0: the message 0x34
void labMessageStep() {
    ScriptState& s = block();
    if (s.subState != 0) return;
    if (s.stepPhase == 0) {
        message(0x34);
        ++s.stepPhase;
    } else if (s.stepPhase != 1) {
        return;
    }
    if (messageClosed()) nextStep();
}
// exe 0x531750: the message 0x35
void labReplyStep() {
    ScriptState& s = block();
    if (s.subState == 0) {
        message(0x35);
        s.subState = 1;
        return;
    }
    if (s.subState == 1 && s.stepPhase == 0 && messageClosed()) nextStep();
}
// exe 0x5317b0: the message 0x39, the player turns (0x220); closed -> the wrap up
void labFarewellStep() {
    ScriptState& s = block();
    if (s.subState == 0) {
        message(0x39);
        s.subState = 1;
    } else if (s.subState != 1) {
        return;
    }
    switch (s.stepPhase) {
    case 0:
        s.stepPhase = 1;
        headTarget(0x380, 0x60);
        [[fallthrough]];
    case 1:
        if (playerTurn(0x220, 0x40) == 0) ++s.stepPhase;
        return;
    case 2:
        if (!messageClosed()) return;
        s.subState = 0;
        s.enterState(CutsceneState::WrapUp);
        joesLabWrapUp();
        return;
    default: return;
    }
}
// the step routines of the three scripts, by the named routine
void chapterStep(const RoutineRef& routine) {
    switch (routine.id()) {
    case RoutineId::TownChapterRollGreetsStep: rollGreetsStep(); return;
    case RoutineId::TownChapterClerkTalksStep: clerkTalksStep(); return;
    case RoutineId::TownChapterDoorKnockStep: doorKnockStep(); return;
    case RoutineId::TownChapterTurnToDoorStep: turnToDoorStep(); return;
    case RoutineId::TownChapterWalkToCounterStep: walkToCounterStep(); return;
    case RoutineId::TownChapterRollExplainsStep: rollExplainsStep(); return;
    case RoutineId::TownChapterShopTalkStep: shopTalkStep(); return;
    case RoutineId::TownChapterRollLeavesStep: rollLeavesStep(); return;
    case RoutineId::TownChapterWatchRollLeaveStep: watchRollLeaveStep(); return;
    case RoutineId::TownChapterLookAroundLabStep: lookAroundLabStep(); return;
    case RoutineId::TownChapterWalkIntoLabStep: walkIntoLabStep(); return;
    case RoutineId::TownChapterLabMessageStep: labMessageStep(); return;
    case RoutineId::TownChapterLabReplyStep: labReplyStep(); return;
    case RoutineId::TownChapterLabFarewellStep: labFarewellStep(); return;
    default: notPortedAt(kTownStoryPort, "town script step", routine.pointerValue());
    }
}

// ---- the scripts ------------------------------------------------------------------------------------------------------
enum class SetUpSub : uint8_t { Start = 0, WaitPlayer = 1 };
// exe 0x5305d0 (script 0x4d, its states 0x8a9af0)
void junkShopScript() {
    ScriptState& s = block();
    switch (s.cutsceneState()) {
    case CutsceneState::SetUp: {
        const uint8_t st = s.subState;
        if (SetUpSub(st) == SetUpSub::Start) {
            scriptSetup(stateAddress(TableId::TownStoryJunkShopCameraScript), stateAddress(TableId::TownStoryJunkShopSteps));
            Player& p = player();
            p.pos.x.whole = 0xf0;
            p.pos.y.whole = -0x10;
            p.pos.z.whole = -0x90;
            p.rotation.y = kQuarterTurn;
            clearPlayerMotion();
            s.clearScriptByteRun();
            s.subState = uint8_t(SetUpSub::WaitPlayer);
        } else if (SetUpSub(st) != SetUpSub::WaitPlayer) {
            scriptSequenceTick();
            return;
        }
        scriptSetUpTail(stateAddress(TableId::TownStoryJunkShopEyes), stateAddress(TableId::TownStoryJunkShopMouth));
        return;
    }
    case CutsceneState::Steps: scriptStepsStateHeadTurn(chapterStep); return;
    case CutsceneState::WrapUp: junkShopWrapUp(); return;
    default: notPortedAt(kTownStoryPort, "script 0x4d state", s.state);
    }
}
// exe 0x531030 (script 0x4e, its states 0x8a9bb8)
void rollRunsOffScript() {
    ScriptState& s = block();
    switch (s.cutsceneState()) {
    case CutsceneState::SetUp: {
        const uint8_t st = s.subState;
        if (SetUpSub(st) == SetUpSub::Start) {
            scriptSetup(stateAddress(TableId::TownStoryRollRunsOffCameraScript), stateAddress(TableId::TownStoryRollRunsOffSteps));
            clearPlayerMotion();
            s.clearScriptByteRun();
            s.subState = uint8_t(SetUpSub::WaitPlayer);
        } else if (SetUpSub(st) != SetUpSub::WaitPlayer) {
            scriptSequenceTick();
            return;
        }
        scriptSetUpTail(stateAddress(TableId::TownStoryRollRunsOffEyes), stateAddress(TableId::TownStoryRollRunsOffMouth));
        return;
    }
    case CutsceneState::Steps: scriptStepsStateHeadTurn(chapterStep); return;
    case CutsceneState::WrapUp: rollRunsOffWrapUp(); return;
    default: notPortedAt(kTownStoryPort, "script 0x4e state", s.state);
    }
}
// exe 0x531210 (script 0x4f, its states 0x8a9d80)
void joesLabScript(const PlayerEnv& env) {
    ScriptState& s = block();
    switch (s.cutsceneState()) {
    case CutsceneState::SetUp: {
        const uint8_t st = s.subState;
        if (SetUpSub(st) == SetUpSub::Start) {
            scriptSetup(stateAddress(TableId::TownStoryJoesLabCameraScript), stateAddress(TableId::TownStoryJoesLabSteps));
            clearPlayerMotion();
            Player& p = player();
            p.pos.x.whole = 0x140;
            p.pos.y.whole = int16_t(actorGround(env, p));
            p.pos.z.whole = -0x10;
            p.rotation.y = 0x380;
            s.clearScriptByteRun();
            s.subState = uint8_t(SetUpSub::WaitPlayer);
            soundFadeOut(0xf, 0x111, 0);
        } else if (SetUpSub(st) != SetUpSub::WaitPlayer) {
            scriptSequenceTick();
            return;
        }
        scriptSetUpTail(stateAddress(TableId::TownStoryJoesLabEyes), stateAddress(TableId::TownStoryJoesLabMouth));
        return;
    }
    case CutsceneState::Steps: scriptStepsStateHeadTurn(chapterStep); return;
    case CutsceneState::WrapUp: joesLabWrapUp(); return;
    default: notPortedAt(kTownStoryPort, "script 0x4f state", s.state);
    }
}

// script 0x71 (exe 0x53d220, its states 0x8ae260; ST0E with flag 0x584: the train attack failed): the sequence
// 0x8ae230 / the steps 0x8ae250 (one no-op step at tick 0x78), a grey tint rising from 0x80 by 2 a tick (exe
// 0x5f3150(0x22vvvvvv), full-screen), at 0xfc every class killed once; the list's end -> the wrap up, the music faded;
// the wrap up: the white screen exe 0x5f30e0 (trainDefenceWhiteScreen), once no fade runs and the music is done the
// SFX voices stopped, the classes killed, fade 0x12, then the game over (the main task's game-over mode, the task
// switch to exe 0x542ff0 as script 0x3c)
constexpr uint8_t kTintFull = 0xfc, kTintStep = 2, kTintStart = 0x80;
constexpr RoutineId kNoOpStep = RoutineId::EmptyRoutine;
enum class TrainLostWrapUp : uint8_t { Start = 0, WaitMusic = 1, GameOver = 2 };
void trainLostScript() {
    ScriptState& s = block();
    switch (s.cutsceneState()) {
    case CutsceneState::SetUp: // exe 0x53d240
        scriptSequenceStart(stateAddress(TableId::TownStoryTrainLostCameraScript), stateAddress(TableId::TownStoryTrainLostSteps)); // exe 0x511390
        scriptReplayPadOn();                      // exe 0x5117f0
        s.enterState(CutsceneState::Steps);
        s.stepPhase = kTintStart;
        scriptSequenceTick(); // exe 0x5114d0 (a tail jump)
        return;
    case CutsceneState::Steps: { // exe 0x53d270
        if (s.stepPhase >= kTintFull) {
            dc::screenTint(0x22ffffff); // exe 0x5f3150
            if (s.stepSubPhase == 0) {
                actorsSweep(kSweepActors, kSweepFree);
                ++s.stepSubPhase;
            }
        } else {
            s.stepPhase += kTintStep;
            const uint32_t v = s.stepPhase;
            dc::screenTint(0x22000000u | v << 16 | v << 8 | v); // exe 0x5f3150
        }
        const ScriptStep step = scriptStepAt(s.steps.value());
        if (step.id == kScriptStepEnd) {
            s.enterState(CutsceneState::WrapUp);
            soundFadeOut(0xf, 0xfff, 0); // exe 0x5d5780
        } else if (step.routine != 0 && !stateRoutine(step.routine).is(kNoOpStep)) {
            notPortedAt(kTownStoryPort, "script 0x71 step", step.routine);
        }
        scriptSequenceTick();
        return;
    }
    case CutsceneState::WrapUp: // exe 0x53d310 (each sub-state draws the white screen: trainDefenceWhiteScreen)
        if (TrainLostWrapUp(s.subState) == TrainLostWrapUp::GameOver) {
            if (fadeState().busy != 0) return;
            scriptReplayPadOff(); // exe 0x511890
            scriptEndBlock();     // exe 0x5116a0
            mainTask().enterMode(MainMode::ToGameOver, 1); // + exe 0x604b06(0x542ff0): the game over
            return;
        }
        if (s.subState > uint8_t(TrainLostWrapUp::GameOver)) return;
        if (TrainLostWrapUp(s.subState) == TrainLostWrapUp::Start) s.subState = uint8_t(TrainLostWrapUp::WaitMusic);
        if (fadeState().busy == 0 && soundDone(0xf)) { // exe 0x5d5900
            ++s.subState;
            soundStopSfxVoices(); // exe 0x5d59b0
            actorsSweep(kSweepActors, kSweepFree);
            scriptFade(0x12, 0); // exe 0x53f6b0
        }
        return;
    default: notPortedAt(kTownStoryPort, "script 0x71 state", s.state);
    }
}

// ---- the chapter's room-script handlers (exe 0x512890: [[table + story * 4] + sub * 4]) --------------------------------
// The handler's bytes: the step (+0) and the bytes after it (game/room_scripts RoomScriptState). exe 0x511090 =
// spawnRecords, exe 0x5110e0 = spawnRecordTracked (game/spawn).
RoomScriptState& rooms() { return roomScriptState(); }
// the tilted props of Joe's lab: their pitch and yaw (the previous rotation with them)
void tilt(uint32_t prop, int16_t pitch, int16_t yaw) {
    Actor& a = actorAt(prop);
    a.prevRotation.x = pitch;
    a.rotation.x = pitch;
    a.prevRotation.y = yaw;
    a.rotation.y = yaw;
}
constexpr uint32_t kFlagShopScene = 0x5c1, kFlagAfterShop = 0x5c2, kFlagLabScene = 0x5c3, kFlagAfterLab = 0x5e1;
// exe 0x57c820 (ST08 story 0 sub 1, Joe's lab): the props, the two tilted ones, Maria + Roll after the scene; the
// first time after the junk shop (flag 0x5c2): the flags 0x5c3 / 0x5e1 and script 0x4f
void joesLabRoom() {
    if (rooms().handlerStep != 0) return;
    spawnRecords(stateAddress(TableId::TownRoomsJoesLabSpawns), 1);
    spawnRecords(stateAddress(TableId::TownStoryJoesLabPropSpawns), 2);
    tilt(spawnRecordTracked(stateAddress(TableId::TownStoryJoesLabFirstTiltedProp)), 0xf50, int16_t(3 * kQuarterTurn));
    tilt(spawnRecordTracked(stateAddress(TableId::TownStoryJoesLabSecondTiltedProp)), 0xec8, 0xe40);
    if (storyFlag(kFlagLabScene)) {
        spawnRecords(stateAddress(TableId::TownStoryJoesLabAfterSceneSpawns), 1);
    } else if (storyFlag(kFlagAfterShop)) {
        setStoryFlag(kFlagLabScene);
        setStoryFlag(kFlagAfterLab);
        scriptRun(0x4f);
    }
    ++rooms().handlerStep;
}
// exe 0x57cd40 (ST0A story 0 sub 0, the junk shop): the first visit (flag 0x5c1) runs script 0x4d; later the clerk
// (0x8c5870) and, one tick later, sound 0x16
void junkShopRoom() {
    uint8_t& st = rooms().handlerStep;
    if (st == 0) {
        if (!storyFlag(kFlagShopScene)) {
            scriptRun(0x4d);
            setStoryFlag(kFlagShopScene);
            st = 2;
            return;
        }
        spawnRecords(stateAddress(TableId::TownRoomsJunkShopClerkSpawns), 1);
        st = 1;
    } else if (st == 1) {
        soundPlayFlatForced(0x16);
        st = 2;
    }
}
// exe 0x57d760 (ST0E, the train attack): flag 0x584 -> script 0x71; the fight not started (flag 0x581 clear): the two
// pirate mechas 0x8c6568 x 2 (type 0x7a + static 0x4e), Megaman at (-0x1d00, -0x680, 0x300) (kept through the warps),
// the mission opening (exe 0x57d8b0 = 0x582f50 with message 0, its step in the handler's byte 1, the banner's y 0x30)
// -> flag 0x581, step 2 -> byte 2 = 3. After it (0x581, no 0x585) with flag 0x5a1: the records 0x8c6590 x 2, Megaman
// placed again, then flag 0x585
constexpr uint32_t kFlagTrainLost = 0x584, kFlagFightStarted = 0x581, kFlagSecondRoom = 0x5a1,
                   kFlagSecondPlaced = 0x585;
enum class OpeningStep : uint8_t { Banner = 0, Freeze = 1, WaitEvent = 2, Done = 3 };
// exe 0x57d8b0(state, 0): the mission's opening; true once done
bool missionOpening() {
    uint8_t& s = rooms().handlerByte1;
    switch (OpeningStep(s)) {
    case OpeningStep::Banner:
        hudBannerShow(0x54, 0x30, HudBannerStyle::MissionStart); // exe 0x5f25e0
        eventStartAt(0, nullptr, 8, 0, 0);                              // exe 0x543b40(0, 0, 8, 0, 0)
        ++s;
        return false;
    case OpeningStep::Freeze:
        actorsSweep(kSweepActors, kSweepToggleRun); // exe 0x5e05d0
        ++s;
        return false;
    case OpeningStep::WaitEvent:
        if (eventState().active != 0) return false;
        ++s;
        hudBanner().hide(); // exe 0x5f2680
        player().inputMode = 0;
        actorsSweep(kSweepActors, kSweepSetRun);
        return true;
    case OpeningStep::Done: return true;
    default: return false;
    }
}
void placeMegaman() {
    Player& p = player();
    p.pos.x = int32_t(0xe3000000u);
    p.pos.y = int32_t(0xf9800000u);
    p.pos.z = 0x3000000;
    p.runFlags |= kActorWarpKept;
}
void trainAttackRoom() {
    uint8_t& st = rooms().handlerStep;
    if (storyFlag(kFlagTrainLost)) {
        scriptRun(0x71);
        return;
    }
    if (!storyFlag(kFlagFightStarted)) {
        switch (st) {
        case 0:
            spawnRecords(stateAddress(TableId::TownStoryTrainAttackFirstSpawns), 2);
            placeMegaman();
            st = 1;
            missionOpening();
            return;
        case 1:
            if (missionOpening()) {
                st = 2;
                rooms().handlerByte1 = 0;
                setStoryFlag(kFlagFightStarted);
            }
            return;
        case 2: rooms().handlerByte2 = 3; return;
        default: return;
        }
    }
    if (storyFlag(kFlagSecondPlaced) || !storyFlag(kFlagSecondRoom)) return;
    switch (st) {
    case 0:
        spawnRecords(stateAddress(TableId::TownStoryTrainAttackSecondSpawns), 2);
        st = 1;
        return;
    case 1:
        placeMegaman();
        st = 2;
        return;
    case 2: setStoryFlag(kFlagSecondPlaced); return;
    default: return;
    }
}
// exe 0x57da20 (ST0F story 0 sub 0, the ruin's entrance): the first time (flag 0x585, + 0x783) Roll (exe 0x4e6110,
// walking in along the path 0x8961f0) and the message trigger 0x37; flag 0x5d0 (Roll fainted) and the locks 0x710 ..
// 0x72f cleared
void ruinEntranceRoom() {
    if (rooms().handlerStep != 0) return;
    if (!storyFlag(kFlagSecondPlaced)) {
        setStoryFlag(0x783);
        setStoryFlag(kFlagSecondPlaced);
        spawnRecords(stateAddress(TableId::TownStoryRuinEntranceSpawns), 2);
    }
    clearStoryFlag(0x5d0);
    clearLockFlags(kLockFlags);
    rooms().handlerStep = 1;
}
// a handler that spawns `n` of the records `list` once a room
void spawnOnce(uint32_t list, uint32_t n) {
    if (rooms().handlerStep != 0) return;
    spawnRecords(list, n);
    ++rooms().handlerStep;
}

} // namespace

bool townRoomHandler(RoutineId fn, bool run) {
    switch (fn) {
    case RoutineId::JoesLabRoomHandler:
        if (run) joesLabRoom();
        return true;
    case RoutineId::JunkShopRoomHandler:
        if (run) junkShopRoom();
        return true;
    case RoutineId::St0DStory0Sub0RoomHandler: // ST0D story 0 sub 0: the walker 0x8c62d8
        if (run) spawnOnce(stateAddress(TableId::TownStorySt0DWalkerSpawns), 1);
        return true;
    case RoutineId::TrainAttackRoomHandler:
        if (run) trainAttackRoom();
        return true;
    case RoutineId::St0DStoryFOnSub0RoomHandler:
        // ST0D stories 0xf.. (sub 0 from 0xf, then later subs): the walker 0x8c62ec x 1
        if (run) spawnOnce(stateAddress(TableId::TownStorySt0DWalkersSpawns), 1);
        return true;
    case RoutineId::St0DWalkersRoomHandler: // ... 0x8c62ec x 2
        if (run) spawnOnce(stateAddress(TableId::TownStorySt0DWalkersSpawns), 2);
        return true;
    case RoutineId::St0DWalkerRoomHandler: // ... 0x8c6314 x 1
        if (run) spawnOnce(stateAddress(TableId::TownStorySt0DLoneWalkerSpawns), 1);
        return true;
    case RoutineId::RuinEntranceRoomHandler:
        if (run) ruinEntranceRoom();
        return true;
    default: return false;
    }
}

bool townStoryScriptPorted(uint8_t id) {
    switch (storyScriptHandler(id).id()) {
    case RoutineId::JunkShopScript: case RoutineId::RollRunsOffScript: case RoutineId::JoesLabScript: case RoutineId::TrainLostScript: return true;
    default: return false;
    }
}

bool townStoryScriptTick(const PlayerEnv& env, uint8_t id) {
    switch (storyScriptHandler(id).id()) {
    case RoutineId::JunkShopScript: junkShopScript(); return true;
    case RoutineId::RollRunsOffScript: rollRunsOffScript(); return true;
    case RoutineId::JoesLabScript: joesLabScript(env); return true;
    case RoutineId::TrainLostScript: trainLostScript(); return true;
    default: return false;
    }
}

// script 0x71's white screen (exe 0x5f30e0 in its wrap up, exe 0x53d310)
bool trainDefenceWhiteScreen() {
    constexpr uint8_t kTrainDefenceScript = 0x71;
    const ScriptState& sc = scriptState();
    return sc.id == kTrainDefenceScript && sc.state == uint8_t(CutsceneState::WrapUp);
}

} // namespace od::port
