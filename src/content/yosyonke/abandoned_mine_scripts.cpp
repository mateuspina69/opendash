// the Abandoned Mine's story scripts 0x50..0x54 (see abandoned_mine_scripts.hpp), bit-exact against tests/data/story_ST0F_*.bin
// (test_story). Each script: state 0 its set-up (the camera script and the steps, the replay pad, the player taken
// over), state 1 a step runner, state 2 its wrap-up; the steps are dispatched by their named routines.
// Step ticks are the scripts' timelines (the case labels and the tick compares).
#include "content/yosyonke/abandoned_mine_scripts.hpp"
#include "content/yosyonke/abandoned_mine_scripts_state.hpp"

#include "engine/routine_ids.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actor_sweeps.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/display.hpp"
#include "game/event_state.hpp"
#include "game/fade.hpp"
#include "game/fade_state.hpp"
#include "game/fixed_point.hpp"
#include "game/game_state.hpp"
#include "game/message.hpp"
#include "game/message_state.hpp"
#include "game/pickups.hpp"
#include "game/player/player_input.hpp"
#include "game/player/player_motion.hpp"
#include "game/player/player_script_control.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/minimap.hpp"
#include "game/script_engine.hpp"
#include "game/script_state.hpp"
#include "game/spawn.hpp"
#include "game/stage_files.hpp"
#include "game/stage_state.hpp"
#include "game/story_flags.hpp"
#include "game/transition.hpp"
#include "game/transition_state.hpp"
#include "game/dat_names.hpp"

using namespace od::port::abandoned_mine_scripts_state;

namespace od::port {

namespace {

constexpr const char* kRuinScriptsPort = "ruin scripts port"; // the area of its port-gap errors

// the scripts' handlers

// the cast the wrap-ups spawn: the floating device 0x4f2c40, its glow 0x437fa0 and the relic 0x4dfc70
constexpr uint32_t kHallCastCount = 3;

// the talks' messages (window 0, the stage's message bank)
constexpr int8_t kTalkWindow = 0;
constexpr int16_t kRollFirstMessage = 0x96, kRollLastMessage = 0x9c;   // Roll at the lift
constexpr int16_t kHallFirstMessage = 0xa0;                            // the hall: 0xa0, then 0xa1..0xa5
constexpr int16_t kHallNextMessage = 0xa1;
constexpr int16_t kDeviceFirstMessage = 0xaa;                          // back in the hall: 0xaa
constexpr int16_t kDeviceTalkFirstMessage = 0xab, kDeviceTalkNextMessage = 0xac; // 0xab, then 0xac..0xaf

// the message flags the cast's talk animations key on (set by the messages' codes)
constexpr uint32_t kFlagHallCue = 0x680, kFlagHallTalk = 0x681, kFlagHallTalkDone = 0x682;
constexpr uint32_t kFlagDeviceCue = 0x683, kFlagDeviceTalk = 0x684, kFlagDeviceTalkDone = 0x685;
// the cast's animations they pick (+0xd of the script's actor 0)
constexpr uint8_t kHallTalkAnim = 1, kHallTalkEndAnim = 2, kDeviceTalkAnim = 3, kDeviceTalkEndAnim = 0;
// the device's spin (+0xe of the script's actor 1) the device talk starts
constexpr uint8_t kDeviceSpin = 0x40;

// the music (the four music channels) faded out before the guardian
constexpr int16_t kMusicFadeOutStep = 0xfff;
constexpr uint16_t kSoundGuardianWakes = 0x30;
// the guardian's DAT (sub 12's second model bank) and the hall's back after the fight
constexpr DatFile kGuardianDat{"ST0F01.DAT"}, kHallDat{"ST0F00.DAT"};

// script 0x50's sweep: every pool but the player's, except the group-2 animated actors / statics / effects and the
// projectiles (the lift and Roll stay)
constexpr uint32_t kLiftSweep = kSweepControllers | kSweepEffectsGroup1 | kSweepEffectsGroup0 | kSweepStaticsGroup1 |
                                kSweepStaticsGroup0 | kSweepShots | kSweepAnimatedGroup0;

constexpr uint32_t kVibrationStop = 0x1000000;
constexpr uint8_t kSkipFadeIn = kFadeInRequest | 2, kGuardianSkipFadeIn = kFadeInRequest | 1;
constexpr uint8_t kLiftFadeOut = kFadeOutRequest | 2;
// the head turn's limit (the player's lean target, either way)
constexpr int16_t kHeadTurnLimit = 0x300;
// the lift of script 0x50: its speed up to kLiftTopSpeed in steps of kLiftAcceleration
constexpr int16_t kLiftTopSpeed = 0x30, kLiftAcceleration = 4;
// the ride's request: ST0F sub 5 at (0x1c00, -1, 0) facing 0x400, fade in 2, no fade out
constexpr uint8_t kRuinStage = 0x0f, kRuinLowerRoom = 5;
constexpr uint8_t kRideFadeIn = 2;

RuinScriptBytes& scriptBytes() { return scriptState().part<RuinScriptBytes>(); }


// ---- small script helpers ----------------------------------------------------------------------------------------
void talk(int16_t message) { windowOpen(kTalkWindow, kMessageBank, message); } // exe 0x557390(0, bank, message)
bool talkClosed() { return windowStatus(kTalkWindow) == 0; }                    // exe 0x557810(0) == 0
// the next step (exe 0x5115c0(1))
void nextStep() { scriptSequenceSkip(1); }
// exe 0x5e3df0 on the player's yaw
int playerTurn(int16_t angle, int step) { return turnYawTo(player().rotation.y, uint32_t(int32_t(angle)), step); }
// the replay pad holds the player's "forward" button (a scripted walk forward) / is released
void replayWalkForward() { replayPad().held = player().button(PlayerButton::Forward); }
void replayRelease() { replayPad().held = 0; }
// the player's velocities and accelerations, its move directions and their angles cleared (the exe's short / byte /
// dword stores)
void clearPlayerMotion() {
    Player& p = player();
    p.velocity.z = 0;
    p.velocity.y = 0;
    p.velocity.x = 0;
    p.acceleration.z = 0;
    p.acceleration.y = 0;
    p.acceleration.x = 0;
    p.setPrevMoveDir(MoveDir::Forward);
    p.setMoveDir(MoveDir::Forward);
    p.clearDirectionOffsetWords();
}
// the player's move directions, their angles and the idle faces cleared (the set-ups of scripts 0x51 / 0x52)
void clearPlayerDirectionsAndFaces() {
    Player& p = player();
    p.setPrevMoveDir(MoveDir::Forward);
    p.setMoveDir(MoveDir::Forward);
    p.clearDirectionOffsetWords();
    p.idleFaces[0] = 0;
    p.idleFaces[1] = 0;
}
// the player at the 16.16 point facing yaw (keyframe data: the placements of the scripts)
void placePlayer(int16_t x, int16_t y, int16_t z, int16_t yaw) {
    Player& p = player();
    p.pos.x = wholeToFixed16(x);
    p.pos.y = wholeToFixed16(y);
    p.pos.z = wholeToFixed16(z);
    p.rotation.y = yaw;
}

// runner 0x531920's tail: the player's head (its lean target) turned toward the head target by the rate, within the
// limit either way
void headTurn(RuinScriptBytes& b) {
    Player& p = player();
    const uint16_t h = uint16_t(p.leanTarget);
    int d = int((uint32_t(uint16_t(p.rotation.y)) + h) & 0xfff) - int(uint16_t(b.headTarget) & 0xfff);
    if (uint32_t(d + kHalfTurn) > uint32_t(kFullTurn)) d = int((uint32_t(-d) ^ 0xfffu) + 1u);
    const int16_t step = b.rate;
    const int half = int(step) >> 1;
    if (d < -half) {
        p.leanTarget = int16_t(h + step);
        if (int16_t(h + step) > kHeadTurnLimit) p.leanTarget = kHeadTurnLimit;
    } else if (d > half) {
        p.leanTarget = int16_t(h - step);
        if (int16_t(h - step) < -kHeadTurnLimit) p.leanTarget = int16_t(-kHeadTurnLimit);
    }
}

void runStep(const RoutineRef& routine);

// exe 0x531920 (state 1 of scripts 0x50 / 0x51): the step's ids and tick copied into the script's bytes, the step's
// routine, the skip (with the wipe, while a frame of buffer 1 is drawn) -> state 2, the head turn, the sequence's tick
void stepsWithHeadTurn() {
    ScriptState& s = scriptState();
    RuinScriptBytes& b = scriptBytes();
    scriptState().part<CastTimelineStep>().stepId = s.stepId;
    scriptState().part<CastTimelineStep>().stepSub = s.stepSub;
    scriptState().part<CastTimelineTick>().stepTick = s.stepTick;
    const ScriptStep step = scriptStepAt(s.steps.value());
    bool wrapUp = step.id == kScriptStepEnd;
    if (!wrapUp) {
        const RoutineRef routine = stateRoutine(step.routine);
        if (routine) runStep(routine);
        if (scriptSkipRequested() != 0 && display().drawBuffer != 0) {
            scriptFade(kScriptSkipWipe, 0);
            wrapUp = true;
        }
    }
    if (wrapUp) s.enterState(CutsceneState::WrapUp);
    headTurn(b);
    scriptSequenceTick();
}

// ---- script 0x50: Roll at the lift of sub 1 (messages 0x96..0x9c), the ride down -------------------------------
// exe 0x531b00: the player hidden; once Roll stops, the player placed at the lift (0xf4b, -1, 0) facing 0xc00 and
// shown; the replay pad walks it 0x1a ticks, Roll's cue (+0xe) at 5
void liftArrivalStep() {
    ScriptState& s = scriptState();
    Player& p = player();
    switch (s.subState) {
    case 0:
        p.flags &= uint8_t(~kActorLiveDrawn);
        ++s.subState;
        [[fallthrough]];
    case 1: {
        AbandonedMineStageBlock& blk = abandonedMineStageBlock();
        if (blk.roll) {
            if (blk.roll->velocity.z != 0) return;
            p.pos.z = 0;
            p.airborne = 0;
            p.pos.x = wholeToFixed16(0xf4b);
            p.pos.y = wholeToFixed16(-1);
            p.rotation.y = 0xc00;
            p.flags |= kActorLiveDrawn;
        }
        ++s.subState;
        return;
    }
    case 2:
        liftArrival().walkStart = s.stepTick;
        replayWalkForward();
        ++s.subState;
        return;
    case 3:
        if (s.stepTick - liftArrival().walkStart != 5) return;
        if (abandonedMineStageBlock().roll) abandonedMineStageBlock().roll->params[2] = 1;
        ++s.subState;
        return;
    case 4:
        if (s.stepTick - liftArrival().walkStart != 0x1a) return;
        replayRelease();
        ++s.subState;
        return;
    case 5:
        if (playerToScriptIfFree() != 0) return;
        s.subState = 0;
        nextStep();
        return;
    default: return;
    }
}
// exe 0x531c30: at tick 1 the turn armed (to 0xab8, the head to 0x950 by 0x40); from tick 4 the player turns
void turnToRollStep() {
    ScriptState& s = scriptState();
    RuinScriptBytes& b = scriptBytes();
    switch (s.subState) {
    case 0: s.subState = 1; [[fallthrough]];
    case 1:
        if (s.stepTick != 1) return;
        b.turnTarget = 0xab8;
        b.headTarget = 0x950;
        b.rate = 0x40;
        ++s.subState;
        return;
    case 2:
        if (s.stepTick == 4) s.subState = 3;
        return;
    case 3:
        if (playerTurn(b.turnTarget, 0x40) == 0) {
            s.subState = 0;
            nextStep();
        }
        return;
    default: return;
    }
}
// messages first .. last in the talk window (the first at tick t0), then the next step
void messageChain(int32_t t0, int16_t first, int16_t last) {
    ScriptState& s = scriptState();
    const uint8_t st = s.subState;
    if (st == 0) {
        if (s.stepTick != t0) return;
        talk(first);
        s.subState = 1;
        return;
    }
    if (!talkClosed()) return;
    if (int(st) <= last - first) {
        talk(int16_t(first + st));
        s.subState = uint8_t(st + 1);
        return;
    }
    if (int(st) == last - first + 1) {
        s.subState = 0;
        nextStep();
    }
}
// exe 0x531cc0: Roll's talk at the lift
void rollTalkStep() { messageChain(10, kRollFirstMessage, kRollLastMessage); }
// exe 0x531e60: at tick 0x1e the turn armed (to 0xc00, the head too, by 0x40); from tick 0x21 the player turns, then
// it is back in play
void turnToLiftStep() {
    ScriptState& s = scriptState();
    RuinScriptBytes& b = scriptBytes();
    switch (s.subState) {
    case 0: s.subState = 1; [[fallthrough]];
    case 1:
        if (s.stepTick != 0x1e) return;
        b.rate = 0x40;
        b.turnTarget = 0xc00;
        b.headTarget = 0xc00;
        ++s.subState;
        return;
    case 2:
        if (s.stepTick == 0x21) s.subState = 3;
        return;
    case 3:
        if (playerTurn(b.turnTarget, 0x40) == 0) ++s.subState;
        return;
    case 4:
        playerBackToPlay();
        s.subState = 0;
        nextStep();
        return;
    default: return;
    }
}
// exe 0x531f00: the replay pad walks the player onto the lift (ticks 10..0x18); from tick 10 Roll turns toward it
void walkOntoLiftStep() {
    ScriptState& s = scriptState();
    switch (s.subState) {
    case 0: s.subState = 1; [[fallthrough]];
    case 1:
        if (s.stepTick == 10) {
            replayWalkForward();
            ++s.subState;
        }
        break;
    case 2:
        if (s.stepTick == 0x18) {
            replayRelease();
            ++s.subState;
        }
        break;
    case 3:
        if (playerToScriptIfFree() == 0) {
            s.subState = 0;
            nextStep();
        }
        break;
    default: break;
    }
    AbandonedMineStageBlock& blk = abandonedMineStageBlock();
    if (s.stepTick > 10 && blk.roll) {
        Actor& roll = *blk.roll;
        const int a = angleOf(roll.pos, player().pos);
        const int16_t v = int16_t(uint16_t(uint32_t(a) * 16u - 0x7ff1u));
        turnYawTo(roll.rotation.y, uint32_t(int32_t(int16_t(v >> 4))), 0x40);
    }
}
// exe 0x531fc0: from tick 10 the lift (the script's actor 1) and the player sink, the speed + 4 a tick up to 0x30; at
// tick 0x46 the fade out; the fade done -> the next step
void liftRideStep() {
    ScriptState& s = scriptState();
    RuinScriptBytes& b = scriptBytes();
    switch (s.subState) {
    case 0:
        b.rate = 0;
        b.lifting = 0;
        s.subState = 1;
        break;
    case 1:
        if (s.stepTick > 9) {
            b.lifting = 1;
            s.subState = 2;
        }
        break;
    case 2:
        if (s.stepTick > 0x45) {
            scriptFade(kLiftFadeOut, 0);
            ++s.subState;
        }
        break;
    case 3:
        if (fadeState().busy == 0) {
            s.subState = 0;
            b.lifting = 0;
            nextStep();
        }
        break;
    default: break;
    }
    if (b.lifting == 0) return;
    if (b.rate < kLiftTopSpeed) b.rate = int16_t(b.rate + kLiftAcceleration);
    integrateVelocity(actorLayoutOf(*s.actors[1]), 0, b.rate, 0);
    integrateVelocity(player(), 0, b.rate, 0);
}
// exe 0x531840: state 0 0x531860 (the actors swept, the camera script and the steps, the replay pad; the player script
// driven, its motion cleared), 1 = runner 0x531920, 2 0x531a10 (the vibration stopped; the fade over: the request ->
// ST0F sub 5; the player back once the request is taken)
void liftScript() {
    ScriptState& s = scriptState();
    switch (s.cutsceneState()) {
    case CutsceneState::SetUp: // exe 0x531860
        if (s.subState == 0) {
            actorsSweep(kLiftSweep, kSweepFree);
            scriptSequenceStart(stateAddress(TableId::AbandonedMineScriptsLiftCamera), scriptStepList<TableId::AbandonedMineScriptsLiftSteps>);
            scriptReplayPadOn();
            s.subState = 1;
            scriptSequenceTick();
        } else if (s.subState == 1 && playerToScriptIfFree() == 0) {
            playerScriptAnim(PlayerAnim::Idle, 0, true);
            playerBackToPlay();
            clearPlayerMotion();
            replayRelease();
            s.enterState(CutsceneState::Steps);
        }
        return;
    case CutsceneState::Steps: stepsWithHeadTurn(); return;
    case CutsceneState::WrapUp: // exe 0x531a10
        switch (s.subState) {
        case 0:
            padVibrate(0, kVibrationStop);
            ++s.subState;
            [[fallthrough]];
        case 1:
            if (fadeState().busy == 0) ++s.subState;
            return;
        case 2:
            windowCloseAll(WindowClose::Force);
            requestTransition(kTransitionAccepted, kRuinStage, kRuinLowerRoom, 0x1c00, -1, 0, 0x400, kRideFadeIn,
                              kNoFade);
            ++s.subState;
            [[fallthrough]];
        case 3:
            if (transitionRequest().state == kTransitionNone) {
                player().flags |= kActorLiveDrawn;
                playerBackToPlay();
                scriptReplayPadOff();
                scriptEndBlock();
            }
            return;
        default: return;
        }
    default: notPortedAt(kRuinScriptsPort, "script 0x50 state", s.state);
    }
}

// ---- script 0x51: the hall of sub 11 (messages 0xa0..0xa5) --------------------------------------------------------
// exe 0x532210: the replay pad walks the player in (ticks 0x14..0x37)
void hallWalkInStep() {
    const int32_t t = scriptState().stepTick;
    if (t == 0x14) replayWalkForward();
    else if (t == 0x37) replayRelease();
}
// exe 0x532240: the player script driven; at tick 0x14 the shuffle and the turn armed (to 0x780, the head too, by
// 0x80); from tick 0x17 it turns, then back in play
void hallLookStep() {
    ScriptState& s = scriptState();
    RuinScriptBytes& b = scriptBytes();
    switch (s.subState) {
    case 0:
        if (playerToScriptIfFree() == 0) {
            playerScriptAnim(PlayerAnim::Idle, 0, true);
            ++s.subState;
        }
        return;
    case 1:
        if (s.stepTick != 0x14) return;
        playerScriptAnim(PlayerAnim::Shuffle, 0, true);
        b.turnTarget = 0x780;
        b.headTarget = 0x780;
        b.rate = 0x80;
        ++s.subState;
        return;
    case 2:
        if (s.stepTick == 0x17) ++s.subState;
        return;
    case 3:
        if (playerTurn(b.turnTarget, 0x80) == 0) {
            playerBackToPlay();
            playerScriptAnim(PlayerAnim::Idle, 0, true);
            s.subState = 0;
            nextStep();
        }
        return;
    default: return;
    }
}
// exe 0x532310: the replay pad from tick 0x3c, the next step at 0x50
void hallWalkOnStep() {
    const int32_t t = scriptState().stepTick;
    if (t == 0x3c) replayWalkForward();
    else if (t == 0x50) nextStep();
}
// the cast's talk animation by the message flags (the tails of 0x532340 / 0x532d30): the cue flag armed -> the talk
// animation (+0xd of the script's actor 0), the talk flag; then at the end of that animation the end animation, the
// done flag
void castTalkCues(uint32_t cue, uint32_t talking, uint32_t done, uint8_t talkAnim, uint8_t endAnim) {
    AnimatedActor& cast = asAnimated(*scriptState().actors[0]);
    if (!storyFlag(talking)) {
        if (storyFlag(cue)) {
            cast.params[1] = talkAnim;
            setStoryFlag(talking);
        }
    } else if (int8_t(cast.animKey.flags) == -1) {
        cast.params[1] = endAnim;
        setStoryFlag(done);
    }
}
// exe 0x532340: two ticks of a walk, the replay pad released at 0x15, the messages 0xa0..0xa5 from tick 0x32
void hallTalkStep() {
    ScriptState& s = scriptState();
    switch (s.subState) {
    case 0:
        if (s.stepTick < 2) moveInFacing(player(), 0, 0, -0x200);
        else ++s.subState;
        break;
    case 1:
        if (s.stepTick == 0x15) {
            replayRelease();
            ++s.subState;
        }
        break;
    case 2:
        if (s.stepTick == 0x32) {
            talk(kHallFirstMessage);
            ++s.subState;
        }
        break;
    case 3: case 4: case 5: case 6: case 7:
        if (talkClosed()) {
            talk(int16_t(kHallNextMessage + s.subState - 3));
            ++s.subState;
        }
        break;
    case 8:
        if (talkClosed()) {
            s.subState = 0;
            nextStep();
        }
        break;
    default: break;
    }
    castTalkCues(kFlagHallCue, kFlagHallTalk, kFlagHallTalkDone, kHallTalkAnim, kHallTalkEndAnim);
}
// exe 0x532090: state 0 0x5320b0 (the camera script and the steps, the replay pad; the player placed at (-0x540, -1, 0)
// facing 0xc30), 1 = runner 0x531920, 2 0x532160 (the vibration stopped, fade if skipped, the actors swept, the cast
// records x 3, the player at (-0x210, -1, 0x4f5) facing 0x780)
void hallScript() {
    ScriptState& s = scriptState();
    switch (s.cutsceneState()) {
    case CutsceneState::SetUp: // exe 0x5320b0
        if (s.subState == 0) {
            scriptSequenceStart(stateAddress(TableId::AbandonedMineScriptsHallCamera), scriptStepList<TableId::AbandonedMineScriptsHallSteps>);
            scriptReplayPadOn();
            s.subState = 1;
        } else if (s.subState != 1) {
            scriptSequenceTick();
            return;
        }
        if (playerToScriptIfFree() == 0) {
            playerBackToPlay();
            placePlayer(-0x540, -1, 0, 0xc30);
            clearPlayerDirectionsAndFaces();
            replayRelease();
            s.enterState(CutsceneState::Steps);
        }
        scriptSequenceTick();
        return;
    case CutsceneState::Steps: stepsWithHeadTurn(); return;
    case CutsceneState::WrapUp: { // exe 0x532160
        uint8_t st = s.subState;
        if (st == 0) {
            padVibrate(0, kVibrationStop);
            s.subState = ++st;
        }
        if (st == 1) {
            if (s.flags & kScriptSkipTaken) {
                if (fadeState().busy != 0) return;
                scriptFade(kSkipFadeIn, 0);
            }
            ++s.subState;
        } else if (st != 2) {
            return;
        }
        actorsSweep(kSweepTransition, kSweepFree);
        windowCloseAll(WindowClose::Force);
        spawnRecords(stateAddress(TableId::AbandonedMineScriptsHallCastRecords), kHallCastCount);
        placePlayer(-0x210, -1, 0x4f5, 0x780);
        playerBackToPlay();
        scriptReplayPadOff();
        scriptEndBlock();
        return;
    }
    default: notPortedAt(kRuinScriptsPort, "script 0x51 state", s.state);
    }
}

// ---- scripts 0x52 / 0x53: the guardian of sub 12 ---------------------------------------------------------------
// exe 0x532560: state 0 0x532580 (the actors swept, the music faded out, the camera script and the steps (the guardian
// is their cast), unskippable; the player at (-0xb00, -1, 0x40) facing 0xc00, the guardian's DAT requested), 1 =
// runner 0x5328b0, 2 0x532660 (fade if skipped; the guardian kept in the stage block, the player at (-0x4e0, -1,
// 0x40))
void entranceScript() {
    ScriptState& s = scriptState();
    switch (s.cutsceneState()) {
    case CutsceneState::SetUp: // exe 0x532580
        if (s.subState == 0) {
            actorsSweep(kSweepTransition, kSweepFree);
            soundFadeOut(kMusicFades, kMusicFadeOutStep, 0);
            scriptSequenceStart(stateAddress(TableId::AbandonedMineScriptsEntranceCamera), scriptStepList<TableId::AbandonedMineScriptsEntranceSteps>);
            scriptSequenceTick();
            scriptReplayPadOn();
            s.flags |= kScriptUnskippable;
            ++s.subState;
        } else if (s.subState != 1) {
            return;
        }
        if (playerToScriptIfFree() == 0) {
            playerBackToPlay();
            placePlayer(-0xb00, -1, 0x40, 0xc00);
            clearPlayerDirectionsAndFaces();
            datLoadRequest(kGuardianDat);
            replayRelease();
            s.state = uint8_t(CutsceneState::Steps);
            s.subState = 0;
            s.stepPhase = 0;
            s.stepSubPhase = 0;
        }
        return;
    case CutsceneState::Steps: scriptStepsStateSkipNoWipe(&runStep); return;
    case CutsceneState::WrapUp: { // exe 0x532660
        const uint8_t st = s.subState;
        if (st == 0) {
            s.subState = 1;
        } else if (st != 1) {
            if (st != 2) return;
            abandonedMineStageBlock().guardian = &actorLayoutOf(*s.actors[0]);
            playerBackToPlay();
            scriptReplayPadOff();
            Player& p = player();
            p.pos.x = wholeToFixed16(-0x4e0);
            minimap().flags &= uint8_t(~kMinimapAllowed);
            p.pos.y = wholeToFixed16(-1);
            p.pos.z = wholeToFixed16(0x40);
            p.rotation.y = 0xc00;
            p.airborne = 0;
            scriptEndBlock();
            return;
        }
        if (fadeState().busy == 0) {
            if (s.flags & kScriptSkipTaken) scriptFade(kGuardianSkipFadeIn, 0);
            ++s.subState;
        }
        return;
    }
    default: notPortedAt(kRuinScriptsPort, "script 0x52 state", s.state);
    }
}
// exe 0x532700: the replay pad (ticks 1..0x32); from 0x50, once the music has faded, no file loads and a frame of buffer
// 1 is drawn: the other type-1 bank buffer walked (exe 0x5f3200), the guardian's sound, the next step (the fight's
// camera)
void guardianWakeStep() {
    ScriptState& s = scriptState();
    RuinScriptBytes& b = scriptBytes();
    const int32_t t = s.stepTick;
    if (t == 1) {
        b.musicWait = 0;
        replayWalkForward();
    } else if (t == 0x32) {
        replayRelease();
    } else if (t == 0x50) {
        b.musicWait = 1;
    }
    if (b.musicWait == 0) return;
    if (soundDone(kMusicFades) && !(fileLoads().loading & kFileLoading) && display().drawBuffer == 1) {
        stage().bankBuffer ^= 1u;
        stageBankWalk();
        soundPlayFlatForced(kSoundGuardianWakes);
        nextStep();
    }
}
// exe 0x532790: state 0 0x5327b0 (the music faded out, a running event ended, the guardian the script's actor 0, the
// camera script and the steps, the script solids and the pickups kept, the skip locked; the player script driven
// standing; once the music has faded the hall's DAT), 1 = runner 0x5328b0, 2 0x5328f0 (the bank buffer back once
// the file loaded, fade if skipped, the flags cleared, the minimap allowed again)
void endScript() {
    ScriptState& s = scriptState();
    switch (s.cutsceneState()) {
    case CutsceneState::SetUp: { // exe 0x5327b0
        const uint8_t st = s.subState;
        if (st == 0) {
            soundFadeOut(kMusicFades, kMusicFadeOutStep, 0);
            EventState& ev = eventState();
            if (ev.active != 0) {
                windowCloseAll(WindowClose::Force);
                player().inputMode = 0;
                player().immunity = 0;
                ev.active = 0;
            }
            s.actors[0] = abandonedMineStageBlock().guardian.get();
            scriptSequenceStart(stateAddress(TableId::AbandonedMineScriptsEndCamera), scriptStepList<TableId::AbandonedMineScriptsEndSteps>);
            scriptReplayPadOn();
            setStoryFlag(kFlagScriptSolids);
            setStoryFlag(kFlagPickupsStay);
            s.flags |= kScriptSkipLocked;
            ++s.subState;
        } else if (st == 2) {
            if (!soundDone(kMusicFades)) return;
            datLoadRequest(kHallDat);
            Player& p = player();
            p.idleFaces[0] = 0;
            p.idleFaces[1] = 0;
            p.velocity.clearWords();
            p.acceleration.clearWords();
            s.enterState(CutsceneState::Steps);
            return;
        } else if (st != 1) {
            return;
        }
        if (playerToScriptWhenStanding() == 0) {
            playerScriptAnim(PlayerAnim::Idle, 0, true);
            ++s.subState;
        }
        return;
    }
    case CutsceneState::Steps: scriptStepsStateSkipNoWipe(&runStep); return;
    case CutsceneState::WrapUp: { // exe 0x5328f0
        const uint8_t st = s.subState;
        if (st == 0) {
            if (fileLoads().loading & kFileLoading) return;
            if (display().drawBuffer != 1) return;
            stage().bankBuffer ^= 1u;
            stageBankWalk();
        } else if (st == 1) {
            if (fadeState().busy != 0) return;
            if (s.flags & kScriptSkipTaken) scriptFade(kGuardianSkipFadeIn, 0);
        } else if (st == 2) {
            playerBackToPlay();
            scriptReplayPadOff();
            clearStoryFlag(kFlagScriptSolids);
            clearStoryFlag(kFlagPickupsStay);
            minimap().flags |= kMinimapAllowed;
            scriptEndBlock();
            return;
        } else {
            return;
        }
        ++s.subState;
        return;
    }
    default: notPortedAt(kRuinScriptsPort, "script 0x53 state", s.state);
    }
}
// exe 0x532990: the next step (its phase bytes cleared) once the guardian's slot is free
void guardianGoneStep() {
    ScriptState& s = scriptState();
    AbandonedMineStageBlock& blk = abandonedMineStageBlock();
    if (s.stepPhase == 0 && blk.guardian && blk.guardian->flags == 0) scriptNextStep();
}

// ---- script 0x54: back in sub 11 after the guardian (messages 0xaa..0xaf) ---------------------------------------
// exe 0x532b70: the replay pad (ticks 0..0x11); from tick 0x1b, once the player stands, the next step
void backInHallWalkStep() {
    ScriptState& s = scriptState();
    const int32_t t = s.stepTick;
    if (t == 0) replayWalkForward();
    else if (t == 0x11) replayRelease();
    else if (t == 0x1b) s.subState = 1;
    if (s.subState != 0 && playerToScriptIfFree() == 0) {
        s.subState = 0;
        nextStep();
    }
}
// exe 0x532bd0: at tick 10 the shuffle and the turn armed (to 0x600, the head too, by 0x60); from tick 0xd it turns
void backInHallTurnStep() {
    ScriptState& s = scriptState();
    RuinScriptBytes& b = scriptBytes();
    switch (s.subState) {
    case 0: s.subState = 1; [[fallthrough]];
    case 1:
        if (s.stepTick != 10) return;
        playerScriptAnim(PlayerAnim::Shuffle, 0, true);
        b.turnTarget = 0x600;
        b.headTarget = 0x600;
        b.rate = 0x60;
        ++s.subState;
        return;
    case 2:
        if (s.stepTick == 0xd) s.subState = 3;
        return;
    case 3:
        if (playerTurn(b.turnTarget, 0x60) == 0) {
            playerScriptAnim(PlayerAnim::Idle, 0, true);
            s.subState = 0;
            nextStep();
        }
        return;
    default: return;
    }
}
// exe 0x532c70: message 0xaa at tick 0; at 0x28 the player back and the replay pad; at 0x4f the next step, the window
// closed
void deviceMessageStep() {
    const int32_t t = scriptState().stepTick;
    if (t == 0) {
        talk(kDeviceFirstMessage);
    } else if (t == 0x28) {
        playerBackToPlay();
        replayWalkForward();
    } else if (t == 0x4f) {
        nextStep();
        windowClose(kTalkWindow, WindowClose::Gentle);
    }
}
// exe 0x532cd0: the player at (-0xb8, -1, 0x1eb) facing 0x6d5 and the replay pad (ticks 0..0x1b); the next step at
// 0x25
void deviceApproachStep() {
    const int32_t t = scriptState().stepTick;
    if (t == 0) {
        placePlayer(-0xb8, -1, 0x1eb, 0x6d5);
        replayWalkForward();
    } else if (t == 0x1b) {
        replayRelease();
    } else if (t == 0x25) {
        nextStep();
    }
}
// exe 0x532d30: the messages 0xab..0xaf from tick 10 (the device spins with 0xad: the script's actor 1)
void deviceTalkStep() {
    ScriptState& s = scriptState();
    const uint8_t st = s.subState;
    switch (st) {
    case 0:
        if (s.stepTick == 10) {
            talk(kDeviceTalkFirstMessage);
            ++s.subState;
        }
        break;
    case 1: case 2: case 3: case 4:
        if (talkClosed()) {
            talk(int16_t(kDeviceTalkNextMessage + st - 1));
            ++s.subState;
            if (st == 2) (*s.actors[1]).params[2] = kDeviceSpin;
        }
        break;
    case 5:
        if (talkClosed()) {
            s.subState = 0;
            nextStep();
        }
        break;
    default: break;
    }
    castTalkCues(kFlagDeviceCue, kFlagDeviceTalk, kFlagDeviceTalkDone, kDeviceTalkAnim, kDeviceTalkEndAnim);
}
// exe 0x5329c0: state 0 0x5329e0 (the camera script and the steps, the replay pad; the player script driven at
// (0x500, -1, 0) facing 0x400, its motion cleared), 1 = runner 0x52f420, 2 0x532ab0 (the vibration stopped, fade if
// skipped, the actors swept, the cast records x 3 (the device now leads out), the player at (-0x236, -1, 0x4f2) facing
// 0x6d5, the doors locked, the next story's progress + 1 (exe 0x512800))
void deviceScript() {
    ScriptState& s = scriptState();
    switch (s.cutsceneState()) {
    case CutsceneState::SetUp: // exe 0x5329e0
        if (s.subState == 0) {
            scriptSequenceStart(stateAddress(TableId::AbandonedMineScriptsDeviceCamera), scriptStepList<TableId::AbandonedMineScriptsDeviceSteps>);
            scriptReplayPadOn();
            s.subState = 1;
        } else if (s.subState != 1) {
            return;
        }
        if (playerToScriptIfFree() == 0) {
            playerScriptAnim(PlayerAnim::Idle, 0, true);
            playerBackToPlay();
            clearPlayerMotion();
            placePlayer(0x500, -1, 0, 0x400);
            replayRelease();
            s.enterState(CutsceneState::Steps);
        }
        return;
    case CutsceneState::Steps: scriptStepsStateNoFaces(&runStep); return;
    case CutsceneState::WrapUp: { // exe 0x532ab0
        uint8_t st = s.subState;
        if (st == 0) {
            padVibrate(0, kVibrationStop);
            s.subState = ++st;
        }
        if (st == 1) {
            if (s.flags & kScriptSkipTaken) {
                if (fadeState().busy != 0) return;
                scriptFade(kSkipFadeIn, 0);
            }
            ++s.subState;
        } else if (st != 2) {
            return;
        }
        actorsSweep(kSweepTransition, kSweepFree);
        windowCloseAll(WindowClose::Force);
        spawnRecords(stateAddress(TableId::AbandonedMineScriptsDeviceCastRecords), kHallCastCount);
        placePlayer(-0x236, -1, 0x4f2, 0x6d5);
        playerBackToPlay();
        scriptReplayPadOff();
        setStoryFlag(kFlagLocked);
        ++gameState().nextStory; // exe 0x512800
        scriptEndBlock();
        return;
    }
    default: notPortedAt(kRuinScriptsPort, "script 0x54 state", s.state);
    }
}

// the steps' routines (by the named routine)
void runStep(const RoutineRef& routine) {
    switch (routine.id()) {
    case RoutineId::EmptyRoutine: return; // ret
    case RoutineId::AbandonedMineLiftArrivalStep: liftArrivalStep(); return;
    case RoutineId::AbandonedMineTurnToRollStep: turnToRollStep(); return;
    case RoutineId::AbandonedMineRollTalkStep: rollTalkStep(); return;
    case RoutineId::AbandonedMineTurnToLiftStep: turnToLiftStep(); return;
    case RoutineId::AbandonedMineWalkOntoLiftStep: walkOntoLiftStep(); return;
    case RoutineId::AbandonedMineLiftRideStep: liftRideStep(); return;
    case RoutineId::AbandonedMineHallWalkInStep: hallWalkInStep(); return;
    case RoutineId::AbandonedMineHallLookStep: hallLookStep(); return;
    case RoutineId::AbandonedMineHallWalkOnStep: hallWalkOnStep(); return;
    case RoutineId::AbandonedMineHallTalkStep: hallTalkStep(); return;
    case RoutineId::AbandonedMineGuardianWakeStep: guardianWakeStep(); return;
    case RoutineId::AbandonedMineGuardianGoneStep: guardianGoneStep(); return;
    case RoutineId::AbandonedMineBackInHallWalkStep: backInHallWalkStep(); return;
    case RoutineId::AbandonedMineBackInHallTurnStep: backInHallTurnStep(); return;
    case RoutineId::AbandonedMineDeviceMessageStep: deviceMessageStep(); return;
    case RoutineId::AbandonedMineDeviceApproachStep: deviceApproachStep(); return;
    case RoutineId::AbandonedMineDeviceTalkStep: deviceTalkStep(); return;
    default: notPortedAt(kRuinScriptsPort, "ruin script step", routine.pointerValue());
    }
}

} // namespace

bool abandonedMineScriptPorted(RoutineId handler) {
    switch (handler) {
    case RoutineId::AbandonedMineLiftScript:
    case RoutineId::AbandonedMineHallScript:
    case RoutineId::AbandonedMineEntranceScript:
    case RoutineId::AbandonedMineEndScript:
    case RoutineId::AbandonedMineDeviceScript: return true;
    default: return false;
    }
}

bool abandonedMineScriptTick(const PlayerEnv&, RoutineId handler) {
    switch (handler) {
    case RoutineId::AbandonedMineLiftScript: liftScript(); return true;
    case RoutineId::AbandonedMineHallScript: hallScript(); return true;
    case RoutineId::AbandonedMineEntranceScript: entranceScript(); return true;
    case RoutineId::AbandonedMineEndScript: endScript(); return true;
    case RoutineId::AbandonedMineDeviceScript: deviceScript(); return true;
    default: return false;
    }
}

} // namespace od::port
