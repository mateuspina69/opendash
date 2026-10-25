// The ruin under Nino's scripts (ST35), one routine per exe routine; the names are hypotheses from the observed
// behaviour. Each: a set-up, the steps runner with the player's face tracks (exe 0x53a5e0), a wrap-up; the casts
// (content/nino/guardian_cast) cued through their sub-state.
//   0x15 (exe 0x51d3e0, states 0x8a20cc; camera 0x8a1f8c (spawns the cast 0x8a1f78 = type 0x3a sub 1), steps 0x8a2050):
//        the guardian wakes (ST35 sub 5)
//   0x16 (exe 0x51d740, states 0x8a21d4; camera 0x8a20ec (the cast 0x8a20d8 = type 0x3a sub 2), steps 0x8a2168): the
//        guardian beaten (ST35 sub 5)
//   0x17 (exe 0x51d9e0, states 0x8a232c; camera 0x8a21f4 (the cast 0x8a21e0 = type 0x3a sub 3), steps 0x8a22c0): ST35
//        sub 0 on the way back, the second guardian drops in
//   0x19 (exe 0x51e8d0, states 0x8a298c; camera 0x8a2800 (the casts 0x8a27d8 = type 0x3a sub 4 and 0x8a27ec = type 0x24
//        sub 1), steps 0x8a2918): the second guardian beaten (ST35 sub 0)
#include "content/nino/st35_scripts.hpp"

#include "engine/routine_ids.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_sweeps.hpp"
#include "game/audio_logic.hpp"
#include "game/camera.hpp"
#include "game/fade_state.hpp"
#include "game/keyframe_tracks.hpp"
#include "game/player/player_input.hpp"
#include "game/player/player_motion.hpp"
#include "game/player/player_script_control.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/script_engine.hpp"
#include "game/script_state.hpp"
#include "game/story.hpp"
#include "game/transition.hpp"

namespace od::port {

namespace {

constexpr uint32_t kVoiceFade = 8;
constexpr uint32_t kSweepAll = kSweepActors, kSweepCast = 0x6fe;
constexpr uint8_t kSkipArrivalFade = 1;

int32_t tick() { return scriptState().stepTick; }
void anim(uint8_t a) { playerScriptAnimId(a, 0, 1); } // exe 0x5723d0(a, 0, 1)
void nextStep() { scriptSequenceSkip(1); }             // exe 0x5115c0(1)
// the player at (0, 0, z) facing yaw
void playerAtZ(int32_t z, int16_t yaw) {
    Player& p = player();
    p.pos.x = 0;
    p.pos.y = 0;
    p.pos.z = z;
    p.rotation.y = yaw;
}
// the cast actor k of the script cued (its sub-state)
void cue(size_t k, uint8_t v) { (*scriptState().actors[k]).subState = v; }
// the player's scripted walk: its velocity's lanes (moved in its facing each step)
void walk(int16_t x, int16_t y, int16_t z) {
    Player& p = player();
    p.velocity.x = x;
    p.velocity.y = y;
    p.velocity.z = z;
}
void walkTick() {
    Player& p = player();
    moveInFacing(p, p.velocity.x, p.velocity.y, p.velocity.z); // exe 0x571910
}

// The set-up's steps (the block's sub-state)
enum class SetUp : uint8_t { Start = 0, TakeOver = 1, Face = 2 };
enum class WrapUp : uint8_t { FadeVoice = 0, End = 1 };
enum class VoicePhase : uint8_t { Play = 0, Wait = 1 };

// the tail of the set-up once the player is script driven: animation 0, its directions' angles cleared, the face
// tracks on their tables, the sequence's first tick, the steps, unskippable
void faceSetup(uint32_t eyes, uint32_t mouth) {
    ScriptState& s = scriptState();
    anim(0);
    player().clearDirectionOffsetWords();
    s.faceTracks.init(eyes, mouth);  // exe 0x5e33a0
    s.faceTracks.first.select(0, 0);  // exe 0x5e33d0
    s.faceTracks.second.select(0, 0); // exe 0x5e33f0
    scriptSequenceTick();             // exe 0x5114d0
    s.enterState(CutsceneState::Steps);
    s.flags |= kScriptUnskippable;
}
// the set-up of scripts 0x15 / 0x17 (exe 0x51d400 / 0x51da00): the sequence and the pad first, then the player
void setUpFirst(uint32_t camera, uint32_t steps, uint32_t eyes, uint32_t mouth) {
    ScriptState& s = scriptState();
    switch (SetUp(s.subState)) {
    case SetUp::Start:
        scriptSequenceStart(camera, steps); // exe 0x511390
        scriptReplayPadOn();                // exe 0x5117f0
        s.subState += 1u;
        [[fallthrough]];
    case SetUp::TakeOver:
        if (playerToScriptIfFree() == 0) faceSetup(eyes, mouth); // exe 0x572300
        return;
    default: return;
    }
}
// the set-up of scripts 0x16 / 0x19 (exe 0x51d760 / 0x51e8f0): the pad, then once the player stands the sequence
void setUpStanding(uint32_t camera, uint32_t steps, uint32_t eyes, uint32_t mouth) {
    ScriptState& s = scriptState();
    switch (SetUp(s.subState)) {
    case SetUp::Start:
        scriptReplayPadOn();
        s.subState += 1u;
        [[fallthrough]];
    case SetUp::TakeOver:
        if (playerToScriptWhenStanding() != 0) return;
        scriptSequenceStart(camera, steps);
        s.subState += 1u;
        [[fallthrough]];
    case SetUp::Face: faceSetup(eyes, mouth); return;
    default: return;
    }
}
// the wrap-up (exe 0x51d4a0 / 0x51d810 / 0x51daa0 / 0x51e9a0): the voice faded, then the classes swept, the player
// back at (0, 0, z) facing 0, shown
void wrapUp(uint32_t classes, int32_t z) {
    ScriptState& s = scriptState();
    switch (WrapUp(s.subState)) {
    case WrapUp::FadeVoice:
        if (voiceClipPlaying()) voiceClipFadeOut(kVoiceFade); // exe 0x601220
        s.subState += 1u;
        [[fallthrough]];
    case WrapUp::End: {
        if (!voiceClipIdle() || fadeState().busy != 0) return;
        if (s.flags & kScriptSkipTaken) scriptFade(kSkipArrivalFade, 0);
        actorsSweep(classes, kSweepFree); // exe 0x5e05d0
        anim(0);
        playerBackToPlay();   // exe 0x5723c0
        scriptReplayPadOff(); // exe 0x511890
        Player& p = player();
        p.pos.x = 0;
        p.pos.y = 0;
        p.setDrawn(true);
        p.pos.z = z;
        p.rotation.y = 0;
        scriptEndBlock(); // exe 0x5116a0
        return;
    }
    default: return;
    }
}
// the voice steps (exe 0x51d550 / 0x51d8c0 / 0x51db50's / 0x51ea50): the clip, then once it plays skippable and the
// next step
void voiceStep(uint32_t clip) {
    ScriptState& s = scriptState();
    switch (VoicePhase(s.stepPhase)) {
    case VoicePhase::Play:
        voiceClipPlay(clip); // exe 0x601050
        s.stepPhase += 1u;
        return;
    case VoicePhase::Wait:
        if (voiceClipPlaying()) {
            s.flags &= uint8_t(~kScriptUnskippable);
            s.stepPhase = 0;
            nextStep();
        }
        return;
    default: return;
    }
}

// ---- script 0x15: the guardian wakes ------------------------------------------------------------------------------
void wakesStep(const RoutineRef& routine) {
    const int32_t t = tick();
    switch (routine.id()) {
    case RoutineId::St35WakesVoiceStep: voiceStep(2); return;
    case RoutineId::St35WakesStep2:
        if (t == 0) {
            actorsSweep(kSweepCast, kSweepFree);
            playerAtZ(0x9800000, 0x800);
        } else if (t == 0xf) {
            cameraShake(true, 0x600, 0x80); // exe 0x40ea80
            padVibrate(0x601, 0x410c0);     // exe 0x55cd10
        } else if (t == 0x23) {
            nextStep();
        }
        return;
    case RoutineId::St35WakesStep3:
        if (t == 0) {
            anim(0x80);
            scriptState().faceTracks.first.select(0, 0);
        } else if (t == 0x23) {
            nextStep();
        }
        return;
    case RoutineId::St35WakesStep4:
        if (t == 0) {
            Player& p = player();
            p.pos.z = int32_t(0xfc800000u);
            p.rotation.y = 0;
            anim(0x81);
            cue(0, 1);
        } else if (t == 0x87) {
            cue(0, 2);
        } else if (t == 0xf8) {
            cue(0, 3);
        } else if (t == 0x13b) {
            nextStep();
        }
        return;
    case RoutineId::St35WakesStep5:
        if (t == 0x104) cue(0, 4);
        else if (t == 0x1a4) cue(0, 6);
        else if (t == 0x1c2) nextStep();
        return;
    case RoutineId::St35WakesStep6:
        if (t == 0xe6) cue(0, 7);
        else if (t == 0x12c) nextStep();
        return;
    default: notPortedAt(kStoryPort, "script 0x15 step", routine.pointerValue());
    }
}
// ---- script 0x16: the guardian beaten -------------------------------------------------------------------------------
void beatenStep(const RoutineRef& routine) {
    const int32_t t = tick();
    switch (routine.id()) {
    case RoutineId::St35BeatenVoiceStep: voiceStep(4); return;
    case RoutineId::St35BeatenStep2:
        if (t == 0) {
            actorsSweep(kSweepCast, kSweepFree);
            playerAtZ(int32_t(0xfb000000u), 0);
            anim(0x81);
        } else if (t == 0xa5) {
            nextStep();
        }
        return;
    case RoutineId::St35BeatenStep3:
        if (t == 0) cue(0, 1);
        else if (t == 0x87) nextStep();
        return;
    case RoutineId::St35BeatenStep4:
        if (t == 0) cue(0, 2);
        else if (t == 0x3c) cue(0, 3);
        else if (t == 0x96) nextStep();
        return;
    default: notPortedAt(kStoryPort, "script 0x16 step", routine.pointerValue());
    }
}
// ---- script 0x17: the second guardian drops in ----------------------------------------------------------------------
void dropsInStep(const RoutineRef& routine) {
    ScriptState& s = scriptState();
    const int32_t t = tick();
    switch (routine.id()) {
    case RoutineId::St35DropsInStep1:
        if (s.stepPhase == 0) {
            voiceClipPlay(7);
            anim(0x82);
            player().rotation.y = 0;
            walk(0, 0, -0x180);
            s.stepPhase += 1u;
        } else {
            voiceStep(7);
        }
        walkTick();
        return;
    case RoutineId::St35DropsInStep2:
        switch (t) {
        case 0: {
            actorsSweep(kSweepAll, kSweepFree);
            Player& p = player();
            p.pos.x = 0;
            p.pos.y = 0;
            p.pos.z = 0x5400000;
            anim(0x82);
            break;
        }
        case 0x1b: anim(0x83); break;
        case 0x20: player().velocity.z = 0; break;
        case 0x34: anim(0x84); break;
        case 0x57: nextStep(); break;
        default: break;
        }
        walkTick();
        return;
    case RoutineId::St35DropsInStep3:
        if (t == 0x23) anim(0x85);
        else if (t == 0x32) nextStep();
        return;
    case RoutineId::St35DropsInStep4:
        if (t == 0) {
            player().setDrawn(false);
            cue(0, 1);
        } else if (t == 0x13b) {
            nextStep();
        }
        return;
    default: notPortedAt(kStoryPort, "script 0x17 step", routine.pointerValue());
    }
}
// ---- script 0x19: the second guardian beaten ------------------------------------------------------------------------
void dissolvesStep(const RoutineRef& routine) {
    const int32_t t = tick();
    switch (routine.id()) {
    case RoutineId::St35DissolvesVoiceStep: voiceStep(0xe); return;
    case RoutineId::St35DissolvesStep2:
        if (t == 0) {
            actorsSweep(kSweepAll, kSweepFree);
            playerAtZ(int32_t(0xf9c00000u), 0);
            walk(0, 0, 0);
        } else if (t == 0x13b) {
            nextStep();
        }
        return;
    case RoutineId::St35DissolvesStep3:
        if (t == 0x87) {
            cue(0, 1);
        } else if (t == 0x96) {
            anim(0x87);
            walk(0, 0, 0x30);
        } else if (t == 0xa5) {
            walk(0, 0, 0);
        } else if (t == 0xb4) {
            cue(0, 3);
        } else if (t == 0x1fe) {
            nextStep();
        }
        walkTick();
        return;
    case RoutineId::St35DissolvesStep4:
        if (t == 0x1e) anim(0x86);
        else if (t == 0xb4) cue(1, 1);
        else if (t == 0x10e) nextStep();
        return;
    case RoutineId::St35DissolvesStep5:
        if (t == 0x1e) {
            cue(1, 3);
        } else if (t == 0x3c) {
            cue(1, 4);
        } else if (t == 0x5a) {
            cue(0, 5);
        } else if (t == 0x6e) {
            anim(0x88);
            walk(0, 0, -0x140);
        } else if (t == 0x84) {
            walk(0, 0, 0);
        } else if (t == 0x159) {
            nextStep();
        }
        walkTick();
        return;
    default: notPortedAt(kStoryPort, "script 0x19 step", routine.pointerValue());
    }
}

void wakesScript() { // exe 0x51d3e0
    ScriptState& s = scriptState();
    switch (s.cutsceneState()) {
    case CutsceneState::SetUp: setUpFirst(stateAddress(TableId::St35ScriptsFirstSceneCamera), stateAddress(TableId::St35ScriptsFirstSceneSteps), stateAddress(TableId::St35ScriptsFirstSceneEyes), stateAddress(TableId::St35ScriptsFirstSceneMouth)); return;
    case CutsceneState::Steps: scriptStepsState(&wakesStep); return; // exe 0x53a5e0
    case CutsceneState::WrapUp: wrapUp(kSweepCast, 0x3000000); return;
    default: notPortedAt(kStoryPort, "script 0x15 state", s.state);
    }
}
void beatenScript() { // exe 0x51d740
    ScriptState& s = scriptState();
    switch (s.cutsceneState()) {
    case CutsceneState::SetUp: setUpStanding(stateAddress(TableId::St35ScriptsStandingSceneCamera), stateAddress(TableId::St35ScriptsStandingSceneSteps), stateAddress(TableId::St35ScriptsStandingSceneEyes), stateAddress(TableId::St35ScriptsStandingSceneMouth)); return;
    case CutsceneState::Steps: scriptStepsState(&beatenStep); return;
    case CutsceneState::WrapUp: wrapUp(kSweepCast, int32_t(0xfb000000u)); return;
    default: notPortedAt(kStoryPort, "script 0x16 state", s.state);
    }
}
void dropsInScript() { // exe 0x51d9e0
    ScriptState& s = scriptState();
    switch (s.cutsceneState()) {
    case CutsceneState::SetUp: setUpFirst(stateAddress(TableId::St35ScriptsSecondSceneCamera), stateAddress(TableId::St35ScriptsSecondSceneSteps), stateAddress(TableId::St35ScriptsSecondSceneEyes), stateAddress(TableId::St35ScriptsSecondSceneMouth)); return;
    case CutsceneState::Steps: scriptStepsState(&dropsInStep); return;
    case CutsceneState::WrapUp: wrapUp(kSweepAll, 0x4800000); return;
    default: notPortedAt(kStoryPort, "script 0x17 state", s.state);
    }
}
void dissolvesScript() { // exe 0x51e8d0
    ScriptState& s = scriptState();
    switch (s.cutsceneState()) {
    case CutsceneState::SetUp: setUpStanding(stateAddress(TableId::St35ScriptsLastSceneCamera), stateAddress(TableId::St35ScriptsLastSceneSteps), stateAddress(TableId::St35ScriptsLastSceneEyes), stateAddress(TableId::St35ScriptsLastSceneMouth)); return;
    case CutsceneState::Steps: scriptStepsState(&dissolvesStep); return;
    case CutsceneState::WrapUp: wrapUp(kSweepAll, int32_t(0xf8500000u)); return;
    default: notPortedAt(kStoryPort, "script 0x19 state", s.state);
    }
}


} // namespace

bool ninoRuinSt35ScriptPorted(RoutineId handler) {
    return handler == RoutineId::St35WakesScript || handler == RoutineId::St35BeatenScript || handler == RoutineId::St35DropsInScript ||
           handler == RoutineId::St35DissolvesScript;
}
bool ninoRuinSt35ScriptTick(const PlayerEnv&, RoutineId handler) {
    switch (handler) {
    case RoutineId::St35WakesScript: wakesScript(); return true;
    case RoutineId::St35BeatenScript: beatenScript(); return true;
    case RoutineId::St35DropsInScript: dropsInScript(); return true;
    case RoutineId::St35DissolvesScript: dissolvesScript(); return true;
    default: return false;
    }
}

} // namespace od::port
