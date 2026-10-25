// Script 0x13 and the ST1C room case (see st1c_script13.hpp). The script's states are exe 0x51bb50 / 0x51bc90 /
// 0x51bcf0 (table 0x8a1e08); its camera script 0x8a0e1c runs the 43 steps of the list 0x8a1bb8 (ids 0..9: the scenes),
// each step dispatched by its routine. Step ticks in the routines are the cutscene's timeline.
#include "content/forbidden_island/st1c_script13.hpp"

#include "engine/routine_ids.hpp"
#include "game/loaded_data.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actor_sweeps.hpp"
#include "game/audio_logic.hpp"
#include "game/display.hpp"
#include "game/fade_state.hpp"
#include "game/game_state.hpp"
#include "game/message.hpp"
#include "game/player/player_input.hpp"
#include "game/player/player_script_control.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/room_scripts.hpp"
#include "game/script_engine.hpp"
#include "game/script_state.hpp"
#include "game/stage_files.hpp"
#include "game/stage_state.hpp"
#include "game/story.hpp"
#include "game/transition.hpp"
#include "game/transition_state.hpp"
#include "game/dat_names.hpp"

namespace od::port {

namespace {

constexpr uint8_t kSt1c = 0x1c;
constexpr uint8_t kScript13 = 0x13;
// the sweep of the script's start: every actor class but the player's and the group-0 effects
constexpr uint32_t kSweepAllButEffects0 = kSweepActors & ~kSweepEffectsGroup0;
// the first script actor (exe data: its spawn record) and the records of the cast swaps
constexpr TableId kSecondCastRecords[3] = {TableId::St1cScript13SecondCastRecord0, TableId::St1cScript13SecondCastRecord1, TableId::St1cScript13SecondCastRecord2};
constexpr TableId kThirdCastRecords[3] = {TableId::St1cScript13ThirdCastRecord0, TableId::St1cScript13ThirdCastRecord1, TableId::St1cScript13ThirdCastRecord2};
// the voices and the DATs loaded for them
constexpr uint32_t kVoiceArrival = 0xd, kVoiceSecond = 0x11, kVoiceThird = 0x19;
constexpr DatFile kDatSecondVoice{"ST1C01.DAT"}, kDatThirdVoice{"ST1C02.DAT"}, kDatNextStage{"PL00T2.DAT"};
constexpr uint32_t kVoiceFadeStep = 8;
// the script's effect (pool 0xa0 stage type 0x19)
constexpr uint8_t kEffectType = 0x19;
constexpr uint8_t kStepEffectSubtype = 5; // the steps' (exe 0x51d0d0)
constexpr uint8_t kGlowSubtype = 3, kGlowVariant = 0x80; // step 3:2's glow
constexpr uint8_t kEffectEnding = 2;      // the effect's state that ends it (step 7:1)
// the end: the request to ST3E sub 3 at (0x240, -1, -0x90) facing a quarter turn
constexpr uint8_t kSt3e = 0x3e, kSt3eArrivalRoom = 3;
constexpr int16_t kArrivalX = 0x240, kArrivalY = -1, kArrivalZ = -0x90, kArrivalYaw = 0x400;
constexpr uint8_t kArrivalFadeIn = 2;
// the player's ground pose (steps 2:2 and 6:0)
constexpr int32_t kGroundPoseX = 0x2000000, kGroundPoseZ = 0x15800000;

// the fades of the steps
constexpr uint32_t kFadeLongIn = 1, kFadeIn = 2, kFadeOutSlow = 0xc, kFadeOut = 0x12, kFadeToWhite = 0x14, kFadeOutFast = 0x1a;

ScriptState& block() { return scriptState(); }
St1cScriptBytes& cues() { return st1cScriptBytes(); }
int stepTick() { return block().stepTick; }
// the step's own phase (+6), counted up by the routines
uint8_t phase() { return block().stepPhase; }
void nextPhase() { ++block().stepPhase; }

// exe 0x51cfd0(sub): the in-stage warp to ST1C's sub-room (no fades), the destination's place cleared
void warpToRoom(uint8_t room) {
    TransitionRequest& r = transitionRequest();
    r.keepPosition = 0;
    r.exitId = 0;
    r.exitByte1 = 0;
    r.destination = RoomPlace{};
    r.state = kTransitionWarp;
    r.destStage = kSt1c;
    r.destRoom = room;
    r.fadeIn = kNoFade;
    r.fadeOut = kNoFade;
}
// exe 0x51d020(eyes keys, mouth keys): the next key of each list that is due (its tick reached) selected on the
// player's face tracks
void playerFaceKeys(uint32_t eyesKeys, uint32_t mouthKeys) {
    St1cScriptBytes& c = cues();
    const CueKey keys[2] = {dataRecord<CueKey>(eyesKeys + uint32_t(c.faceKeyCursors[0]) * kCueKeySize, CueKey::kBytes),
                            dataRecord<CueKey>(mouthKeys + uint32_t(c.faceKeyCursors[1]) * kCueKeySize, CueKey::kBytes)};
    for (size_t i = 0; i < 2; ++i) {
        const CueKey& k = keys[i];
        if (k.tick == kCueKeysEnd || int32_t(k.tick) > scriptState().part<CastTimelineTick>().stepTick) continue;
        block().faceTracks.track(FaceTrack(i)).select(k.sequence, k.key);
        ++c.faceKeyCursors[i];
    }
}
// exe 0x51d0d0(a, b, c, d): the steps' effect (pool 0xa0 type 0x19 sub 5, its parameters a..d) kept in the bytes
void spawnStepEffect(uint8_t p0, uint8_t p1, uint8_t p2, uint8_t p3) {
    const uint32_t slot = allocActorA0(kActorStageClass);
    cues().effect = slot != 0 ? &effectActorAt(slot) : nullptr;
    if (cues().effect == nullptr) return;
    EffectActor& e = effectActorAt(slot);
    e.flags = kActorLive;
    e.type = kEffectType;
    e.subtype = kStepEffectSubtype;
    e.setParamsWord(uint32_t(p0) | uint32_t(p1) << 8 | uint32_t(p2) << 16 | uint32_t(p3) << 24);
}
// the step's end: its phases cleared, the next step, a warp to a sub-room when asked
constexpr int kNoWarp = -1;
void endStep(int warpRoom) {
    scriptNextStep();
    if (warpRoom >= 0) warpToRoom(uint8_t(warpRoom));
}
// the end of a step whose exe clears its phases one byte at a time
void endStepByBytes() { scriptNextStepClearingPhases(); }
// the pose and step bytes (+0xc / +0xd) of script actor k, the parameters its class plays
ActorHeader& scriptActor(size_t k) { return (*block().actors[k]); }
constexpr int kKeep = -1;
void cueFirstActor(int pose, int step) {
    if (pose >= 0) scriptActor(0).params[0] = uint8_t(pose);
    if (step >= 0) scriptActor(0).params[1] = uint8_t(step);
}
// the plain waits: phase 0 clears the face-key cursors (the first actor cued first), then after `ticks` the step's
// end (and a warp). sameTick: the exe tests the tick in the same call as phase 0 (else from the next call on)
void waitStep(int ticks, bool sameTick, int warpRoom = kNoWarp, int pose = kKeep, int step = kKeep) {
    if (phase() == 0) {
        if (pose >= 0 || step >= 0) cueFirstActor(pose, step);
        cues().clearFaceKeyCursors();
        nextPhase();
        if (!sameTick) return;
    } else if (phase() != 1) {
        return;
    }
    if (ticks < stepTick()) endStep(warpRoom);
}
// the player shown at (0x200, -, 0x1580) facing yaw 0, its y = the ground's height (stored as the whole 16.16 value,
// as the transcription has it)
void playerGroundPose(const PlayerEnv& env) {
    Player& p = player();
    p.flags |= kActorLiveDrawn;
    p.pos.x = kGroundPoseX;
    p.pos.z = kGroundPoseZ;
    p.pos.y = actorGround(env, p);
    p.rotation.y = 0;
}
void showPlayer(bool on) { player().setDrawn(on); }
bool fadeIdle() { return fadeState().idle(); }
bool filesLoaded() { return (fileLoads().loading & kFileLoading) == 0; }
bool streamIdle() { return (fileLoads().loading & kStreamBusy) == 0; }
bool drawingBuffer0() { return display().drawBuffer == 0; }
void lockSkip(bool on) {
    if (on) block().flags |= kScriptUnskippable;
    else block().flags &= uint8_t(~kScriptUnskippable);
}

// ---- the steps ------------------------------------------------------------------------------------------------------
// 0:0 exe 0x51be40: the second and third actors cued at 0x40, the player's stage animation 1 at 0x60, the end at 0xa0
void stepOpeningCues() {
    switch (phase()) {
    case 0:
        cues().clearFaceKeyCursors();
        nextPhase();
        [[fallthrough]];
    case 1:
        if (0x40 < stepTick()) {
            scriptActor(2).setParamsWord(0x8030406);
            scriptActor(1).params[1] = 1;
            nextPhase();
        }
        break;
    case 2:
        if (0x60 < stepTick()) {
            playerStageAnim(1);
            nextPhase();
        }
        break;
    case 3:
        if (0xa0 < stepTick()) endStep(kNoWarp);
        break;
    default: break;
    }
    playerFaceKeys(stateAddress(TableId::St1cScript13OpeningEyesKeys), stateAddress(TableId::St1cScript13OpeningMouthKeys));
}
// 0:1 exe 0x51bef0
void stepOpeningWait() {
    waitStep(0xf4, true);
    playerFaceKeys(stateAddress(TableId::St1cScript13OpeningWaitEyesKeys), stateAddress(TableId::St1cScript13OpeningWaitMouthKeys));
}
// 0:2 exe 0x51bf50: the player's stage animation 2 at 0x1e; the end at 0xbf with the warp to sub 2
void stepLookUp() {
    const uint8_t c = phase();
    if (c == 0) {
        cues().clearFaceKeyCursors();
        nextPhase();
    } else if (c != 1) {
        if (c == 2 && stepTick() >= 0xbf) endStep(2);
        return;
    }
    if (0x1e < stepTick()) {
        playerStageAnim(2);
        nextPhase();
    }
}
// 0:3 exe 0x51bfc0: the player hidden, a fade at 200; once it is done from 0x114 the end with the warp to sub 5
void stepFadeToRoom5() {
    const uint8_t c = phase();
    if (c == 0) {
        cues().clearFaceKeyCursors();
        player().flags &= uint8_t(~kActorLiveDrawn);
        nextPhase();
    } else if (c != 1) {
        if (c == 2 && fadeIdle() && stepTick() >= 0x114) endStep(5);
        return;
    }
    if (200 < stepTick()) {
        scriptFade(kFadeOut, 0);
        nextPhase();
    }
}
// 1:0 exe 0x51c040: the long fade in; the end once it is done after 0x3f7
void stepLongFadeIn() {
    if (phase() == 0) {
        cues().clearFaceKeyCursors();
        scriptFade(kFadeLongIn, 0);
        nextPhase();
    } else if (phase() != 1) {
        return;
    }
    if (0x3f7 < stepTick() && fadeIdle()) endStep(kNoWarp);
}
// the voices after their DAT (steps 2:2 / 6:2): the voice playing faded, the DAT loaded once it is idle, then the
// voice once the files are in and the stream is idle on a frame of buffer 0
// 2:2 exe 0x51c130: the skip locked, the player on the ground; the second voice after 0x5a; the end once it plays
void stepSecondVoice(const PlayerEnv& env) {
    switch (phase()) {
    case 0:
        cues().clearFaceKeyCursors();
        lockSkip(true);
        playerGroundPose(env);
        voiceClipFadeOut(kVoiceFadeStep);
        nextPhase();
        [[fallthrough]];
    case 1:
        if (voiceClipIdle()) {
            datLoadRequest(kDatSecondVoice);
            nextPhase();
        }
        return;
    case 2:
        if (filesLoaded()) nextPhase();
        return;
    case 3:
        if (streamIdle() && drawingBuffer0() && 0x5a < stepTick()) {
            voiceClipPlay(kVoiceSecond);
            nextPhase();
        }
        return;
    case 4:
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
// 6:2 exe 0x51c960: the player hidden, the skip locked; the third voice; once it plays the end with the warp to sub 2
void stepThirdVoice() {
    switch (phase()) {
    case 0:
        cues().clearFaceKeyCursors();
        player().flags &= uint8_t(~kActorLiveDrawn);
        lockSkip(true);
        voiceClipFadeOut(kVoiceFadeStep);
        nextPhase();
        [[fallthrough]];
    case 1:
        if (voiceClipIdle()) {
            datLoadRequest(kDatThirdVoice);
            nextPhase();
        }
        return;
    case 2:
        if (filesLoaded()) nextPhase();
        return;
    case 3:
        if (streamIdle() && drawingBuffer0()) {
            voiceClipPlay(kVoiceThird);
            nextPhase();
        }
        return;
    case 4:
        if (voiceClipPlaying()) {
            endStepByBytes();
            warpToRoom(2);
        }
        return;
    default: return;
    }
}
// 3:0 exe 0x51c240: the player's stage animation 3, its animation paused; the end after 199
void stepPausedPose() {
    if (phase() == 0) {
        cues().clearFaceKeyCursors();
        playerStageAnim(3);
        player().animPaused = 1;
        nextPhase();
    } else if (phase() == 1 && 199 < stepTick()) {
        endStep(kNoWarp);
    }
}
// 3:1 exe 0x51c2a0: the skip locked and a fast fade out at 0xbd; the end once it is done
void stepFastFadeOut() {
    const uint8_t c = phase();
    if (c == 0) {
        cues().clearFaceKeyCursors();
        nextPhase();
    } else if (c != 1) {
        if (c == 2 && fadeIdle()) endStepByBytes();
        return;
    }
    if (0xbd < stepTick()) {
        lockSkip(true);
        scriptFade(kFadeOutFast, 0);
        nextPhase();
    }
}
// 3:2 exe 0x51c310: a slow fade, the player's animation running again, the first actor's step 4 and a glow (pool 0xa0
// type 0x19 sub 3); the skip back and the stage animation 4 once the fade is done from 0x78; the end after 0x9f
void stepGlow() {
    const uint8_t c = phase();
    if (c == 0) {
        cues().clearFaceKeyCursors();
        scriptFade(kFadeOutSlow, 0);
        player().animPaused = 0;
        cueFirstActor(kKeep, 4);
        const uint32_t slot = allocActorA0(kActorStageClass);
        if (slot != 0) {
            EffectActor& glow = effectActorAt(slot);
            glow.flags = kActorLive;
            glow.type = kEffectType;
            glow.subtype = kGlowSubtype;
            glow.variant = kGlowVariant;
            glow.loopFlags |= kActorLateLoop;
            glow.setParamsWord(0);
            glow.pos.x = 0x170;
            glow.pos.y = -0x2e9;
            glow.pos.z = 0x15b0;
        }
        nextPhase();
    } else if (c == 1) {
        if (fadeIdle() && stepTick() >= 0x78) {
            lockSkip(false);
            playerStageAnim(4);
            nextPhase();
        }
    } else if (c == 2 && 0x9f < stepTick()) {
        endStepByBytes();
    }
    playerFaceKeys(stateAddress(TableId::St1cScript13GlowEyesKeys), stateAddress(TableId::St1cScript13GlowMouthKeys));
}
// the cast swapped (4:0 exe 0x51c3f0 / 7:0 exe 0x51cab0): every actor but the player freed, the other type-1 buffer
// walked (the DAT the step before loaded), new script actors
void swapBank() {
    actorsSweep(kSweepActors, kSweepFree);
    stage().bankBuffer ^= 1u;
    stageBankWalk();
}
// 4:0 exe 0x51c3f0: the second cast, the player's stage animation 5; the end after 0xbd
void stepSecondCast() {
    if (phase() == 0) {
        cues().clearFaceKeyCursors();
        swapBank();
        playerStageAnim(5);
        scriptActorFreeRecord(stateAddress(TableId::St1cScript13FirstCastFreed));
        for (TableId r : kSecondCastRecords) scriptActorSpawnRecord(stateAddress(r));
        nextPhase();
    } else if (phase() == 1 && 0xbd < stepTick()) {
        endStep(kNoWarp);
    }
}
// 5:0 exe 0x51c690: the player hidden, the first actor's pose 10 / step 8 at 0x31; the end from 0xa0
void stepHiddenCue() {
    const uint8_t c = phase();
    if (c == 0) {
        cues().clearFaceKeyCursors();
        player().flags &= uint8_t(~kActorLiveDrawn);
        nextPhase();
    } else if (c != 1) {
        if (c == 2 && stepTick() >= 0xa0) endStep(kNoWarp);
        return;
    }
    if (0x31 < stepTick()) {
        cueFirstActor(10, 8);
        nextPhase();
    }
}
// 6:0 exe 0x51c800: the player on the ground, its stage animation 5; the end after 0x1fe
void stepGroundPose(const PlayerEnv& env) {
    if (phase() == 0) {
        cues().clearFaceKeyCursors();
        playerGroundPose(env);
        playerStageAnim(5);
        nextPhase();
    } else if (phase() != 1) {
        return;
    }
    if (0x1fe < stepTick()) endStep(kNoWarp);
}
// 6:1 exe 0x51c890: the first actor's pose 6, the player's stage animations 6 at 0x8b and 7 from 0xdc, the skip locked
// and a fade at 0x29d; the end once it is done
void stepTwoPoses() {
    switch (phase()) {
    case 0:
        cueFirstActor(6, kKeep);
        cues().clearFaceKeyCursors();
        nextPhase();
        [[fallthrough]];
    case 1:
        if (0x8b < stepTick()) {
            playerStageAnim(6);
            nextPhase();
        }
        return;
    case 2:
        if (stepTick() < 0xdc) return;
        playerStageAnim(7);
        nextPhase();
        return;
    case 3:
        if (0x29d < stepTick()) {
            lockSkip(true);
            scriptFade(kFadeOut, 0);
            nextPhase();
        }
        return;
    case 4:
        if (fadeIdle()) endStepByBytes();
        return;
    default: return;
    }
}
// 6:3 exe 0x51ca40: a fade in; the skip back once it is done; the end from 0x78 with the warp to sub 4
void stepFadeInToRoom4() {
    const uint8_t c = phase();
    if (c == 0) {
        scriptFade(kFadeIn, 0);
        cues().clearFaceKeyCursors();
        nextPhase();
    } else if (c != 1) {
        if (c == 2 && stepTick() >= 0x78) {
            endStepByBytes();
            warpToRoom(4);
        }
        return;
    }
    if (fadeIdle()) {
        lockSkip(false);
        nextPhase();
    }
}
// 7:0 exe 0x51cab0: the third cast and the steps' effect; the end after 0x251 with the warp to sub 3
void stepThirdCast() {
    if (phase() == 0) {
        cues().clearFaceKeyCursors();
        swapBank();
        for (TableId r : kThirdCastRecords) scriptActorSpawnRecord(stateAddress(r));
        spawnStepEffect(0x50, 0x46, 0, 2);
        nextPhase();
    } else if (phase() != 1) {
        return;
    }
    if (0x251 < stepTick()) endStep(3);
}
// 7:1 exe 0x51cb50: the steps' effect ends; the end after 0x77
void stepEffectEnds() {
    if (phase() == 0) {
        cues().clearFaceKeyCursors();
        cues().effect->state = kEffectEnding;
        nextPhase();
    } else if (phase() == 1 && 0x77 < stepTick()) {
        endStep(kNoWarp);
    }
}
// 8:0 exe 0x51cd30: the steps' effect again; the end after 0x111
void stepEffectAgain() {
    if (phase() == 0) {
        cues().clearFaceKeyCursors();
        nextPhase();
        spawnStepEffect(0x50, 0x46, 0, 2);
    } else if (phase() == 1 && 0x111 < stepTick()) {
        endStep(kNoWarp);
    }
}
// 9:2 exe 0x51cf10: the fade to white after 0x17b (the skip locked); once it is done every actor freed, then 0xc4
// ticks to the end
void stepFinalFade() {
    switch (phase()) {
    case 0:
        cues().clearFaceKeyCursors();
        nextPhase();
        return;
    case 1:
        if (0x17b < stepTick()) {
            scriptFade(kFadeToWhite, 0);
            lockSkip(true);
            nextPhase();
        }
        return;
    case 2:
        if (fadeIdle()) {
            cues().waitTicks = 0;
            block().stepPhase = 3;
            actorsSweep(kSweepActors, kSweepFree);
        }
        return;
    case 3: {
        const int32_t n = cues().waitTicks + 1;
        cues().waitTicks = n;
        if (0xc3 < n) endStepByBytes();
        return;
    }
    default: return;
    }
}

// the step routines of the list 0x8a1bb8 (by the named routine)
void runStep(const PlayerEnv& env, const RoutineRef& routine) {
    switch (routine.id()) {
    case RoutineId::St1cScript13OpeningCuesStep: stepOpeningCues(); return;
    case RoutineId::St1cScript13OpeningWaitStep: stepOpeningWait(); return;
    case RoutineId::St1cScript13LookUpStep: stepLookUp(); return;
    case RoutineId::St1cScript13FadeToRoom5Step: stepFadeToRoom5(); return;
    case RoutineId::St1cScript13LongFadeInStep: stepLongFadeIn(); return;
    case RoutineId::St1cScript13Step6: waitStep(0xca, true); return;
    case RoutineId::St1cScript13Step7: waitStep(0x166, true, 0); return;
    case RoutineId::St1cScript13SecondVoiceStep: stepSecondVoice(env); return;
    case RoutineId::St1cScript13PausedPoseStep: stepPausedPose(); return;
    case RoutineId::St1cScript13FastFadeOutStep: stepFastFadeOut(); return;
    case RoutineId::St1cScript13GlowStep: stepGlow(); return;
    case RoutineId::St1cScript13SecondCastStep: stepSecondCast(); return;
    case RoutineId::St1cScript13Step13: waitStep(99, true); return;
    case RoutineId::St1cScript13Step14: waitStep(99, true, kNoWarp, kKeep, 4); return;
    case RoutineId::St1cScript13Step15: waitStep(99, true, kNoWarp, kKeep, 8); return;
    case RoutineId::St1cScript13Step16: waitStep(0x31, true, kNoWarp, 10, 8); return;
    case RoutineId::St1cScript13Step17: waitStep(0x27, true, kNoWarp, 6, 4); return;
    case RoutineId::St1cScript13Step18: waitStep(0x67, true); return;
    case RoutineId::St1cScript13Step19: waitStep(199, true, 1); return;
    case RoutineId::St1cScript13HiddenCue: stepHiddenCue(); return;
    case RoutineId::St1cScript13Step21: waitStep(0x72a, true, kNoWarp, 6, 8); return;
    case RoutineId::St1cScript13Step22: waitStep(0x3b, true, kNoWarp, 10, 8); return;
    case RoutineId::St1cScript13Step23: waitStep(199, true, 0, 6, 4); return;
    case RoutineId::St1cScript13GroundPoseStep: stepGroundPose(env); return;
    case RoutineId::St1cScript13TwoPosesStep: stepTwoPoses(); return;
    case RoutineId::St1cScript13ThirdVoiceStep: stepThirdVoice(); return;
    case RoutineId::St1cScript13FadeInToRoom4Step: stepFadeInToRoom4(); return;
    case RoutineId::St1cScript13ThirdCastStep: stepThirdCast(); return;
    case RoutineId::St1cScript13EffectEndsStep: stepEffectEnds(); return;
    case RoutineId::St1cScript13Step30: waitStep(0x16d, false); return;
    case RoutineId::St1cScript13Step31: waitStep(0x158, false); return;
    case RoutineId::St1cScript13Step32: waitStep(0x6f, false); return;
    case RoutineId::St1cScript13Step33: waitStep(0x207, false); return;
    case RoutineId::St1cScript13Step34: waitStep(0x177, false); return;
    case RoutineId::St1cScript13Step35: waitStep(0x112, false, 4); return;
    case RoutineId::St1cScript13EffectAgainStep: stepEffectAgain(); return;
    case RoutineId::St1cScript13Step37: waitStep(0x24d, false); return;
    case RoutineId::St1cScript13Step38: waitStep(0x18c, false); return;
    case RoutineId::St1cScript13Step39: waitStep(0x1df, false); return;
    case RoutineId::St1cScript13Step40: waitStep(0x13f, false, 3); return;
    case RoutineId::St1cScript13Step41: waitStep(0x207, false); return;
    case RoutineId::St1cScript13Step42: waitStep(0x77, false); return;
    case RoutineId::St1cScript13FinalFadeStep: stepFinalFade(); return;
    default: notPortedAt(kStoryPort, "script 0x13 step", routine.pointerValue());
    }
}

// ---- the states -----------------------------------------------------------------------------------------------------
// exe 0x51bb50: the script's bytes cleared, the actors swept, the sequence started on the replay pad; once the player
// is script driven its stage animation 0, its face tracks, its motion cleared, the first actor; then the arrival voice
// once the stream is idle; state 1 once it plays
void setUp() {
    ScriptState& s = block();
    switch (s.subState) {
    case 0:
        s.clearScriptByteRun();
        actorsSweep(kSweepAllButEffects0, kSweepFree);
        lockSkip(true);
        scriptSequenceStart(stateAddress(TableId::St1cScript13CameraScript), stateAddress(TableId::St1cScript13Steps));
        scriptReplayPadOn();
        scriptSequenceTick();
        ++s.subState;
        return;
    case 1: {
        if (playerToScriptIfFree() != 0) return;
        playerStageAnim(0);
        Player& p = player();
        p.clearDirectionOffsetWords();
        s.faceTracks.init(stateAddress(TableId::St1cScript13FaceEyesTable), stateAddress(TableId::St1cScript13FaceMouthTable));
        s.faceTracks.first.select(0, 0);
        s.faceTracks.second.select(0, 0);
        p.velocity.clearWords();
        p.acceleration.clearWords();
        scriptActorSpawnRecord(stateAddress(TableId::St1cScript13FirstCastRecord));
        ++s.subState;
        return;
    }
    case 2:
        if (streamIdle() && drawingBuffer0() && fadeIdle()) {
            voiceClipPlay(kVoiceArrival);
            ++s.subState;
        }
        return;
    case 3:
        if (voiceClipPlaying()) {
            s.state = uint8_t(CutsceneState::Steps);
            s.subState = 0;
            s.stepPhase = 0;
            s.stepSubPhase = 0;
            lockSkip(false);
        }
        return;
    default: return;
    }
}
// exe 0x51bc90: the step's routine, the skip (taken on a frame of buffer 1: the skip's wipe, then state 2), the
// sequence and the player's face tracks
void steps(const PlayerEnv& env) {
    ScriptState& s = block();
    const ScriptStep step = scriptStepAt(s.steps.value());
    if (step.id != kScriptStepEnd) {
        runStep(env, stateRoutine(step.routine));
        if (scriptSkipRequested() == 0 || drawingBuffer0()) {
            scriptSequenceTick();
            faceTracksTick(s.faceTracks);
            return;
        }
        scriptFade(kScriptSkipWipe, 0);
    }
    s.enterState(CutsceneState::WrapUp);
    scriptSequenceTick();
    faceTracksTick(s.faceTracks);
}
// exe 0x51bcf0: the vibration stopped, the player idle, the actors swept, the windows closed, the voice faded; the DAT
// of ST3E once it is idle, then the request to ST3E sub 3; once it is taken the player back, the next story
void wrapUp() {
    ScriptState& s = block();
    switch (s.subState) {
    case 0:
        padVibrate(0, 0x1000000);
        playerScriptAnim(PlayerAnim::Idle, 0, true);
        actorsSweep(kSweepActors, kSweepFree);
        windowCloseAll(WindowClose::Force);
        if (voiceClipPlaying()) voiceClipFadeOut(kVoiceFadeStep);
        ++s.subState;
        [[fallthrough]];
    case 1:
        if (voiceClipIdle()) {
            datLoadRequest(kDatNextStage);
            ++s.subState;
        }
        return;
    case 2:
        if (filesLoaded()) {
            requestTransition(kTransitionAccepted, kSt3e, kSt3eArrivalRoom, kArrivalX, kArrivalY, kArrivalZ,
                              kArrivalYaw, kArrivalFadeIn, kNoFade);
            ++s.subState;
        }
        return;
    case 3:
        if (transitionRequest().state == kTransitionNone) {
            player().animPaused = 0;
            playerBackToPlay();
            scriptReplayPadOff();
            player().flags |= kActorLiveDrawn;
            ++gameState().nextStory; // exe 0x512800
            scriptEndBlock();
        }
        return;
    default: return;
    }
}
// exe 0x51bb10: the bytes for the cast (the step's tick, id and sub), then the state
void script13(const PlayerEnv& env) {
    ScriptState& s = block();
    St1cScriptBytes& c = cues();
    st1cScriptGlobals().bytesAddress = kScriptBytesAddress;
    scriptState().part<CastTimelineTick>().stepTick = s.stepTick;
    scriptState().part<CastTimelineStep>().stepId = s.stepId;
    scriptState().part<CastTimelineStep>().stepSub = s.stepSub;
    switch (s.cutsceneState()) { // [0x8a1e08]
    case CutsceneState::SetUp: setUp(); return;
    case CutsceneState::Steps: steps(env); return;
    case CutsceneState::WrapUp: wrapUp(); return;
    default: notPortedAt(kStoryPort, "script 0x13 state", s.state);
    }
}

} // namespace

bool st1cScriptPorted(RoutineId handler) { return handler == RoutineId::St1cScript13; }
bool st1cScriptTick(const PlayerEnv& env, RoutineId handler) {
    if (handler != RoutineId::St1cScript13) return false;
    script13(env);
    return true;
}

bool st1cRoomCaseKnown(uint8_t stage) { return stage == kSt1c; }
bool st1cRoomCase() {
    if (gameState().stage != kSt1c) return false;
    RoomScriptState& room = roomScriptState();
    if (gameState().room == 0 && room.handlerStep == 0) {
        scriptRun(kScript13);
        room.handlerStep = 1;
    }
    return true;
}

} // namespace od::port
