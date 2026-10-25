// The pirate base's story-12 scripts (hypothesis names): script 0x5c (exe 0x535f80, steps 0x8ab820; ST3B sub 0, the
// player reaches the base's gate: the gate breaks open with debris, its guards come out, Megaman jumps back), 0x5d (exe
// 0x5366b0, steps 0x8abb78; ST3C sub 2 on arrival: the music fades, the bank's DAT loads, Megaman walks in and reacts;
// the end swaps the bank and brings the gold statue) and 0x5e (exe 0x536b60, steps 0x8abd10; ST3C sub 2 once the statue
// is beaten: the voice over the sequence; the end loads the next DAT, the story's flags). Each handler dispatches on the
// script's state: the set-up, the steps runner (the steps by their named routines), the wrap-up.
#include "content/saul_kada/pirate_base_scripts.hpp"
#include "content/saul_kada/pirate_base_scripts_state.hpp"

#include "content/saul_kada/st3b_script5c_cast.hpp"

#include "engine/routine_ids.hpp"
#include "game/loaded_data.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actor_sweeps.hpp"
#include "game/audio_logic.hpp"
#include "game/breakable_walls.hpp"
#include "game/display.hpp"
#include "game/fade_state.hpp"
#include "game/player/player_script_control.hpp"
#include "game/player/player_state.hpp"
#include "game/player/player_weapons.hpp"
#include "game/port_gap.hpp"
#include "game/script_engine.hpp"
#include "game/script_state.hpp"
#include "game/spawn.hpp"
#include "game/stage_files.hpp"
#include "game/stage_state.hpp"
#include "game/story.hpp"
#include "game/story_flags.hpp"
#include "game/transition.hpp"

using namespace od::port::pirate_base_scripts_state;

namespace od::port {

namespace {

constexpr RoutineId kStepIdle = RoutineId::EmptyRoutine;
enum class Opening : uint8_t { Start = 0, TakeOver = 1, Voice = 2, WaitVoice = 3 };
enum class WrapUp : uint8_t { FadeVoice = 0, WaitVoice = 1, Settle = 2, Ready = 3 };
constexpr uint8_t kSkipFade = 0x22, kSkippedFade = 1;
constexpr uint32_t kVoiceFade = 8;
constexpr uint32_t kMusicFades = 0xf, kArrivalMusic = 1;
constexpr int16_t kMusicFadeStep = 0x333;
BaseScriptFields& fields() {
    return scriptState().part<BaseScriptFields>();
}

int32_t tick() { return scriptState().stepTick; }
Actor& scriptActor() { return actorLayoutOf(*scriptState().actors[0]); }
void nextPhase() { scriptState().subState += 1u; }
void nextStepPhase() { scriptState().stepPhase += 1u; }
void playerAnim(uint8_t anim) { playerScriptAnimId(anim, 0, 1); } // exe 0x5723d0(anim, 0, 1)
void openWall(uint32_t column, uint32_t row) { switchGridCellVariant(column, row, 1); } // exe 0x58d5f0
// the player placed (his move angles and idle faces cleared, his position and yaw)
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
// the player back in play at the end, at his place
void playerBack(int32_t x, int32_t y, int32_t z, int16_t yaw) {
    Player& p = player();
    playerBackToPlay();   // exe 0x5723c0
    scriptReplayPadOff(); // exe 0x511890
    p.pos.x = x;
    p.setDrawn(true);
    p.pos.y = y;
    p.pos.z = z;
    p.rotation.y = yaw;
    p.airborne = 0;
}

// exe 0x536630(point, variant, row): a piece of the gate's debris at the point with the row's spin and velocity
// [0x8ab868 + row * 12]
struct DebrisRow : DataRecord {
    static constexpr uint32_t kBytes = 0xc;
    int16_t yaw = 0; // +0x0
    uint16_t spinX = 0; // +0x2
    uint16_t spinY = 0; // +0x4
    int16_t velocityX = 0; // +0x6
    int16_t velocityY = 0; // +0x8
    int16_t velocityZ = 0; // +0xa
    template <class R> static DebrisRow load(const R& r) {
        DebrisRow v;
        v.yaw = r.template read<int16_t>(0x0);
        v.spinX = r.template read<uint16_t>(0x2);
        v.spinY = r.template read<uint16_t>(0x4);
        v.velocityX = r.template read<int16_t>(0x6);
        v.velocityY = r.template read<int16_t>(0x8);
        v.velocityZ = r.template read<int16_t>(0xa);
        return v;
    }
};
void gateDebris(uint32_t point, uint8_t variant, uint32_t row) {
    const DebrisRow r = dataRecord<DebrisRow>(stateAddress(TableId::PirateBaseScriptsGateDebrisRows) + row * 12, DebrisRow::kBytes);
    const uint32_t slot = allocActorA0(kActorStageClass); // exe 0x5e13a0
    if (!slot) return;
    EffectActor& debris = effectActorAt(slot);
    GateDebrisFields& fields = classState<GateDebrisFields>(effectActorAt(slot));
    debris.variant = variant;
    debris.loopFlags |= kActorSweepGroup2 | kActorLateLoop;
    debris.flags = kActorLive;
    debris.type = kGateDebrisType;
    debris.setPositionWords(dataShortVec4(point));
    fields.spinX = r.spinX;
    fields.spinY = r.spinY;
    fields.angles.x = 0;
    fields.angles.z = 0;
    fields.angles.y = r.yaw;
    fields.velocity.x = r.velocityX;
    fields.velocity.y = r.velocityY;
    fields.velocity.z = r.velocityZ;
}
// the runner of scripts 0x5c / 0x5d / 0x5e (exe 0x5360b0 / 0x536790 / 0x536cb0): the step's routine, the skip (fade
// 0x22 -> the wrap-up); the list's end -> the wrap-up run at once
template <class Step, class End> void runSteps(Step step, End wrapUp) {
    ScriptState& s = scriptState();
    const ScriptStep current = scriptStepAt(s.steps.value());
    if (current.id == kScriptStepEnd) {
        s.enterState(CutsceneState::WrapUp);
        wrapUp();
        scriptSequenceTick();
        return;
    }
    if (const RoutineRef routine = stateRoutine(current.routine)) step(routine);
    if (scriptSkipRequested() && display().drawBuffer != 0) { // exe 0x511790
        scriptFade(kSkipFade, 0);
        s.enterState(CutsceneState::WrapUp);
    }
    scriptSequenceTick();
}
// the openings of scripts 0x5c / 0x5e (exe 0x535fbd / 0x536b9d): the actors swept, the sequence, the replay pad,
// unskippable, the player at (0x880, 0, 0x800) yaw 0x680 (0x5e: hidden, the music faded)
void baseOpening(uint32_t camera, uint32_t steps, bool hide) {
    ScriptState& s = scriptState();
    actorsSweep(kSweepActors, kSweepFree);
    scriptSequenceStart(camera, steps);
    scriptReplayPadOn();
    s.flags |= kScriptUnskippable;
    if (hide) player().setDrawn(false);
    placePlayer(0x8800000, 0, 0x8000000, 0x680);
    s.clearScriptByteRun(); // exe 0x40dc40
    if (hide) soundFadeOut(kMusicFades, kMusicFadeStep, 0);
    scriptSequenceTick();
}
// the wrap-ups' head (exe 0x536110 / 0x5367f0): the voice faded out, idle, one more tick; true once ready
bool wrapUpReady() {
    ScriptState& s = scriptState();
    switch (WrapUp(s.subState)) {
    case WrapUp::FadeVoice:
        if (voiceClipPlaying()) voiceClipFadeOut(kVoiceFade);
        nextPhase();
        [[fallthrough]];
    case WrapUp::WaitVoice:
        if (voiceClipIdle()) nextPhase();
        return false;
    case WrapUp::Settle: s.subState = uint8_t(WrapUp::Ready); return false;
    case WrapUp::Ready: return true;
    default: return false;
    }
}
// the openings' common voice wait: playing -> the steps (the opening noted, skippable again)
void startStepsOnVoice(uint8_t& opened) {
    if (!voiceClipPlaying()) return;
    opened = 1;
    scriptState().flags &= uint8_t(~kScriptUnskippable);
    scriptState().enterState(CutsceneState::Steps);
}

// ---- script 0x5c ---------------------------------------------------------------------------------------------------
constexpr RoutineId kGateLook = RoutineId::PirateBaseScriptsGateLook, kGateTurn = RoutineId::PirateBaseScriptsGateTurn,
                    kGateSweep = RoutineId::PirateBaseScriptsGateSweep,
                    kGateBreak = RoutineId::PirateBaseScriptsGateBreak,
                   kGateGuards = RoutineId::PirateBaseScriptsGateGuards;
// the gate's debris points (exe data, 8 bytes each)
void gateStep(const RoutineRef& routine) {
    ScriptState& s = scriptState();
    const int32_t t = tick();
    switch (routine.id()) {
    case kGateLook:
        if (t == 0) playerAnim(0x80);
        else if (t == 0x122) playerAnim(0x81);
        return;
    case kGateTurn:
        if (t == 0) {
            s.clearStepPhase();
            playerAnim(0x82);
        }
        return;
    case kGateSweep:
        if (t == 0) actorsSweep(0x700, kSweepFree);
        return;
    case kGateBreak:
        if (s.stepPhase == 0 && scriptActor().pos.z.whole > -0x900) {
            openWall(0x3b, 0x3b);
            openWall(0x3c, 0x3b);
            openWall(0x3d, 0x3b);
            gateDebris(stateAddress(TableId::PirateBaseScriptsDebrisTop), 1, 0);
            gateDebris(stateAddress(TableId::PirateBaseScriptsDebrisTop) + 8, 1, 1);
            openWall(0x3b, 0x3e);
            openWall(0x3d, 0x3e);
            nextStepPhase();
        }
        if (t == 0x27) s.clearStepPhase();
        return;
    case kGateGuards: {
        const int16_t z = scriptActor().pos.z.whole;
        switch (s.stepPhase) {
        case 0:
            if (!storyFlag(0x58f)) scriptActorSpawnRecord(stateAddress(TableId::PirateBaseScriptsGateFirstGuardRecord));
            if (!storyFlag(0x590)) scriptActorSpawnRecord(stateAddress(TableId::PirateBaseScriptsGateSecondGuardRecord));
            if (!storyFlag(0x591)) scriptActorSpawnRecord(stateAddress(TableId::PirateBaseScriptsGateThirdGuardRecord));
            nextStepPhase();
            [[fallthrough]];
        case 1:
            if (z > 0x300) {
                scriptActorSpawnRecord(stateAddress(TableId::PirateBaseScriptsGateLeaderRecord));
                nextStepPhase();
            }
            break;
        case 2:
            if (z > 0x500) {
                openWall(0x3b, 0x42);
                openWall(0x3d, 0x42);
                for (uint32_t k = 0; k < 6; ++k) gateDebris(stateAddress(TableId::PirateBaseScriptsDebrisSides) + k * 8, 2, k + 2);
                nextStepPhase();
            }
            break;
        case 3:
            if (z > 0x900) {
                scriptActorSpawnRecord(stateAddress(TableId::PirateBaseScriptsGateFirstExitRecord));
                scriptActorSpawnRecord(stateAddress(TableId::PirateBaseScriptsGateSecondExitRecord));
                nextStepPhase();
            }
            break;
        default: break;
        }
        Player& p = player();
        switch (s.stepSubPhase) {
        case 0:
            if (t == 0xa) {
                playerAnim(0x83);
                s.stepSubPhase += 1u;
            }
            return;
        case 1:
            if (t != 0x17) return;
            playerAnim(0x14);
            p.rotation.y = 0xc00;
            p.velocity.y = int16_t(0xfe60);
            s.stepSubPhase += 1u;
            [[fallthrough]];
        case 2: // the jump back
            p.velocity.y = int16_t(uint16_t(uint16_t(p.velocity.y) + 0x20u));
            moveAlongYaw(p.pos, p.rotation.y, 0, p.velocity.y, -0x240); // exe 0x5e36f0
            return;
        default: return;
        }
    }
    case kStepIdle: return;
    default: notPortedAt(kStoryPort, "script 0x5c step", routine.pointerValue());
    }
}
void gateWrapUp() { // exe 0x536110
    if (!wrapUpReady() || fadeState().busy != 0) return;
    if (scriptState().flags & kScriptSkipTaken) scriptFade(kSkippedFade, 0);
    actorsSweep(kSweepActors, kSweepFree);
    playerBack(int32_t(0xfb000000), int32_t(0xffff0000), int32_t(0xf7000000), 0x800);
    scriptEndBlock();
}
void gateScript() { // exe 0x535f80
    ScriptState& s = scriptState();
    switch (s.cutsceneState()) {
    case CutsceneState::SetUp: // exe 0x535fa0
        switch (Opening(s.subState)) {
        case Opening::Start:
            baseOpening(stateAddress(TableId::PirateBaseScriptsGateCamera), stateAddress(TableId::PirateBaseScriptsGateSteps), false);
            nextPhase();
            [[fallthrough]];
        case Opening::TakeOver:
            if (playerToScriptIfFree() == 0) { // exe 0x572300
                playerAnim(0x80);
                nextPhase();
            }
            return;
        case Opening::Voice:
            voiceClipPlay(8);
            nextPhase();
            return;
        case Opening::WaitVoice: startStepsOnVoice(fields().opened); return;
        default: return;
        }
    case CutsceneState::Steps: runSteps(gateStep, gateWrapUp); return;
    case CutsceneState::WrapUp: gateWrapUp(); return;
    default: notPortedAt(kStoryPort, "script 0x5c state", s.state);
    }
}

// ---- script 0x5d ---------------------------------------------------------------------------------------------------
constexpr RoutineId kArrivalLoad = RoutineId::PirateBaseScriptsArrivalLoad,
                    kArrivalReset = RoutineId::PirateBaseScriptsArrivalReset,
                    kArrivalWalk = RoutineId::PirateBaseScriptsArrivalWalk,
                   kArrivalPlace = RoutineId::PirateBaseScriptsArrivalPlace,
                   kArrivalReact = RoutineId::PirateBaseScriptsArrivalReact;
constexpr uint32_t kArrivalDat = 0x10e;
void arrivalStep(const RoutineRef& routine) {
    ScriptState& s = scriptState();
    const int32_t t = tick();
    Player& p = player();
    switch (routine.id()) {
    case kArrivalLoad:
        switch (s.stepPhase) {
        case 0:
            soundFadeOut(kArrivalMusic, kMusicFadeStep, 0); // exe 0x5d5780
            nextStepPhase();
            return;
        case 1:
            if (soundDone(kArrivalMusic)) { // exe 0x5d5900
                datLoadRequest(kArrivalDat); // exe 0x40efe0
                nextStepPhase();
            }
            return;
        case 2:
            if (!(fileLoads().loading & kFileLoading) && t >= 0x2d) {
                voiceClipPlay(0xd);
                nextStepPhase();
            }
            return;
        case 3:
            if (voiceClipPlaying()) {
                s.flags &= uint8_t(~kScriptUnskippable);
                fields().opened = 1;
                s.clearStepPhase();
                scriptSequenceSkip(1); // exe 0x5115c0
            }
            return;
        default: return;
        }
    case kArrivalReset:
        if (t == 1) s.clearStepPhase();
        return;
    case kArrivalWalk:
        switch (s.stepPhase) {
        case 0:
            p.setDrawn(true);
            playerAnim(0);
            nextStepPhase();
            [[fallthrough]];
        case 1:
            if (t == 8) {
                soundPlayAtPos(0xb8, kSoundAtListener); // exe 0x5d5380
                nextStepPhase();
            }
            return;
        case 2:
            if (t == 0x26) {
                soundPlayAtPos(0xb9, kSoundAtListener);
                nextStepPhase();
            }
            return;
        case 3:
            if (t == 0x3a) {
                playerAnim(1);
                nextStepPhase();
            }
            return;
        case 4:
            moveAlongYaw(p.pos, p.rotation.y, 0, 0, -0x200); // exe 0x5e36f0
            if (p.pos.z.whole < 0x780 && p.body.key.frame == 3) {
                playerAnim(0x81);
                nextStepPhase();
            }
            return;
        default: return;
        }
    case kArrivalPlace:
        if (t == 0) p.pos.z.whole = 0x680;
        return;
    case kArrivalReact:
        if (t == 0x1f4) playerAnim(0x82);
        else if (t == 0x23f) playerAnim(0x83);
        else if (t == 0x39d) playerAnim(0x84);
        return;
    case kStepIdle: return;
    default: notPortedAt(kStoryPort, "script 0x5d step", routine.pointerValue());
    }
}
void arrivalWrapUp() { // exe 0x5367f0
    if (!wrapUpReady() || fadeState().busy != 0) return;
    if (scriptState().flags & kScriptSkipTaken) scriptFade(kSkippedFade, 0);
    actorsSweep(kSweepActors, kSweepFree);
    stage().bankBuffer ^= 1u;
    stageBankWalk();              // exe 0x5f3200
    spawnRecordTracked(stateAddress(TableId::PirateBaseScriptsArrivalBossRecord)); // exe 0x5110e0: the gold statue's boss
    spawnRecordTracked(stateAddress(TableId::PirateBaseScriptsArrivalSparklerRecord)); // the sparkler
    playerBack(0, int32_t(0xffff0000), 0xa000000, 0);
    scriptEndBlock();
    soundPlayFlatForced(0x32); // exe 0x5d5260
}
void arrivalScript() { // exe 0x5366b0
    ScriptState& s = scriptState();
    switch (s.cutsceneState()) {
    case CutsceneState::SetUp: // exe 0x5366d0
        switch (s.subState) {
        case 0:
            actorsSweep(kSweepActors, kSweepFree);
            scriptSequenceStart(stateAddress(TableId::PirateBaseScriptsArrivalCamera), stateAddress(TableId::PirateBaseScriptsArrivalSteps));
            scriptReplayPadOn();
            s.flags |= kScriptUnskippable;
            player().setDrawn(false);
            placePlayer(0, 0, 0xfc00000, 0);
            s.clearScriptByteRun();
            scriptSequenceTick();
            nextPhase();
            [[fallthrough]];
        case 1:
            if (playerToScriptIfFree() == 0) {
                s.enterState(CutsceneState::Steps);
                playerAnim(0);
            }
            return;
        default: return;
        }
    case CutsceneState::Steps: runSteps(arrivalStep, arrivalWrapUp); return;
    case CutsceneState::WrapUp: arrivalWrapUp(); return;
    default: notPortedAt(kStoryPort, "script 0x5d state", s.state);
    }
}

// ---- script 0x5e ---------------------------------------------------------------------------------------------------
constexpr RoutineId kVictoryFade = RoutineId::PirateBaseScriptsVictoryFade;
constexpr uint32_t kVictoryDat = 0x10b;
constexpr uint32_t kFlagFirstCleared = 0x7e5, kFlagLastCleared = 0x7ee;
enum class VictoryEnd : uint8_t { FadeVoice = 0, WaitVoice = 1, Load = 2, Finish = 3 };
void victoryStep(const RoutineRef& routine) {
    switch (routine.id()) {
    case kVictoryFade:
        if (tick() == 0xe5) scriptFade(0x12, 0);
        return;
    case kStepIdle: return;
    default: notPortedAt(kStoryPort, "script 0x5e step", routine.pointerValue());
    }
}
void victoryWrapUp() { // exe 0x536d10
    ScriptState& s = scriptState();
    switch (VictoryEnd(s.subState)) {
    case VictoryEnd::FadeVoice:
        if (voiceClipPlaying()) voiceClipFadeOut(kVoiceFade);
        nextPhase();
        [[fallthrough]];
    case VictoryEnd::WaitVoice:
        if (voiceClipIdle()) nextPhase();
        return;
    case VictoryEnd::Load:
        if (fadeState().busy != 0) return;
        actorsSweep(kSweepActors, kSweepFree);
        datLoadRequest(kVictoryDat);
        nextPhase();
        return;
    case VictoryEnd::Finish:
        if (fileLoads().loading & kFileLoading) return;
        stage().bankBuffer ^= 1u;
        stageBankWalk();
        scriptFade(kSkippedFade, 0);
        for (uint32_t f = kFlagFirstCleared; f <= kFlagLastCleared; ++f) clearStoryFlag(f);
        setStoryFlag(0x7f4);
        setStoryFlag(0x7f5);
        playerBack(0, int32_t(0xff000000), 0x3000000, 0);
        weaponMode(0); // exe 0x573570
        scriptEndBlock();
        soundPlayFlatForced(0x33);
        return;
    default: return;
    }
}
void victoryScript() { // exe 0x536b60
    ScriptState& s = scriptState();
    switch (s.cutsceneState()) {
    case CutsceneState::SetUp: // exe 0x536b80
        switch (Opening(s.subState)) {
        case Opening::Start:
            baseOpening(stateAddress(TableId::PirateBaseScriptsVictoryCamera), stateAddress(TableId::PirateBaseScriptsVictorySteps), true);
            nextPhase();
            [[fallthrough]];
        case Opening::TakeOver:
            if (playerToScriptIfFree() == 0) {
                playerAnim(0x80);
                scriptFade(0xc, 0);
                nextPhase();
            }
            return;
        case Opening::Voice:
            if (fadeState().busy == 0) {
                voiceClipPlay(0xe);
                nextPhase();
            }
            return;
        case Opening::WaitVoice: startStepsOnVoice(scriptState().part<ScriptCastStart>().started); return;
        default: return;
        }
    case CutsceneState::Steps: runSteps(victoryStep, victoryWrapUp); return;
    case CutsceneState::WrapUp: victoryWrapUp(); return;
    default: notPortedAt(kStoryPort, "script 0x5e state", s.state);
    }
}

} // namespace

bool pirateBaseScriptsPorted(RoutineId handler) {
    return handler == RoutineId::PirateBaseGateScript || handler == RoutineId::PirateBaseArrivalScript || handler == RoutineId::PirateBaseVictoryScript;
}
bool pirateBaseScriptsTick(const PlayerEnv&, RoutineId handler) {
    if (!pirateBaseScriptsPorted(handler)) return false;
    switch (handler) {
    case RoutineId::PirateBaseGateScript: gateScript(); break;
    case RoutineId::PirateBaseArrivalScript: arrivalScript(); break;
    case RoutineId::PirateBaseVictoryScript: victoryScript(); break;
    default: break;
    }
    return true;
}

} // namespace od::port
