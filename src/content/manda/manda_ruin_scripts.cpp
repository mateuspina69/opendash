// The story in the Manda ruin: its scripts and their cast (see manda_ruin_scripts.hpp). The scripts' states are their
// exe tables' (state 0 / the runner / state 2: scripts 9, 0xa, 0xb, 0xc, 0x1a run the ruin's runner exe 0x518e30,
// script 0xe the talk scripts' runner exe 0x52f420, script 0xf the cast runner exe 0x537c50); their steps are
// dispatched by the named routine. Step ticks are the scenes' timeline.
#include "content/manda/manda_ruin_scripts.hpp"
#include "content/manda/manda_ruin_scripts_state.hpp"

#include "engine/data_source.hpp"
#include "game/actor_animation.hpp"
#include "game/actor_model.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actor_sweeps.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/breakable_walls.hpp"
#include "game/camera.hpp"
#include "game/cutscene_actor.hpp"
#include "game/display.hpp"
#include "game/event.hpp"
#include "game/event_state.hpp"
#include "game/explosions.hpp"
#include "game/fade_state.hpp"
#include "game/field_alias.hpp"
#include "game/global_class_types.hpp"
#include "game/hit_list.hpp"
#include "game/message.hpp"
#include "game/player/player_input.hpp"
#include "game/player/player_motion.hpp"
#include "game/player/player_script_control.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/script_engine.hpp"
#include "game/script_state.hpp"
#include "game/skid_sparks.hpp"
#include "game/spawn.hpp"
#include "game/stage_files.hpp"
#include "game/stage_state.hpp"
#include "game/story_flags.hpp"
#include "game/transition.hpp"
#include "game/transition_state.hpp"

using namespace od::port::manda_ruin_scripts_state;

namespace od::port {

using namespace manda_ruin_scripts_state;

namespace {

constexpr const char* kRuinArea = "island story port"; // the area of its port-gap errors

constexpr uint8_t kSt12 = 0x12;
constexpr uint32_t kVoiceFadeStep = 8;
constexpr int16_t kMusicFadeSlow = 0x666, kMusicFadeMid = 0x333, kMusicFadeSkip = 0x222;
constexpr uint32_t kFadeInAfterSkip = 1;
constexpr uint16_t kSoundCue = 0x11, kSoundGuardianWakes = 0x12, kSoundBackToPlay = 0x10;


ScriptState& block() { return scriptState(); }
int stepTick() { return block().stepTick; }
uint8_t phase() { return block().stepPhase; }
uint8_t subPhase() { return block().stepSubPhase; }
void lockSkip(bool on) {
    if (on) block().flags |= kScriptUnskippable;
    else block().flags &= uint8_t(~kScriptUnskippable);
}
bool skipTaken() { return (block().flags & kScriptSkipTaken) != 0; }
bool fileLoaded() { return (fileLoads().loading & kFileLoading) == 0; }
bool streamIdle() { return (fileLoads().loading & kStreamBusy) == 0; }
void musicFadeOut(int16_t step) { soundFadeOut(kMusicFades, step, 0); }
bool musicFaded() { return soundDone(kMusicFades); }
// the player taken over: its move direction's angles and its face cleared
void clearPlayerForScript() {
    Player& p = player();
    p.clearDirectionOffsetWords();
    p.idleFaces[0] = 0;
    p.idleFaces[1] = 0;
}
// the player's 16.16 position (whole units, no fraction: the exe's dword stores) and yaw
void placePlayer(int16_t x, int16_t y, int16_t z, int16_t yaw) {
    Player& p = player();
    p.pos.x = int32_t(uint32_t(uint16_t(x)) << 16);
    p.pos.y = int32_t(uint32_t(uint16_t(y)) << 16);
    p.pos.z = int32_t(uint32_t(uint16_t(z)) << 16);
    p.rotation.y = yaw;
}
// the player's stage animation n (forced / as a request the playing one keeps)
void stageAnim(uint8_t n) { playerScriptAnimId(uint8_t(kStageAnims + n), 0, true); }
void stageAnimSoft(uint8_t n) { playerScriptAnimId(uint8_t(kStageAnims + n), 0, false); }
// the player walks along its yaw by `speed` (exe 0x5e36f0)
void walkAlongYaw(int speed) { moveAlongYaw(player().pos, player().rotation.y, 0, 0, speed); }
uint8_t playerBodyFrame() { return uint8_t(player().body.key.frame); }
// the type-1 buffers swapped and the new one walked (the next scene's bank)
void swapBank() {
    stage().bankBuffer ^= 1u;
    stageBankWalk(); // exe 0x5f3200
}
// the step's phases cleared (the word store) / one byte at a time, the next step
void nextStep() { scriptNextStep(); }
void nextStepByBytes() { scriptNextStepClearingPhases(); }

// exe 0x518e30: the ruin's runner -- the step's routine (when it has one), the skip (on a frame of buffer 1: the skip's
// wipe and the music faded), at the list's end or a skip state 2; the sequence's tick
void ruinStepsState(void (*routines)(const RoutineRef&)) {
    ScriptState& s = block();
    const ScriptStep step = scriptStepAt(s.steps.value());
    bool toWrapUp = step.id == kScriptStepEnd;
    if (!toWrapUp) {
        if (const RoutineRef routine = stateRoutine(step.routine)) routines(routine);
        if (scriptSkipRequested() != 0 && display().drawBuffer != 0) {
            scriptFade(kScriptSkipWipe, 0);
            musicFadeOut(kMusicFadeSkip);
            toWrapUp = true;
        }
    }
    if (toWrapUp) s.enterState(CutsceneState::WrapUp);
    scriptSequenceTick();
}

// ---- script 9: the first guardian hall (ST13 sub 1; steps 0x89fac0) ------------------------------------------------
// 0 exe 0x518f40: the player runs in, slows by its animation's frames; the music faded, the DAT 0x80, the voice 0 after
// 0x83; the next step once it plays
void stepFirstHallWalkIn() {
    bool next = false;
    switch (phase()) {
    case 0:
        playerScriptAnim(PlayerAnim::Run, 0, true);
        ++block().stepPhase;
        [[fallthrough]];
    case 1:
        walkAlongYaw(-0x200);
        if (player().pos.z.whole <= 0) {
            stageAnimSoft(1);
            next = true;
        }
        break;
    case 2:
        walkAlongYaw(-0x134);
        next = playerBodyFrame() == 2;
        break;
    case 3:
        walkAlongYaw(-0x72);
        next = playerBodyFrame() == 7;
        break;
    default: break;
    }
    if (next) ++block().stepPhase;
    switch (subPhase()) {
    case 0:
        musicFadeOut(kMusicFadeMid);
        ++block().stepSubPhase;
        return;
    case 1:
        if (musicFaded()) {
            datLoadRequest(0x80);
            ++block().stepSubPhase;
        }
        return;
    case 2:
        if (fileLoaded() && stepTick() > 0x83) {
            voiceClipPlay(0);
            ++block().stepSubPhase;
        }
        return;
    case 3:
        if (voiceClipPlaying()) {
            block().stepPhase = 0;
            block().stepSubPhase = 0;
            lockSkip(false);
            scriptSequenceSkip(1);
        }
        return;
    default: return;
    }
}
// 1 exe 0x5190c0: a sound at 0x40, the phases cleared at 0x117
void stepFirstHallSound() {
    if (stepTick() == 0x40) soundPlayFlatForced(kSoundCue);
    else if (stepTick() == 0x117) block().clearStepPhase();
}
// 2 exe 0x5190f0: the stage animation 2, the next step at 0x1e
void stepFirstHallLook() {
    if (phase() == 0) {
        stageAnimSoft(2);
        block().stepPhase = 1;
    }
    if (stepTick() == 0x1e) nextStep();
}
// 4 exe 0x519130: the stage animation 3 at 0x2d
void stepFirstHallPose() {
    if (stepTick() != 0x2d) return;
    stageAnimSoft(3);
    block().clearStepPhase();
}
// 5 exe 0x519160: the music faded at 0xe6 (the skip locked), the next step at 0xfa
void stepFirstHallEnd() {
    if (stepTick() == 0xe6) {
        musicFadeOut(kMusicFadeMid);
        lockSkip(true);
    } else if (stepTick() == 0xfa) {
        scriptSequenceSkip(1);
    }
}
void steps9(const RoutineRef& routine) {
    switch (routine.id()) {
    case RoutineId::MandaScript9FirstHallWalkInStep: stepFirstHallWalkIn(); return;
    case RoutineId::MandaScript9FirstHallSoundStep: stepFirstHallSound(); return;
    case RoutineId::MandaScript9FirstHallLookStep: stepFirstHallLook(); return;
    case RoutineId::MandaScript9FirstHallPoseStep: stepFirstHallPose(); return;
    case RoutineId::MandaScript9FirstHallEndStep: stepFirstHallEnd(); return;
    case RoutineId::EmptyRoutine: return;
    default: notPortedAt(kRuinArea, "script 9 step", routine.pointerValue());
    }
}
// exe 0x518d80: the actors swept, the sequence on the replay pad, the player placed; once it is script driven state 1
void script9SetUp() {
    ScriptState& s = block();
    if (s.subState == 0) {
        actorsSweep(kSweepActors, kSweepFree);
        scriptSequenceStart(stateAddress(TableId::MandaRuinScriptsScript9CameraScript), stateAddress(TableId::MandaRuinScriptsScript9Steps));
        scriptReplayPadOn();
        lockSkip(true);
        clearPlayerForScript();
        placePlayer(-0x200, -0xc1, 0x380, 0);
        s.subState = 1;
    } else if (s.subState != 1) {
        return;
    }
    if (playerToScriptIfFree() == 0) {
        s.enterState(CutsceneState::Steps);
        playerScriptAnim(PlayerAnim::Idle, 0, true);
    }
}
// exe 0x518e90: the voice faded; once it is idle, no fade runs and the music faded (a skip: a fade in), the actors
// swept, the guardian and its controller (records 0x89f96c x 2), the player back, a sound
void script9WrapUp() {
    ScriptState& s = block();
    switch (s.subState) {
    case 0:
        if (voiceClipPlaying()) voiceClipFadeOut(kVoiceFadeStep);
        break;
    case 1:
        if (voiceClipPlaying() || !fadeState().idle() || !musicFaded()) return;
        if (skipTaken()) scriptFade(kFadeInAfterSkip, 0);
        break;
    case 2:
        actorsSweep(kSweepActors, kSweepFree);
        spawnRecords(stateAddress(TableId::MandaRuinScriptsScript9WrapUpSpawns), 2);
        playerBackToPlay();   // exe 0x5723c0
        scriptReplayPadOff(); // exe 0x511890
        player().airborne = 0;
        scriptEndBlock();
        soundPlayFlatForced(kSoundGuardianWakes);
        return;
    default: return;
    }
    ++s.subState;
}

// ---- script 0xa: after the first guardian (ST13 sub 1, flag 0x5b1; steps 0x89fbb8) --------------------------------
// 0 exe 0x5193e0: a sound once the music faded, then the skip back
void stepAfterGuardianSound() {
    ScriptState& s = block();
    if (s.stepPhase == 0) {
        if (musicFaded()) {
            soundPlayFlatForced(kSoundCue);
            ++s.stepPhase;
        }
    } else if (s.stepPhase == 1) {
        lockSkip(false);
        s.stepPhase = 2;
    }
    if (stepTick() == 0) {
        s.stepPhase = 0;
        s.stepSubPhase = 0;
        lockSkip(false);
    }
}
// 2 exe 0x519450: the stage animation 4 at 0xe6, the player walks by its animation's frames
void stepAfterGuardianWalk() {
    bool next = false;
    switch (phase()) {
    case 0:
        if (stepTick() == 0xe6) {
            stageAnim(4);
            next = true;
        }
        break;
    case 1:
        walkAlongYaw(-0x133);
        next = playerBodyFrame() == 0x11;
        break;
    case 2:
        walkAlongYaw(-0x71);
        next = playerBodyFrame() == 0x17;
        break;
    default: break;
    }
    if (next) ++block().stepPhase;
    if (stepTick() == 299) block().clearStepPhase();
}
// the end of scripts 0xa / 0xc's steps (exe 0x5194f0 / 0x519d30): the figure freed, the voice and the music faded, the
// next DAT once the stream is idle, then (its byte set) the actors swept, the next step
void stepFreeAndLoad(uint32_t figureRecord, uint16_t dat, bool fadeVoice) {
    ScriptState& s = block();
    switch (s.stepPhase) {
    case 0:
        scriptActorFreeRecord(figureRecord); // exe 0x511760
        if (fadeVoice && voiceClipPlaying()) voiceClipFadeOut(kVoiceFadeStep);
        musicFadeOut(kMusicFadeMid);
        lockSkip(true);
        ++s.stepPhase;
        return;
    case 1:
        if (!voiceClipPlaying() && musicFaded()) ++s.stepPhase;
        return;
    case 2:
        if (streamIdle()) {
            datLoadRequest(dat);
            ++s.stepPhase;
        }
        return;
    case 3:
        if (fileLoaded()) {
            scriptState().part<RuinScriptBytes>().datLoaded = 1;
            actorsSweep(kSweepActors, kSweepFree);
            scriptSequenceSkip(1);
        }
        return;
    default: return;
    }
}
void stepsA(const RoutineRef& routine) {
    switch (routine.id()) {
    case RoutineId::MandaScriptAAfterGuardianSoundStep: stepAfterGuardianSound(); return;
    case RoutineId::MandaScriptABlockStep: block().clearStepPhase(); return;
    case RoutineId::MandaScriptAAfterGuardianWalkStep: stepAfterGuardianWalk(); return;
    case RoutineId::MandaScriptAFreeAndLoadStep: stepFreeAndLoad(stateAddress(TableId::MandaRuinScriptsFirstHallFigureRecord), 0x7e, true); return;
    default: notPortedAt(kRuinArea, "script 10 step", routine.pointerValue());
    }
}
// the set-up of scripts 0xa / 0xc (exe 0x5191c0 / 0x519a40): the music faded, the sequence on the replay pad, the
// figure; once the player is script driven it is placed, its byte cleared, the voice; state 1 once it plays
void afterGuardianSetUp(bool sweepFirst, uint32_t camera, uint32_t steps, uint32_t figureRecord, int16_t z,
                        uint32_t voice, int16_t x) {
    ScriptState& s = block();
    const uint8_t st = s.subState;
    if (st == 0) {
        if (sweepFirst) {
            actorsSweep(kSweepTransition, kSweepFree);
            musicFadeOut(kMusicFadeSlow);
            scriptReplayPadOn();
            scriptSequenceStart(camera, steps);
            scriptActorSpawnRecord(figureRecord); // exe 0x511740
            lockSkip(true);
        } else {
            musicFadeOut(kMusicFadeSlow);
            scriptReplayPadOn();
            scriptSequenceStart(camera, steps);
            lockSkip(true);
            actorsSweep(kSweepTransition, kSweepFree);
            scriptActorSpawnRecord(figureRecord); // exe 0x511740
        }
        scriptSequenceTick();
        ++s.subState;
    } else if (st == 2) {
        if (voiceClipPlaying()) s.enterState(CutsceneState::Steps);
        return;
    } else if (st != 1) {
        return;
    }
    if (playerToScriptIfFree() != 0) return;
    scriptState().part<RuinScriptBytes>().datLoaded = 0;
    clearPlayerForScript();
    placePlayer(x, -0xc1, z, 0);
    playerScriptAnim(PlayerAnim::Idle, 0, true);
    voiceClipPlay(voice);
    ++s.subState;
}
// exe 0x5192b0: the player idle, the actors swept, the voice faded; once it is idle the DAT 0x7e unless its step
// loaded it (once the music faded); then (a skip: a fade in) the next bank walked, the player back, a sound
void scriptAWrapUp() {
    ScriptState& s = block();
    switch (s.subState) {
    case 0:
        playerScriptAnim(PlayerAnim::Idle, 0, true);
        actorsSweep(kSweepTransition, kSweepFree);
        if (voiceClipPlaying()) voiceClipFadeOut(kVoiceFadeStep);
        s.stepPhase = 0;
        s.stepSubPhase = 0;
        ++s.subState;
        [[fallthrough]];
    case 1:
        if (!voiceClipPlaying()) {
            s.stepPhase = 0;
            s.subState = scriptState().part<RuinScriptBytes>().datLoaded != 0 ? 3u : 2u;
        }
        return;
    case 2:
        if (s.stepPhase == 0) {
            if (musicFaded()) {
                datLoadRequest(0x7e);
                ++s.stepPhase;
            }
        } else if (fileLoaded()) {
            s.subState = 3;
            s.stepPhase = 0;
        }
        return;
    case 3:
        if (!fadeState().idle()) return;
        if (skipTaken()) scriptFade(kFadeInAfterSkip, 0);
        swapBank();
        playerBackToPlay();   // exe 0x5723c0
        scriptReplayPadOff(); // exe 0x511890
        player().airborne = 0;
        player().setDrawn(true);
        scriptEndBlock();
        soundPlayFlatForced(kSoundBackToPlay);
        return;
    default: return;
    }
}

// ---- script 0xb: the second guardian appears (ST12 sub 5; steps 0x89fe48) ------------------------------------------
constexpr uint32_t kFlagBladeLanded = 0x680; // the falling blade (class 0x59c800) lands
// 0 exe 0x519770: the player walks, then turns -0x60 a tick to face a half turn (idle); the music faded, the DAT 0x7b,
// the next bank walked on a frame of buffer 1 and the voice 2; the next step once it plays
void stepSecondGuardianTurn() {
    ScriptState& s = block();
    const uint8_t c = s.stepPhase;
    if (c == 0) {
        s.stepPhase = 1;
    } else if (c == 1 || c == 2) {
        PlayerAnim anim = PlayerAnim::Walk;
        bool go = true;
        if (c == 2) {
            Player& p = player();
            p.rotation.y = int16_t(uint32_t(uint16_t(p.rotation.y)) - 0x60u);
            if (p.rotation.y > -0x801) {
                go = false;
            } else {
                p.rotation.y = kHalfTurn;
                anim = PlayerAnim::Idle;
            }
        }
        if (go) {
            playerScriptAnim(anim, 0, false);
            ++s.stepPhase;
        }
    }
    switch (s.stepSubPhase) {
    case 0:
        musicFadeOut(kMusicFadeSlow);
        ++s.stepSubPhase;
        return;
    case 1:
        if (musicFaded()) {
            datLoadRequest(0x7b);
            ++s.stepSubPhase;
        }
        return;
    case 2:
        if (s.stepPhase == 3 && fileLoaded() && display().drawBuffer == 1) {
            swapBank();
            voiceClipPlay(2);
            ++s.stepSubPhase;
        }
        return;
    case 3:
        if (voiceClipPlaying()) nextStep();
        return;
    default: return;
    }
}
// 1 exe 0x519890: the skip back; the stage animation 1 at 0xf; when the blade lands the player is knocked back (a
// shake, a sound) until 0x7d
void stepSecondGuardianBlade() {
    ScriptState& s = block();
    Player& p = player();
    switch (s.stepPhase) {
    case 0: s.stepPhase = 1; return;
    case 1:
        lockSkip(false);
        s.stepPhase = 2;
        [[fallthrough]];
    case 2:
        if (stepTick() == 0xf) {
            stageAnimSoft(1);
            ++s.stepPhase;
        }
        return;
    case 3:
        if (storyFlag(kFlagBladeLanded)) {
            soundPlayFlatForced(kSoundCue);
            cameraShake(0, 0x600, 0x40);
            playerScriptAnim(PlayerAnim::KnockDownBehind, 0, false);
            p.velocity.y = -0x300;
            soundPlayAtPos(0x97, p.pos);
            ++s.stepPhase;
        }
        return;
    case 4:
        moveAlongYaw(p.pos, p.rotation.y, 0, p.velocity.y, -0x280); // exe 0x5e36f0
        p.velocity.y += 0x40u;
        if (stepTick() != 0x7d) return;
        playerScriptAnim(PlayerAnim::KnockDownLandBehind, 0, false);
        s.stepPhase = 5;
        return;
    default: return;
    }
}
// 2 exe 0x5199a0: the player placed; the stage animation 2 at 0x4b
void stepSecondGuardianPlace() {
    if (stepTick() == 0) placePlayer(0, -0xc1, 0x280, 0);
    else if (stepTick() == 0x4b) stageAnimSoft(2);
}
// 3 exe 0x5199f0: at 100 the skip locked, the music faded
void stepSecondGuardianFade() {
    if (stepTick() != 100) return;
    lockSkip(true);
    musicFadeOut(kMusicFadeMid);
}
void stepsB(const RoutineRef& routine) {
    switch (routine.id()) {
    case RoutineId::MandaScriptBSecondGuardianTurnStep: stepSecondGuardianTurn(); return;
    case RoutineId::MandaScriptBSecondGuardianBladeStep: stepSecondGuardianBlade(); return;
    case RoutineId::MandaScriptBSecondGuardianPlaceStep: stepSecondGuardianPlace(); return;
    case RoutineId::MandaScriptBSecondGuardianFadeStep: stepSecondGuardianFade(); return;
    default: notPortedAt(kRuinArea, "script 0xb step", routine.pointerValue());
    }
}
// exe 0x5195f0: the actors swept, the sequence on the replay pad, the player placed; once it is script driven state 1
void scriptBSetUp() {
    ScriptState& s = block();
    if (s.subState == 0) {
        actorsSweep(kSweepTransition, kSweepFree);
        scriptSequenceStart(stateAddress(TableId::MandaRuinScriptsScriptBCameraScript), stateAddress(TableId::MandaRuinScriptsScriptBSteps));
        scriptReplayPadOn();
        lockSkip(true);
        clearPlayerForScript();
        placePlayer(0, -0xc1, -0x190, 0);
        scriptSequenceTick();
        s.subState = 1;
    } else if (s.subState != 1) {
        return;
    }
    if (playerToScriptIfFree() == 0) {
        s.enterState(CutsceneState::Steps);
        playerScriptAnim(PlayerAnim::Idle, 0, true);
    }
}
// exe 0x5196a0: the voice faded; once it is idle, no fade runs and the music faded (a skip: a fade in), the actors
// swept, the guardian, its blades and its controller (records 0x89fc7c x 7), the player back placed, a sound
void scriptBWrapUp() {
    ScriptState& s = block();
    switch (s.subState) {
    case 0:
        if (voiceClipPlaying()) voiceClipFadeOut(kVoiceFadeStep);
        s.subState = 1;
        [[fallthrough]];
    case 1:
        if (voiceClipPlaying() || !fadeState().idle() || !musicFaded()) return;
        if (skipTaken()) scriptFade(kFadeInAfterSkip, 0);
        ++s.subState;
        return;
    case 2:
        actorsSweep(kSweepTransition, kSweepFree);
        spawnRecords(stateAddress(TableId::MandaRuinScriptsScriptBWrapUpSpawns), 7);
        playerBackToPlay();   // exe 0x5723c0
        scriptReplayPadOff(); // exe 0x511890
        placePlayer(0, -0xc1, 0x280, 0);
        player().airborne = 0;
        scriptEndBlock();
        soundPlayFlatForced(kSoundGuardianWakes);
        return;
    default: return;
    }
}

// ---- script 0xc: after the second guardian (ST12 sub 5, flag 0x5b3; steps 0x89ff60) -------------------------------
// 0 exe 0x519c60: a sound once the music faded, the skip back
void stepAfterSecondSound() {
    if (phase() == 0 && musicFaded()) {
        soundPlayFlatForced(kSoundCue);
        lockSkip(false);
        ++block().stepPhase;
    }
}
// 2 exe 0x519ca0: the player runs at 0x28 until its animation's frame 7, then idles; the next step at 0x5a
void stepAfterSecondWalk() {
    ScriptState& s = block();
    switch (s.stepPhase) {
    case 0:
        if (stepTick() == 0x28) {
            playerScriptAnim(PlayerAnim::Run, 0, false);
            ++s.stepPhase;
        }
        return;
    case 1:
        walkAlongYaw(-0x200);
        if (playerBodyFrame() == 7) {
            playerScriptAnim(PlayerAnim::Idle, 0, false);
            ++s.stepPhase;
        }
        return;
    case 2:
        if (stepTick() == 0x5a) nextStep();
        return;
    default: return;
    }
}
void stepsC(const RoutineRef& routine) {
    switch (routine.id()) {
    case RoutineId::MandaScriptCAfterSecondSoundStep: stepAfterSecondSound(); return;
    case RoutineId::MandaScriptABlockStep: block().clearStepPhase(); return;
    case RoutineId::MandaScriptCAfterSecondWalkStep: stepAfterSecondWalk(); return;
    case RoutineId::MandaScriptCFreeAndLoadStep: stepFreeAndLoad(stateAddress(TableId::MandaRuinScriptsSecondHallFigureRecord), 0x7a, false); return;
    default: notPortedAt(kRuinArea, "script 0xc step", routine.pointerValue());
    }
}
// exe 0x519b20: the player idle, the actors swept, the voice faded; once it is idle the DAT 0x7a unless its step loaded
// it (once the music faded); then (a skip: a fade in) the next bank walked, the player back, the message trigger
// (record 0x89fe94), a sound
void scriptCWrapUp() {
    ScriptState& s = block();
    switch (s.subState) {
    case 0:
        playerScriptAnim(PlayerAnim::Idle, 0, true);
        actorsSweep(kSweepActors, kSweepFree);
        if (voiceClipPlaying()) voiceClipFadeOut(kVoiceFadeStep);
        s.subState = 1;
        s.stepPhase = 0;
        s.stepSubPhase = 0;
        [[fallthrough]];
    case 1:
        if (!voiceClipPlaying()) {
            s.stepPhase = 0;
            s.stepSubPhase = 0;
            s.subState = scriptState().part<RuinScriptBytes>().datLoaded != 0 ? 3u : 2u;
        }
        return;
    case 2:
        if (s.stepPhase == 0) {
            if (musicFaded()) {
                datLoadRequest(0x7a);
                ++s.stepPhase;
            }
        } else if (fileLoaded()) {
            s.subState = 3;
            s.stepPhase = 0;
        }
        return;
    case 3:
        if (!fadeState().idle()) return;
        if (skipTaken()) scriptFade(kFadeInAfterSkip, 0);
        swapBank();
        playerBackToPlay();   // exe 0x5723c0
        scriptReplayPadOff(); // exe 0x511890
        player().setDrawn(true);
        player().airborne = 0;
        spawnRecords(stateAddress(TableId::MandaRuinScriptsScriptCWrapUpSpawns), 1);
        scriptEndBlock();
        soundPlayFlatForced(kSoundBackToPlay);
        return;
    default: return;
    }
}

// ---- script 0x1a: the last hall (ST14 sub 4; steps 0x8a2b48) ---------------------------------------------------------
constexpr uint32_t kMusicChannel0 = 1; // the fade entry of the first music channel
// 0 exe 0x51ee70: the player idle; once the first channel faded and no voice plays the DAT 0x84; once it is in, on a
// frame of buffer 1, the next bank walked and the voice 4; once it plays a sound, the skip back, the next step
void stepLastHallVoice() {
    ScriptState& s = block();
    switch (s.stepPhase) {
    case 0:
        playerScriptAnim(PlayerAnim::Idle, 0, true);
        ++s.stepPhase;
        return;
    case 1:
        if (soundDone(kMusicChannel0) && !voiceClipPlaying()) {
            datLoadRequest(0x84);
            ++s.stepPhase;
        }
        return;
    case 2:
        if (fileLoaded() && display().drawBuffer == 1) {
            swapBank();
            voiceClipPlay(4);
            ++s.stepPhase;
        }
        return;
    case 3:
        if (voiceClipPlaying()) {
            s.stepPhase = 0;
            s.stepSubPhase = 0;
            lockSkip(false);
            soundPlayFlatForced(0x13);
            scriptSequenceSkip(1);
        }
        return;
    default: return;
    }
}
// 4 exe 0x51ef90: at 0x78 the stage animation 3 facing a quarter turn, it walks until 0x91, the next step at 0x9b; the
// footsteps; the music faded at 0x46 (the skip locked)
void stepLastHallWalk() {
    ScriptState& s = block();
    const uint8_t c = s.stepPhase;
    bool next = false;
    if (c == 0) {
        if (stepTick() == 0x78) {
            stageAnim(3);
            player().rotation.y = kQuarterTurn;
            next = true;
        }
    } else if (c == 1) {
        walkAlongYaw(-0x100);
        next = stepTick() == 0x91;
    } else if (c == 2 && stepTick() == 0x9b) {
        s.stepPhase = 0;
        s.stepSubPhase = 0;
        scriptSequenceSkip(1);
    }
    if (next) ++s.stepPhase;
    const int32_t t = stepTick();
    if (t == 0x83 || t == 0x8c || t == 0x92 || t == 0x9a) soundPlayAtPos(0x91, player().pos);
    if (stepTick() == 0x46) {
        musicFadeOut(0x16c);
        lockSkip(true);
    }
}
// 5 exe 0x51f060: the skip back once the music faded, a sound at 0x3c, the stage animation 4 at 0x5a
void stepLastHallEnd() {
    if (phase() == 0 && musicFaded()) {
        lockSkip(false);
        ++block().stepPhase;
    }
    if (stepTick() == 0x3c) soundPlayFlatForced(0x14);
    if (stepTick() == 0x5a) stageAnim(4);
}
void steps1a(const RoutineRef& routine) {
    switch (routine.id()) {
    case RoutineId::MandaScript1aLastHallVoiceStep: stepLastHallVoice(); return;
    case RoutineId::MandaScript1aStep2:
        if (stepTick() == 0x78) stageAnim(1);
        return;
    case RoutineId::MandaScript1aStep3:
        if (stepTick() == 0x78) stageAnim(2);
        return;
    case RoutineId::MandaScript1aLastHallWalkStep: stepLastHallWalk(); return;
    case RoutineId::MandaScript1aLastHallEndStep: stepLastHallEnd(); return;
    case RoutineId::EmptyRoutine: return;
    default: notPortedAt(kRuinArea, "script 0x1a step", routine.pointerValue());
    }
}
// exe 0x51ece0: the actors swept, window 4 closed, the sequence on the replay pad, the player placed; once it is script
// driven the first channel faded, state 1
void script1aSetUp() {
    ScriptState& s = block();
    if (s.subState == 0) {
        actorsSweep(kSweepActors, kSweepFree);
        windowClose(4, WindowClose::Force); // exe 0x5576b0
        scriptSequenceStart(stateAddress(TableId::MandaRuinScriptsScript1aCameraScript), stateAddress(TableId::MandaRuinScriptsScript1aSteps));
        scriptReplayPadOn();
        lockSkip(true);
        clearPlayerForScript();
        placePlayer(0xa00, -0xc0, 0, 0xc00);
        s.subState = 1;
    } else if (s.subState != 1) {
        return;
    }
    if (playerToScriptIfFree() == 0) {
        soundFadeOut(kMusicChannel0, kMusicFadeSlow, 0);
        s.enterState(CutsceneState::Steps);
        playerScriptAnim(PlayerAnim::Idle, 0, true);
    }
}
// exe 0x51eda0: the voice faded; once it is idle and no fade runs (a skip: once the music faded a fade in, a sound) the
// guardian (records 0x8a29c0 x 5), the player back placed
void script1aWrapUp() {
    ScriptState& s = block();
    const uint8_t st = s.subState;
    if (st == 0) {
        if (voiceClipPlaying()) voiceClipFadeOut(kVoiceFadeStep);
        ++s.subState;
    } else if (st == 2) {
        actorsSweep(kSweepActors, kSweepFree);
        spawnRecords(stateAddress(TableId::MandaRuinScriptsScript1aWrapUpSpawns), 5);
        playerBackToPlay();   // exe 0x5723c0
        scriptReplayPadOff(); // exe 0x511890
        player().airborne = 0;
        placePlayer(0x600, -0x1e0, 0x400, 0);
        scriptEndBlock();
        return;
    } else if (st != 1) {
        return;
    }
    if (voiceClipPlaying() || !fadeState().idle()) return;
    if (skipTaken()) {
        if (!musicFaded()) return;
        scriptFade(kFadeInAfterSkip, 0);
        soundPlayFlatForced(0x14);
    }
    ++s.subState;
}

// ---- script 0xe: the ruin's guardian beaten (ST14 sub 4, flag 0x5b5; steps 0x8a0180) ------------------------------
// 0 exe 0x51a850: the actors swept, the guardian's wreck (record 0x8a00e0); the skip back once the music faded
void stepGuardianWreck() {
    ScriptState& s = block();
    if (stepTick() == 0) {
        actorsSweep(kSweepActors, kSweepFree);
        scriptActorSpawnRecord(stateAddress(TableId::MandaRuinScriptsGuardianWreckRecord));
    }
    if (s.stepSubPhase == 0 && musicFaded()) {
        lockSkip(false);
        ++s.stepSubPhase;
    }
}
// 1 exe 0x51a8a0: the second figure (record 0x8a00f4) at 0x1e; the player turns 0x60 a tick from 0x3c (its shuffle),
// at 0x51 it jumps (falling, its speeds) until 100
void stepGuardianJump() {
    ScriptState& s = block();
    Player& p = player();
    if (stepTick() == 0x1e) scriptActorSpawnRecord(stateAddress(TableId::MandaRuinScriptsGuardianJumpRecord));
    switch (s.stepPhase) {
    case 0:
        if (stepTick() == 0x3c) {
            playerScriptAnim(PlayerAnim::Shuffle, 0, true);
            ++s.stepPhase;
        }
        return;
    case 1:
        p.rotation.y += 0x60u;
        if (stepTick() == 0x51) {
            playerScriptAnim(PlayerAnim::RunningJumpFall, 0, true);
            p.velocity.z = 0x200;
            p.acceleration.y = 0x240;
            ++s.stepPhase;
        }
        return;
    case 2:
        p.velocity.y += uint16_t(p.acceleration.y >> 4);
        integrateVelocity(p, 0, p.velocity.y, -int(p.velocity.z));
        if (stepTick() == 100) {
            p.velocity.z = 0;
            ++s.stepPhase;
        }
        return;
    default: return;
    }
}
void stepsE(const RoutineRef& routine) {
    switch (routine.id()) {
    case RoutineId::MandaScriptEGuardianWreckStep: stepGuardianWreck(); return;
    case RoutineId::MandaScriptEGuardianJumpStep: stepGuardianJump(); return;
    default: notPortedAt(kRuinArea, "script 0xe step", routine.pointerValue());
    }
}
// exe 0x51a680: the replay pad, the music faded; once the player stands free it is placed, the sequence, state 1
void scriptESetUp() {
    ScriptState& s = block();
    if (s.subState == 0) {
        scriptReplayPadOn();
        musicFadeOut(kMusicFadeSlow);
        s.subState = 1;
        lockSkip(true);
    } else if (s.subState != 1) {
        return;
    }
    if (playerToScriptWhenStanding() != 0) return; // exe 0x572320
    s.enterState(CutsceneState::Steps);
    scriptState().part<RuinScriptBytes>().datLoaded = 0;
    clearPlayerForScript();
    placePlayer(0x420, -0x1e0, 0x220, 0xa00);
    playerScriptAnim(PlayerAnim::Idle, 0, true);
    scriptSequenceStart(stateAddress(TableId::MandaRuinScriptsScriptECameraScript), stateAddress(TableId::MandaRuinScriptsScriptESteps));
}
// exe 0x51a730: the player idle, the actors swept; the DAT 0x83 unless loaded; then (a skip: a fade in) the player back
// placed, the reward on its pedestal (record 0x8a0108), a sound
void scriptEWrapUp() {
    ScriptState& s = block();
    switch (s.subState) {
    case 0:
        playerScriptAnim(PlayerAnim::Idle, 0, true);
        actorsSweep(kSweepActors, kSweepFree);
        s.subState = 1;
        [[fallthrough]];
    case 1:
        if (scriptState().part<RuinScriptBytes>().datLoaded == 0) {
            s.subState = 2;
            s.stepPhase = 0;
        } else {
            s.subState = 3;
        }
        return;
    case 2:
        if (s.stepPhase == 0) {
            datLoadRequest(0x83);
            ++s.stepPhase;
        } else if (fileLoaded()) {
            s.subState = 3;
            s.stepPhase = 0;
        }
        return;
    case 3:
        if (!fadeState().idle()) return;
        if (skipTaken()) scriptFade(kFadeInAfterSkip, 0);
        playerBackToPlay();   // exe 0x5723c0
        scriptReplayPadOff(); // exe 0x511890
        player().setDrawn(true);
        player().airborne = 0;
        placePlayer(0x380, 0x40, 0x180, 0x200);
        spawnRecords(stateAddress(TableId::MandaRuinScriptsScriptEWrapUpSpawns), 1);
        scriptEndBlock();
        soundPlayFlatForced(kSoundBackToPlay);
        return;
    default: return;
    }
}

// ---- script 0xf: the blue card at the terminal (ST12 sub 0, flag 0x5b6; steps 0x8a0338) ----------------------------
constexpr uint32_t kFlagTerminalDone = 0x59f;
constexpr uint8_t kPlatformSparkVariant = 0x15;
constexpr int16_t kPlatformX = 0x100, kPlatformY = -0xa0, kPlatformZ = 0x100, kPlatformRadius = 0x120;
// exe 0x51afd0: four sparks (the global skid sparks, variant 0x15) on a circle of radius 0x120 around the platform's
// centre (<< the stage shift), angles ((counter + k) & 0xf) << 8 for k = 12, 8, 4, 0; then the counter + 1
void platformSparks() {
    TerminalScriptBytes& bytes = scriptState().part<TerminalScriptBytes>();
    for (int k = 0xc; k >= 0; k -= 4) {
        const uint32_t angle = ((uint32_t(bytes.sparkAngle) + uint32_t(k)) << 8) & kAngleMask;
        const int c = fixedCos(int(angle)), s = fixedSin(int(angle));
        const uint32_t shift = stage().coordinateShift & 0x1f;
        const int32_t x = ((c * kPlatformRadius) >> 12) + kPlatformX, z = ((s * kPlatformRadius) >> 12) + kPlatformZ;
        const uint32_t slot = allocActorA0(0); // exe 0x5e13b0
        if (slot == 0) continue;
        EffectActor& e = effectActorAt(slot);
        e.flags = kActorLive;
        e.type = typeByte(GlobalEffectType::SkidSparks);
        e.loopFlags = kActorSweepGroup1 | kActorLateLoop;
        e.variant = kPlatformSparkVariant;
        e.pos.x = int16_t(uint32_t(x) << shift);
        e.pos.y = int16_t(uint32_t(int32_t(kPlatformY)) << shift);
        e.pos.z = int16_t(uint32_t(z) << shift);
        auto&& spark = classState<SkidSparkFields>(e);
        spark.yaw = uint16_t(angle);
    }
    ++bytes.sparkAngle;
}
// 0 exe 0x51abe0: the player walks (the speeds 0x40 / 0x50 alternating from 100, its shuffle at 100); a shake at 0x3c;
// at 0x96 the warp to ST12 sub 6
void stepTerminalWalk() {
    TerminalScriptBytes& bytes = scriptState().part<TerminalScriptBytes>();
    switch (stepTick()) {
    case 0:
        bytes.setWalkSpeedBytes(0, 0);
        lockSkip(false);
        break;
    case 0x3c:
        cameraShake(1, 0x100, 0);
        padVibrate(0, 0x1000050);
        break;
    case 100:
        playerScriptAnim(PlayerAnim::Shuffle, 0, true);
        [[fallthrough]];
    case 0x6e:
    case 0x7c:
    case 0x8c: bytes.setWalkSpeedBytes(0x40, 0); break;
    case 0x68:
    case 0x76:
    case 0x84: bytes.setWalkSpeedBytes(0x50, 0); break;
    case 0x96:
        requestTransition(kTransitionWarp, kSt12, 6, 0x100, -1, -0x2f8, kHalfTurn, kNoFade, kNoFade);
        scriptSequenceSkip(1);
        break;
    default: break;
    }
    moveInFacing(player(), 0, 0, bytes.walkSpeed);
}
// 1 exe 0x51ad80: the player hidden, a shake; from 0x33 every other tick the platform's sparks
void stepPlatformRises() {
    if (stepTick() == 0) {
        scriptState().part<TerminalScriptBytes>().sparkAngle = 0;
        player().flags &= uint8_t(~kActorLiveDrawn);
        cameraShake(1, 0xa0, 0);
    }
    if (stepTick() > 0x32 && (uint32_t(stepTick()) & 1) == 0) platformSparks();
}
// 2 exe 0x51add0: the sparks every other tick
void stepPlatformSparks() {
    if (stepTick() >= 0 && (uint32_t(stepTick()) & 1) == 0) platformSparks();
}
// 4 exe 0x51adf0: at 0x50 the player shown, the warp back to ST12 sub 0 at the terminal, idle
void stepBackToTerminal() {
    if (stepTick() != 0x50) return;
    player().flags |= kActorLiveDrawn;
    requestTransition(kTransitionWarp, kSt12, 0, 0, -0x110, -0x7a0, kHalfTurn, kNoFade, kNoFade);
    player().airborne = 0;
    playerScriptAnim(PlayerAnim::Idle, 0, true);
    scriptSequenceSkip(1);
}
// 5 exe 0x51ae80: the five cells of the platform (row 0x3f, columns 0x3b..0x43 step 2) up (variant 2), the actors swept
void stepPlatformUp() {
    if (stepTick() != 0) return;
    for (uint32_t x = 0x3b; x <= 0x43; x += 2) switchGridCellVariant(x, 0x3f, 2);
    actorsSweep(kSweepActors, kSweepFree);
}
void stepsF(const RoutineRef& routine) {
    switch (routine.id()) {
    case RoutineId::MandaScriptFTerminalWalkStep: stepTerminalWalk(); return;
    case RoutineId::MandaScriptFPlatformRisesStep: stepPlatformRises(); return;
    case RoutineId::MandaScriptFPlatformSparksStep: stepPlatformSparks(); return;
    case RoutineId::MandaScriptFBackToTerminalStep: stepBackToTerminal(); return;
    case RoutineId::MandaScriptFPlatformUpStep: stepPlatformUp(); return;
    case RoutineId::EmptyRoutine: return;
    default: notPortedAt(kRuinArea, "script 0xf step", routine.pointerValue());
    }
}
// exe 0x51a9a0: the actors swept, the replay pad, the voice; once it plays and the player is script driven, placed at
// the terminal, the sequence, state 1
void scriptFSetUp() {
    ScriptState& s = block();
    if (s.subState == 0) {
        actorsSweep(kSweepTransition, kSweepFree);
        scriptReplayPadOn(); // exe 0x5117f0
        lockSkip(true);
        voiceClipPlay(0x13);
        ++s.subState;
    } else if (s.subState != 1) {
        return;
    }
    if (!voiceClipPlaying() || playerToScriptIfFree() != 0) return;
    s.enterState(CutsceneState::Steps);
    clearPlayerForScript();
    placePlayer(0, -0x110, -0x7a0, kHalfTurn);
    playerScriptAnim(PlayerAnim::Idle, 0, true);
    scriptSequenceStart(stateAddress(TableId::MandaRuinScriptsScriptFCameraScript), stateAddress(TableId::MandaRuinScriptsScriptFSteps));
}
// exe 0x51aa60: the rumble off, idle, the actors swept, the voice faded; once it is idle and no fade runs the request
// back to the terminal (a load with a fade in while the screen is covered, else a warp); once taken the player back,
// its flag
void scriptFWrapUp() {
    ScriptState& s = block();
    switch (s.subState) {
    case 0:
        padVibrate(0, 0x1000000);
        playerScriptAnim(PlayerAnim::Idle, 0, true);
        actorsSweep(kSweepActors, kSweepFree);
        if (voiceClipPlaying()) voiceClipFadeOut(kVoiceFadeStep);
        ++s.subState;
        return;
    case 1:
        if (voiceClipPlaying() || !fadeState().idle()) return;
        if (fadeState().covered != 0)
            requestTransition(kTransitionAccepted, kSt12, 0, 0, -0x110, -0x7a0, kHalfTurn, 2, kNoFade);
        else
            requestTransition(kTransitionWarp, kSt12, 0, 0, -0x110, -0x7a0, kHalfTurn, kNoFade, kNoFade);
        ++s.subState;
        return;
    case 2:
        if (transitionRequest().state != kTransitionNone) return;
        playerBackToPlay();   // exe 0x5723c0
        scriptReplayPadOff(); // exe 0x511890
        player().flags |= kActorLiveDrawn;
        player().airborne = 0;
        setStoryFlag(kFlagTerminalDone);
        scriptEndBlock();
        return;
    default: return;
    }
}

// ---- the dispatch ------------------------------------------------------------------------------------------------------
template <void (*SetUp)(), void (*Run)(), void (*WrapUp)()> void runScript(const char* name) {
    switch (block().cutsceneState()) {
    case CutsceneState::SetUp: SetUp(); return;
    case CutsceneState::Steps: Run(); return;
    case CutsceneState::WrapUp: WrapUp(); return;
    default: notPortedAt(kRuinArea, name, block().state);
    }
}
void script9Steps() { ruinStepsState(&steps9); }
void scriptASetUp() { afterGuardianSetUp(true, stateAddress(TableId::MandaRuinScriptsScriptACameraScript), stateAddress(TableId::MandaRuinScriptsScriptASteps), stateAddress(TableId::MandaRuinScriptsFirstHallFigureRecord), 0, 1, -0x200); }
void scriptASteps() { ruinStepsState(&stepsA); }
void scriptBSteps() { ruinStepsState(&stepsB); }
void scriptCSetUp() { afterGuardianSetUp(false, stateAddress(TableId::MandaRuinScriptsScriptCCameraScript), stateAddress(TableId::MandaRuinScriptsScriptCSteps), stateAddress(TableId::MandaRuinScriptsSecondHallFigureRecord), 0x200, 3, 0); }
void scriptCSteps() { ruinStepsState(&stepsC); }
void script1aSteps() { ruinStepsState(&steps1a); }
void scriptESteps() { scriptStepsStateNoFaces(&stepsE); }
void scriptFSteps() { scriptStepsStateForCast(&stepsF); }

const DataTable<CollisionBoxRecord> groundedFigureBox{TableId::MandaRuinScriptsGroundedFigureBox};
void groundedFigure(const PlayerEnv& env, AnimatedActor& a) {
    a.savePreviousPose();
    a.prevAnim = a.anim;
    if (a.state == 0) {
        if (!bindActorModel(a, 0)) {
            freeActor(a);
            return;
        }
        a.collisionBox.pointAt(groundedFigureBox.stateAddress());
        a.clearClassBlock();
        a.anim = 7;
        ++a.state;
        a.prevAnim = kNoAnimShown;
    }
    if (!(a.flags & kActorLive)) return;
    a.pos.y.whole = int16_t(actorGround(env, a));
    if (a.anim == a.prevAnim) {
        animStepPose(a);
        return;
    }
    animSetPose(a, a.anim, classState<GroundedFigureFields>(a).startKey);
}

// The parameters of the ST14 flame statics (stage static type 0x11) its end spawns (hypothesis: size and ticks)
struct FlameParams {
    explicit FlameParams(const StaticActor& actor) : FlameParams(const_cast<StaticActor&>(actor)) {}
    explicit FlameParams(StaticActor& actor) : size(actor.params, 0), ticks(actor.params, 2) {}
    BytesField<uint16_t, 4> size;
    BytesField<uint16_t, 4> ticks;
};
const DataTable<CollisionBoxRecord> burningWreckBox{TableId::MandaRuinScriptsBurningWreckBox};
constexpr uint8_t kFlameType = 0x11;
constexpr uint16_t kWreckTint = 0x421f, kWreckTintBase = 0x4200;
// a static of the stage's / the global table: live, its type and variant, the sweep group 2
StaticActor* spawnStatic(bool stageClass, uint8_t type, uint8_t variant) {
    const uint32_t slot = allocActor60(stageClass ? kActorStageClass : 0);
    if (!slot) return nullptr;
    StaticActor& e = staticActorAt(slot);
    e.flags = kActorLive;
    e.type = type;
    e.variant = variant;
    e.loopFlags |= kActorSweepGroup2;
    return &e;
}
// exe 0x491ef0: a blast (the global static type 7) at a random point around the wreck
void randomBlast(const AnimatedActor& a) {
    const uint32_t r1 = rngStep(), r2 = rngStep(), r3 = rngStep();
    const int16_t x = a.pos.x.whole, y = a.pos.y.whole, z = a.pos.z.whole;
    StaticActor* e = spawnStatic(false, typeByte(GlobalStaticType::Blast), 0); // exe 0x5e1300
    if (!e) return;
    e->pos.x.whole = int16_t(x - int16_t(r1 & 0x7f) + 0x40);
    e->pos.y.whole = int16_t(y - int16_t((r2 >> 8) & 0x7f) - 0x80);
    e->pos.z.whole = int16_t(z - int16_t((r3 >> 16) & 0x7f) + 0x40);
    auto&& blast = BlastParams(actorHeaderAt(e->at));
    blast.size = 0x80;
    blast.attack = 0;
}
// exe 0x491fb0 (variant 0, size 0x80) / 0x492000 (variant 0x10, size 0x200): a flame (the stage static 0x11) on it
void flame(const AnimatedActor& a, uint8_t variant, uint16_t size) {
    const int16_t x = a.pos.x.whole, y = a.pos.y.whole, z = a.pos.z.whole;
    StaticActor* e = spawnStatic(true, kFlameType, variant); // exe 0x5e12f0
    if (!e) return;
    e->pos.x.whole = x;
    e->pos.y.whole = y;
    e->pos.z.whole = z;
    FlameParams params(*e);
    params.size = size;
    params.ticks = 0xf0;
}
// exe 0x491d00: the model (none: freed), still, its life and timer, on the ground, animation 0xc posed
void burningWreckBind(const PlayerEnv& env, AnimatedActor& a) {
    BurningWreckFields& f = classState<BurningWreckFields>(a);
    if (!bindActorModel(a, 0)) {
        freeActor(a);
        return;
    }
    a.clearClassBlock();
    a.collisionBox.pointAt(burningWreckBox.stateAddress());
    a.velocity.clearWords();
    a.acceleration.clearWords();
    a.airborne = kAirborneFalling;
    a.tint = kWreckTint;
    f.life = 0x82;
    f.blastTimer = 0x2d;
    f.groundY = int16_t(actorGround(env, a));
    a.anim = 0xc;
    a.params[3] = 0;
    animSetPose(a, 0xc, 0);
    a.subState = 0;
    ++a.state;
    a.step = 0x10;
}
// exe 0x491d90: the colour by the step, a blast when the timer runs out (sooner near the end), the flames at life 2 / 0
// (then state 2), moved along its yaw, animated
void burningWreckRun(AnimatedActor& a) {
    BurningWreckFields& f = classState<BurningWreckFields>(a);
    a.savePreviousPose();
    a.prevAnim = a.anim;
    a.tint = uint16_t(kWreckTintBase | a.step);
    --f.blastTimer;
    if (f.blastTimer == 0) {
        const uint8_t c = uint8_t(a.step + 1u);
        a.step = c;
        if (c >= 0x20) a.step = 0x1f;
        if (f.life >= 0x1f) f.blastTimer = int16_t((rngStep() & 7) + 6u);
        else f.blastTimer = 4;
        randomBlast(a);
    }
    --f.life;
    if (f.life == 2) flame(a, 0, 0x80);
    if (f.life == 0) {
        flame(a, 0x10, 0x200);
        ++a.state;
    }
    a.velocity.y += uint16_t(a.acceleration.y);
    a.velocity.z += uint16_t(a.acceleration.z);
    a.velocity.x += uint16_t(a.acceleration.x);
    moveAlongYaw(a.pos, a.rotation.y, a.velocity.x, a.velocity.y, a.velocity.z); // exe 0x5e36f0
    if (a.anim != a.prevAnim) {
        animSetPose(a, a.anim, int8_t(a.params[3]));
        return;
    }
    animStepPose(a);
}
void burningWreck(const PlayerEnv& env, AnimatedActor& a) {
    switch (a.state) { // [0x892a48]
    case 0: burningWreckBind(env, a); return;
    case 1: burningWreckRun(a); return;
    case 2: freeActor(a); return; // exe 0x468fa0
    default: notPortedAt("st12 / st14 cast port", "0x491ce0 state past [0x892a48]", a.state);
    }
}

// ---- the key item (exe 0x5b12c0, static type 0x40; states [0x8d068c]: 0 0x5b12e0, 1 0x5b1350) --------------------
// Its fields: its spin (+0xc), the message it hands its event (+0xd), the story flag it sets (+0xe), and its class
// block's touch box (the hit list's result and the previous x / z)
struct KeyItemFields {
    explicit KeyItemFields(const StaticActor& actor) : KeyItemFields(const_cast<StaticActor&>(actor)) {}
    explicit KeyItemFields(StaticActor& actor) : spins(actor.params[0]), message(actor.params[1]), flag(actor.params, 2), touchBox(actor.hitBox) {}
    uint8_t& spins;
    FieldAlias<int8_t, uint8_t> message;
    BytesField<uint16_t, 4> flag;
    HitBoxHead& touchBox;
};
const DataTable<InteractionBoxRecord> keyItemUseBox{TableId::MandaRuinScriptsKeyItemUseBox};
constexpr uint16_t kSmallKeyItemFlag = 0x3e9; // the item drawn at half scale (with variant 1)
constexpr int16_t kSmallKeyItemScale = 0x80;
constexpr uint32_t kKeyItemTouched = 0x10000; // the touch box's result: the player touched it
constexpr uint32_t kKeyItemEvent = 0x20022;   // event mode 2 (the item get), its flags
constexpr uint32_t kKeyItemShape = 0x600019, kKeyItemAttack = 0x100000;
void keyItem(StaticActor& a) {
    KeyItemFields f(a);
    if (a.state == 0) { // exe 0x5b12e0
        if (!bindActorModel(a, 0)) {
            freeActor(a); // exe 0x5e1540
            return;
        }
        if (f.flag == kSmallKeyItemFlag && a.variant == 1) {
            a.scale.z = kSmallKeyItemScale;
            a.scale.y = kSmallKeyItemScale;
            a.scale.x = kSmallKeyItemScale;
        }
        a.state = 1;
        a.flags |= kActorDrawn;
        a.subState = 0;
        f.touchBox.result = 0;
        if (a.variant == 0) {
            a.interaction = 0;
        } else {
            a.interactionBox.pointAt(keyItemUseBox.stateAddress());
            a.interaction = kInteractTalk;
        }
        return;
    }
    if (a.state != 1) notPortedAt(kRuinArea, "0x5b12c0 state", a.state);
    bool take = false;
    if (a.variant == 0) {
        if (f.touchBox.result & kKeyItemTouched) {
            musicFadeOut(kMusicFadeSkip);
            take = true;
        }
    } else {
        take = player().talksTo(a) && eventState().active == 0; // exe 0x573fa0
    }
    if (take) {
        eventStartAtActor(a, kKeyItemEvent, 0, uint16_t(int16_t(f.message))); // exe 0x543af0
        setStoryFlag(f.flag);
        freeActor(a); // exe 0x5afdd0 = 0x5e1540
        return;
    }
    if (f.spins != 0) a.rotation.y += 0x20u;
    f.touchBox.previousX = a.prevPos.x.whole;
    f.touchBox.previousZ = a.prevPos.z.whole;
    f.touchBox.result = 0;
    const int16_t pt[3] = {a.pos.x.whole, a.pos.y.whole, a.pos.z.whole};
    hitListSubmit(pt, f.touchBox, (uint32_t(a.slot) << 24) | kKeyItemShape, kKeyItemAttack);
}

} // namespace

bool mandaRuinScriptPorted(RoutineId handler) {
    switch (handler) {
    case RoutineId::MandaRuinScript9:
    case RoutineId::MandaRuinScriptA:
    case RoutineId::MandaRuinScriptB:
    case RoutineId::MandaRuinScriptC:
    case RoutineId::MandaRuinScript1a:
    case RoutineId::MandaRuinScriptE:
    case RoutineId::MandaRuinScriptF: return true;
    default: return false;
    }
}
bool mandaRuinScriptTick(const PlayerEnv&, RoutineId handler) {
    switch (handler) {
    case RoutineId::MandaRuinScript9: runScript<&script9SetUp, &script9Steps, &script9WrapUp>("script 9 state"); return true;
    case RoutineId::MandaRuinScriptA: runScript<&scriptASetUp, &scriptASteps, &scriptAWrapUp>("script 10 state"); return true;
    case RoutineId::MandaRuinScriptB: runScript<&scriptBSetUp, &scriptBSteps, &scriptBWrapUp>("script 0xb state"); return true;
    case RoutineId::MandaRuinScriptC: runScript<&scriptCSetUp, &scriptCSteps, &scriptCWrapUp>("script 0xc state"); return true;
    case RoutineId::MandaRuinScript1a: runScript<&script1aSetUp, &script1aSteps, &script1aWrapUp>("script 0x1a state"); return true;
    case RoutineId::MandaRuinScriptE: runScript<&scriptESetUp, &scriptESteps, &scriptEWrapUp>("script 0xe state"); return true;
    case RoutineId::MandaRuinScriptF: runScript<&scriptFSetUp, &scriptFSteps, &scriptFWrapUp>("script 0xf state"); return true;
    default: return false;
    }
}

void mandaRuinGroundedFigureUpdate(const PlayerEnv& env, uint32_t actor) { groundedFigure(env, animatedActorAt(actor)); }
void mandaRuinBurningWreckUpdate(const PlayerEnv& env, uint32_t actor) { burningWreck(env, animatedActorAt(actor)); }
void mandaRuinKeyItemUpdate(const PlayerEnv&, uint32_t actor) { keyItem(staticActorAt(actor)); }

} // namespace od::port
