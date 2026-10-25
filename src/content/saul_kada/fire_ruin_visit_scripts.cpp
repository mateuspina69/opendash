// The story-13 visit scripts of the fire ruin and the ruin (hypothesis names): script 0x2e (exe 0x526fa0, steps
// 0x8a5aa8; ST27 sub 8 before the escape: the music fades, Megaman at (-0x400, 0, 0x500), the voice, message 0x19; with
// the escape (flag 0x681) the passage (0x3d, 0x40) opens), 0x63 (exe 0x538390, steps 0x8ac6a0; ST26 sub 0 past z
// -0x700: the camera over the cast of records 0x8ac5d8) and 0x2d (exe 0x526b40, steps 0x8a58f0; ST33 sub 0, the ruin's
// first visit: Megaman turns, is driven by the replay pad, its cast reacts). Each handler dispatches on the script's
// state: the set-up, the steps runner, the wrap-up.
#include "content/saul_kada/fire_ruin_visit_scripts.hpp"
#include "content/saul_kada/fire_ruin_visit_scripts_state.hpp"

#include "engine/routine_ids.hpp"
#include "game/camera_state.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actor_sweeps.hpp"
#include "game/audio_logic.hpp"
#include "game/breakable_walls.hpp"
#include "game/camera.hpp"
#include "game/display.hpp"
#include "game/event.hpp"
#include "game/event_state.hpp"
#include "game/fade_state.hpp"
#include "game/message.hpp"
#include "game/player/player_input.hpp"
#include "game/player/player_script_control.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/script_engine.hpp"
#include "game/script_state.hpp"
#include "game/spawn.hpp"
#include "game/stage_files.hpp"
#include "game/story.hpp"
#include "game/story_flags.hpp"
#include "game/transition.hpp"

using namespace od::port::fire_ruin_visit_scripts_state;

namespace od::port {

namespace {

constexpr RoutineId kStepIdle = RoutineId::EmptyRoutine;
constexpr uint32_t kMusicFades = 0xf;
constexpr int16_t kMusicFadeStep = 0xfff;
constexpr uint32_t kVoiceFade = 8;
constexpr uint8_t kSkippedFade = 1;
constexpr uint32_t kFlagEscape = 0x681;
VisitScriptFields& fields() {
    return scriptState().part<VisitScriptFields>();
}

int32_t tick() { return scriptState().stepTick; }
void playerAnim(uint8_t anim) { playerScriptAnimId(anim, 0, 1); } // exe 0x5723d0(anim, 0, 1)
// the player placed for a cutscene (his move angles and idle faces cleared, his position and yaw)
void placePlayer(int32_t x, int32_t y, int32_t z, int16_t yaw) {
    Player& p = player();
    p.clearDirectionOffsetWords();
    p.idleFaces[0] = 0;
    p.idleFaces[1] = 0;
    p.pos.x = x;
    p.pos.y = y;
    p.pos.z = z;
    p.rotation.y = yaw;
}
bool fileLoading() { return (fileLoads().loading & kFileLoading) != 0; }
bool streamBusy() { return (fileLoads().loading & kStreamBusy) != 0; }

// ---- script 0x2e ---------------------------------------------------------------------------------------------------
constexpr RoutineId kEveWait = RoutineId::FireRuinVisitScriptsEveWait,
                    kEveTalk = RoutineId::FireRuinVisitScriptsEveTalk;
enum class EveTalk : uint8_t { WaitFiles = 0, Voice = 1, WaitVoice = 2, Count = 3, Message = 4 };
// its steps (the script's sub-state is their phase)
void eveStep(const RoutineRef& routine) {
    ScriptState& s = scriptState();
    VisitScriptFields& f = fields();
    switch (routine.id()) {
    case kEveWait:
        if (s.subState == 0) {
            if (tick() == 0x3c) s.subState = 1;
        } else if (s.subState == 1 && soundDone(kMusicFades)) {
            scriptSequenceSkip(1); // exe 0x5115c0
            s.subState = 0;
        }
        return;
    case kEveTalk:
        switch (EveTalk(s.subState)) {
        case EveTalk::WaitFiles:
            if (!fileLoading()) s.subState = uint8_t(EveTalk::Voice);
            return;
        case EveTalk::Voice:
            if (!streamBusy()) {
                voiceClipPlay(4);
                s.subState = uint8_t(EveTalk::WaitVoice);
            }
            return;
        case EveTalk::WaitVoice:
            if (voiceClipPlaying()) {
                f.talkTicks = 0;
                s.subState = uint8_t(EveTalk::Count);
            }
            return;
        case EveTalk::Count:
            f.talkTicks += 1u;
            if (f.talkTicks > 0x14a) {
                eventStartAt(0, nullptr, 1, 0, 0x19); // exe 0x543b40
                s.subState = uint8_t(EveTalk::Message);
            }
            return;
        case EveTalk::Message:
            if (eventState().active != 0) return;
            if (storyFlag(kFlagEscape)) {
                switchGridCellVariant(0x3d, 0x40, 1); // exe 0x58d5f0
                setStoryFlag(0x739);
            }
            scriptSequenceSkip(1);
            s.subState = 0;
            return;
        default: return;
        }
    default: notPortedAt(kStoryPort, "script 0x2e step", routine.pointerValue());
    }
}
void escapeEveScript() { // exe 0x526fa0
    ScriptState& s = scriptState();
    switch (s.cutsceneState()) {
    case CutsceneState::SetUp: // exe 0x526fc0
        switch (s.subState) {
        case 0:
            scriptReplayPadOn();
            soundFadeOut(kMusicFades, kMusicFadeStep, 0);
            s.subState = 1;
            [[fallthrough]];
        case 1:
            if (playerToScriptIfFree() != 0) return;
            actorsSweep(kSweepActors, kSweepFree);
            placePlayer(int32_t(0xfc000000), 0, 0x5000000, 0);
            playerAnim(0x50);
            spawnRecords(stateAddress(TableId::FireRuinVisitScriptsEscapeEveSpawns), 4);
            scriptSequenceStart(stateAddress(TableId::FireRuinVisitScriptsEscapeEveCamera), stateAddress(TableId::FireRuinVisitScriptsEscapeEveSteps));
            scriptSequenceTick();
            s.enterStateKeepingPhases(uint8_t(CutsceneState::Steps));
            return;
        default: return;
        }
    case CutsceneState::Steps: { // exe 0x527080: the step, the list's end -> the wrap-up (no skip)
        const ScriptStep step = scriptStepAt(s.steps.value());
        if (step.id != kScriptStepEnd) eveStep(stateRoutine(step.routine));
        else s.enterState(CutsceneState::WrapUp);
        scriptSequenceTick();
        return;
    }
    case CutsceneState::WrapUp: // exe 0x5270b0
        switch (s.subState) {
        case 0:
            if (voiceClipPlaying()) voiceClipFadeOut(kVoiceFade);
            scriptFade(0x11, 0);
            s.subState = 1;
            return;
        case 1:
            if (voiceClipIdle() && fadeState().busy == 0) {
                actorsSweep(kSweepActors, kSweepFree);
                s.subState = 2;
            }
            return;
        case 2: {
            spawnRecords(stateAddress(TableId::FireRuinVisitScriptsEscapeEveSpawns), 4);
            if (storyFlag(kFlagEscape)) spawnRecords(stateAddress(TableId::FireRuinVisitScriptsEscapeEveEscapedSpawns), 1);
            else spawnRecords(stateAddress(TableId::FireRuinVisitScriptsEscapeEveStayedSpawns), 3);
            playerBackToPlay(); // exe 0x5723c0
            scriptReplayPadOff();
            Player& p = player();
            p.airborne = 0;
            p.pos.x = int32_t(0xfc000000);
            p.pos.y = 0;
            if (storyFlag(kFlagEscape)) {
                p.pos.z = 0x5000000;
                soundPlayFlatForced(0x3d);
            } else {
                p.pos.z = 0xa000000;
                soundPlayFlatForced(0x3c);
            }
            p.rotation.y = 0;
            scriptFade(kSkippedFade, 0);
            cameraState().lookPitch = 0; // the script ends clear the look pitch
            scriptEndBlock();
            return;
        }
        default: return;
        }
    default: notPortedAt(kStoryPort, "script 0x2e state", s.state);
    }
}

// ---- script 0x63 ---------------------------------------------------------------------------------------------------
constexpr uint32_t kHallSweep = 0x494; // the classes its end sweeps
void hallScript() { // exe 0x538390
    ScriptState& s = scriptState();
    switch (s.cutsceneState()) {
    case CutsceneState::SetUp: // exe 0x5383b0
        switch (s.subState) {
        case 0:
            scriptReplayPadOn();
            windowCloseAll(WindowClose::Force); // exe 0x5577d0
            s.subState = 1;
            return;
        case 1:
            if (voiceClipIdle()) s.subState = 2;
            return;
        case 2:
            if (playerToScriptIfFree() != 0) return;
            actorsSweep(kSweepActors, kSweepFree);
            placePlayer(0, 0, int32_t(0xf9000000), 0x800);
            spawnRecords(stateAddress(TableId::FireRuinVisitScriptsHallSpawns), 5);
            scriptSequenceStart(stateAddress(TableId::FireRuinVisitScriptsHallCamera), stateAddress(TableId::FireRuinVisitScriptsHallSteps));
            scriptSequenceTick();
            s.enterStateKeepingPhases(uint8_t(CutsceneState::Steps));
            return;
        default: return;
        }
    case CutsceneState::Steps: { // the runner exe 0x52eb60 (skip: fade 0x11); its only step does nothing
        const ScriptStep step = scriptStepAt(s.steps.value());
        if (step.id == kScriptStepEnd) {
            s.enterState(CutsceneState::WrapUp);
        } else if (scriptSkipRequested() && display().drawBuffer != 0) {
            scriptFade(0x11, 0);
            s.enterState(CutsceneState::WrapUp);
        }
        scriptSequenceTick();
        return;
    }
    case CutsceneState::WrapUp: // exe 0x538480
        switch (s.subState) {
        case 0: s.subState = 1; return;
        case 1:
            if (fadeState().busy == 0) s.subState = 2;
            return;
        case 2: {
            if (s.flags & kScriptSkipTaken) scriptFade(kSkippedFade, 0);
            actorsSweep(kHallSweep, kSweepFree);
            spawnRecords(stateAddress(TableId::FireRuinVisitScriptsHallEndSpawns), 1);
            playerBackToPlay();
            scriptReplayPadOff();
            Player& p = player();
            p.airborne = 0;
            p.pos.x = 0;
            p.pos.y = 0;
            p.pos.z = int32_t(0xf9000000);
            p.rotation.y = 0x800;
            cameraState().lookPitch = 0; // the script ends clear the look pitch
            scriptEndBlock();
            return;
        }
        default: return;
        }
    default: notPortedAt(kStoryPort, "script 0x63 state", s.state);
    }
}

// ---- script 0x2d ---------------------------------------------------------------------------------------------------
constexpr RoutineId kVisitPadLook = RoutineId::FireRuinVisitScriptsVisitPadLook,
                    kVisitPlace = RoutineId::FireRuinVisitScriptsVisitPlace,
                    kVisitCastCue = RoutineId::ExitLeafFollowsHandStep,
                    kVisitDrive = RoutineId::FireRuinVisitScriptsVisitDrive;
constexpr uint32_t kVisitSweep = 0x7fe;
constexpr uint16_t kRuinMusic = 0xf;
void ruinVisitStep(const RoutineRef& routine) {
    ScriptState& s = scriptState();
    const int32_t t = tick();
    const auto cast = [&s]() -> ActorHeader& { return (*s.actors[0]); };
    Player& p = player();
    ReplayPad& pad = replayPad();
    switch (routine.id()) {
    case kVisitPadLook:
        if (t == 1) pad.held = p.buttonMap[0];
        else if (t == 0x42) pad.held = 0;
        return;
    case kVisitPlace:
        if (t == 0) {
            p.pos.x = int32_t(0xfb000000);
            soundPlayFlatForced(kRuinMusic);
            s.stepPhase = 1;
        }
        return;
    case kVisitCastCue:
        if (t == 0) cast().subState = 1;
        return;
    case kVisitDrive:
        if (t == 0xa) {
            const uint16_t buttons = uint16_t(p.buttonMap[2] | p.buttonMap[4] | p.buttonMap[8]);
            p.rotation.y = 0x900;
            pad.held = buttons;
            pad.pressed = buttons;
        } else if (t == 0xc) {
            pad.held = 0;
            pad.pressed = 0;
        } else if (t == 0x4d) {
            cast().step = 2;
        } else if (t == 0x9b) {
            cast().step = 4;
        }
        if (t > 0x1d && t < 0x22) p.rotation.y = int16_t((uint16_t(p.rotation.y) + 0x100u) & 0xfffu);
        if (t > 0x26 && s.stepSubPhase == 0) {
            if (playerToScriptIfFree() == 0) s.stepSubPhase = 1;
        } else if (s.stepSubPhase == 1) {
            playerAnim(4);
            s.stepSubPhase = 2;
        } else if (s.stepSubPhase != 0 && p.body.key.frame == 1) {
            p.animPaused = 1;
        }
        if (cast().params[0] != 0) scriptSequenceSkip(1);
        return;
    default: notPortedAt(kStoryPort, "script 0x2d step", routine.pointerValue());
    }
}
void ruinVisitScript() { // exe 0x526b40
    ScriptState& s = scriptState();
    switch (s.cutsceneState()) {
    case CutsceneState::SetUp: // exe 0x526b60
        switch (s.subState) {
        case 0:
            scriptReplayPadOn();
            windowCloseAll(WindowClose::Force);
            s.subState = 1;
            return;
        case 1:
            actorsSweep(kVisitSweep, kSweepFree);
            scriptActorSpawnRecord(stateAddress(TableId::FireRuinVisitScriptsRuinVisitRecord)); // exe 0x511740
            placePlayer(int32_t(0xef800000), 0, 0x2000000, 0xc00);
            scriptSequenceStart(stateAddress(TableId::FireRuinVisitScriptsRuinVisitCamera), stateAddress(TableId::FireRuinVisitScriptsRuinVisitSteps));
            scriptSequenceTick();
            s.subState = 2;
            return;
        case 2:
            if (!fileLoading()) s.subState = 3;
            return;
        case 3:
            if (!streamBusy()) {
                voiceClipPlay(0x14);
                s.subState = 4;
            }
            return;
        case 4:
            if (voiceClipPlaying()) s.enterState(CutsceneState::Steps);
            return;
        default: return;
        }
    case CutsceneState::Steps: { // exe 0x526c60 (skip: fade 0x12)
        const ScriptStep step = scriptStepAt(s.steps.value());
        if (step.id == kScriptStepEnd) {
            s.state = uint8_t(CutsceneState::WrapUp);
        } else {
            if (const RoutineRef routine = stateRoutine(step.routine)) ruinVisitStep(routine);
            if (scriptSkipRequested() && display().drawBuffer != 0) {
                scriptFade(0x12, 0);
                s.state = uint8_t(CutsceneState::WrapUp);
            }
        }
        scriptSequenceTick();
        return;
    }
    case CutsceneState::WrapUp: // exe 0x526cb0
        switch (s.subState) {
        case 0:
            if (voiceClipPlaying()) voiceClipFadeOut(kVoiceFade);
            s.subState = 1;
            return;
        case 1:
            if (voiceClipIdle() && fadeState().busy == 0) s.subState = 2;
            return;
        case 2: {
            if (s.flags & kScriptSkipTaken) scriptFade(kSkippedFade, 0);
            cameraShake(true, 0, 0); // exe 0x40ea80
            actorsSweep(kVisitSweep, kSweepFree);
            spawnRecords(stateAddress(TableId::FireRuinVisitScriptsRuinVisitSpawns), 6);
            spawnRecords(stateAddress(TableId::FireRuinVisitScriptsRuinVisitMoreSpawns), 2);
            if (s.stepPhase == 0) soundPlayFlatForced(kRuinMusic);
            Player& p = player();
            p.animPaused = 0;
            p.pos.x = int32_t(0xf4000000);
            p.pos.y = 0;
            p.pos.z = 0x2000000;
            p.rotation.y = 0xc00;
            playerBackToPlay(); // exe 0x5723c0
            scriptReplayPadOff();
            scriptEndBlock();
            return;
        }
        default: return;
        }
    default: notPortedAt(kStoryPort, "script 0x2d state", s.state);
    }
}

} // namespace

bool fireRuinVisitScriptsPorted(RoutineId handler) {
    return handler == RoutineId::FireRuinEscapeEveScript || handler == RoutineId::FireRuinHallScript || handler == RoutineId::FireRuinVisitScript;
}
bool fireRuinVisitScriptsTick(const PlayerEnv&, RoutineId handler) {
    if (!fireRuinVisitScriptsPorted(handler)) return false;
    switch (handler) {
    case RoutineId::FireRuinEscapeEveScript: escapeEveScript(); break;
    case RoutineId::FireRuinHallScript: hallScript(); break;
    case RoutineId::FireRuinVisitScript: ruinVisitScript(); break;
    default: break;
    }
    return true;
}

} // namespace od::port
