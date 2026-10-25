// ST4B's scripts 0x5a / 0x5b and their cast. Names are hypotheses from the observed behaviour. Render output (the
// shadow list exe 0x5f20f0, the white screen exe 0x5f30e0, the OT sprites of the menus) is left out as in the other
// cast ports unless stated.
//
// Scripts (id, handler, stage, trigger):
//   0x5a (0x5357a0)  ST4B sub 5 (case 0x58b320, flag 0x680): fade 0x12, the retreating boss 0x4bc9c0 + the falling
//                    piece 0x595e80, voice 0x1b; the RNG seeded 0x873ca9e6 for the scene and restored; record 0x8ab390
//   0x5b (0x535b00)  ST4B sub 5 (flag 0x681): the escaping boss 0x4bce70 / the still figure 0x48ade0, voice 0x1c,
//                    blasts (static type 7 variant 0x21), the white screen, fade 0x14; the request (state 2) to ST52
//                    sub 0
#include "content/late_game/st4b_scripts.hpp"
#include "content/late_game/st4b_scripts_state.hpp"

#include "engine/data_source.hpp"
#include "game/actor_animation.hpp"
#include "game/actor_model.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actor_sweeps.hpp"
#include "game/audio_logic.hpp"
#include "game/camera_state.hpp"
#include "game/display.hpp"
#include "game/explosions.hpp"
#include "game/fade_state.hpp"
#include "game/game_state.hpp"
#include "game/global_class_types.hpp"
#include "game/message.hpp"
#include "game/player/player_input.hpp"
#include "game/player/player_script_control.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/script_engine.hpp"
#include "game/script_state.hpp"
#include "game/spark_emitter.hpp"
#include "game/spawn.hpp"
#include "game/story.hpp"
#include "game/transition.hpp"
#include "game/transition_state.hpp"

using namespace od::port::st4b_scripts_state;

namespace od::port {

using namespace st4b_scripts_state;

namespace {

constexpr RoutineId kStepIdle = RoutineId::EmptyRoutine;
constexpr uint32_t kSweepAll = 0xffe;
constexpr uint8_t kSweepFree = 0x80;
constexpr uint8_t kSkipFade = 0x22, kDarkFade = 0x12, kFadeIn = 2;
constexpr uint32_t kSceneSeed = 0x873ca9e6u;
constexpr uint16_t kBlastSound = 0x8b;

St4bScriptFields& fields() {
    return scriptState().part<St4bScriptFields>();
}
int32_t tick() { return scriptState().stepTick; }
// the current step's sub (the cast follows it)
uint8_t currentStepSub() { return scriptStepAt(scriptState().steps.value()).sub; }
void nextPhase() { scriptState().subState += 1u; }
void nextStepPhase() { scriptState().stepPhase += 1u; }

// the player placed at the scene's start (the inlined copies of the state-0 routines): the move angles and the idle
// faces cleared, the position (16.16), the yaw
void playerAt(uint32_t x, uint32_t y, uint32_t z, uint16_t yaw) {
    Player& p = player();
    p.clearDirectionOffsetWords();
    p.idleFaces[0] = 0;
    p.idleFaces[1] = 0;
    p.pos.x = int32_t(x);
    p.pos.y = int32_t(y);
    p.pos.z = int32_t(z);
    p.rotation.y = int16_t(yaw);
}
// the voice-clip tail of the end states: the clip faded out (exe 0x601220(8)) while it plays
void voiceFadeIfPlaying() {
    if (voiceClipPlaying()) voiceClipFadeOut(8);
}

// ---- the steps runners (state 1 of the scripts) --------------------------------------------------------------------
// exe 0x525760 / 0x535c50: the step's routine (0x525760 calls it unchecked), a skip (start) -> fade 0x22; the list's
// end or a skip -> the wrap-up (state / sub-state bytes); the sequence tick
template <class F> void runStepsSkipping(F step) {
    ScriptState& s = scriptState();
    const ScriptStep current = scriptStepAt(s.steps.value());
    if (current.id != kScriptStepEnd) {
        if (const RoutineRef routine = stateRoutine(current.routine)) step(routine);
        if (scriptSkipRequested() == 0 || display().drawBuffer == 0) {
            scriptSequenceTick();
            return;
        }
        scriptFade(kSkipFade, 0);
    }
    s.enterStateKeepingPhases(uint8_t(CutsceneState::WrapUp));
    scriptSequenceTick();
}
// exe 0x5358e0: the step's routine; a skip -> fade 0x22, the wrap-up; the list's end -> the wrap-up at its phase 1 (no
// fade)
template <class F> void runStepsEndingAtPhase1(F step) {
    ScriptState& s = scriptState();
    const ScriptStep current = scriptStepAt(s.steps.value());
    if (current.id != kScriptStepEnd) {
        if (const RoutineRef routine = stateRoutine(current.routine)) step(routine);
        if (scriptSkipRequested() != 0 && display().drawBuffer != 0) {
            scriptFade(kSkipFade, 0);
            s.enterStateKeepingPhases(uint8_t(CutsceneState::WrapUp));
        }
    } else {
        s.state = uint8_t(CutsceneState::WrapUp);
        s.subState = 1;
    }
    scriptSequenceTick();
}
// the RNG's scene seed of scripts 0x5a / 0x5b: the RNG's next value kept (not taken), then the RNG = 0x873ca9e6; the
// end state restores the RNG = next(the kept value)
void rngSceneSeed() {
    fields().savedRng = rngNext(rngLastValue());
    rngSetState(kSceneSeed);
}
void rngSceneRestore() { rngSetState(rngNext(fields().savedRng)); }
// the openings of scripts 0x5a / 0x5b (0x5357c0 / 0x535b20): dark, unskippable, the replay pad; once dark the actors
// swept, the sequence, the RNG's scene seed, the player at (0, -0x200, -0x800) yaw 0x800; once he is free, the steps
template <class Started> void scriptOpening(uint32_t camera, uint32_t steps, bool closeWindow, Started started) {
    ScriptState& s = scriptState();
    switch (s.subState) {
    case 0:
        scriptFade(kDarkFade, 0);
        s.flags |= kScriptUnskippable;
        scriptReplayPadOn();
        nextPhase();
        return;
    case 1:
        if (fadeState().busy != 0) return;
        nextPhase();
        return;
    case 2:
        actorsSweep(kSweepAll, kSweepFree);
        if (closeWindow) windowClose(4, WindowClose::Force); // exe 0x5576b0
        scriptSequenceStart(camera, steps);
        rngSceneSeed();
        playerAt(0, 0xfe000000u, 0xf8000000u, 0x800);
        s.clearStepPhase();
        nextPhase();
        [[fallthrough]];
    case 3:
        if (playerToScriptIfFree() != 0) return;
        started();
        s.enterState(CutsceneState::Steps);
        playerScriptAnimId(0, 0, 1);
        return;
    default: return;
    }
}
// the voice step of both scripts (0x535a40 / 0x535db0): the animation, fade in, the voice; once it plays skippable
// again and `hold` ticks to the next step
void voiceStep(uint8_t anim, uint32_t voice, uint8_t hold) {
    ScriptState& s = scriptState();
    switch (s.stepPhase) {
    case 0:
        playerScriptAnimId(anim, 0, 1);
        scriptFade(kFadeIn, 0);
        nextStepPhase();
        return;
    case 1:
        if (fadeState().busy != 0) return;
        voiceClipPlay(voice);
        nextStepPhase();
        return;
    case 2:
        if (!voiceClipPlaying()) return;
        s.flags &= uint8_t(~kScriptUnskippable);
        nextStepPhase();
        s.stepSubPhase = hold;
        return;
    case 3:
        s.stepSubPhase -= 1u;
        if (s.stepSubPhase != 0) return;
        scriptSequenceSkip(1); // exe 0x5115c0
        return;
    default: return;
    }
}

// ==== script 0x5a (0x5357a0, states 0x8ab4c0 = 0x5357c0 / 0x5358e0 / 0x535940; camera 0x8ab3a4 (op 0x40: records
// 0x8ab368 the boss 0x4bc9c0, 0x8ab37c static 0x595e80 (op 0x41 frees it)), steps 0x8ab490 = 0x535a40, 0x52ef00 x 3,
// 0x535ae0) ======================================================================================================
// ST4B sub 5 (case 0x58b320) on flag 0x680. State 0: fade 0x12 (unskippable), the pad replay; once dark: actors swept,
// window 4 closed, the camera, the RNG scene seed, the player at (0, -0x200, -0x800) yaw 0x800, scripted (anim 0).
// Steps: 0x535a40 (anim 0x8b, fade 2, voice 0x1b; when it plays skippable and 0xf ticks to the next step), 3 empty
// steps, 0x535ae0 (the script camera's roll -0x30 at its first tick). End: vibration off, the voice faded; once idle (a
// skip fades 1 first) actors swept, record 0x8ab390, the player back at (0, -0x200, -0x800) yaw 0x800, the RNG restored,
// the block ended, the roll 0.
const DataTable<RoutineTable<3>> retreatStates{TableId::St4bScriptsRetreatStates};
constexpr RoutineId kRetreatVoice = RoutineId::St4bScriptsRetreatVoice,
                    kRetreatRoll = RoutineId::St4bScriptsRetreatRoll;
void retreatStep(const RoutineRef& routine) {
    switch (routine.id()) {
    case kRetreatVoice: voiceStep(0x8b, 0x1b, 0xf); return;
    case kStepIdle: return; // ret
    case kRetreatRoll:
        if (tick() == 0) scriptCamera().roll = 0xffffffd0u;
        return;
    default: notPortedAt(kStoryPort, "script 0x5a step", routine.pointerValue());
    }
}
// the end position of scripts 0x5a / 0x4a (0x535940 / 0x52fe90): on the ground, (0, -0x200, -0x800) yaw 0x800
void playerBackAtEnd() {
    Player& p = player();
    p.airborne = 0;
    p.pos.x = 0;
    p.pos.y = int32_t(0xfe000000u);
    p.pos.z = int32_t(0xf8000000u);
    p.rotation.y = 0x800;
}
void retreatScript() {
    ScriptState& s = scriptState();
    switch (retreatStates->idAt(s.state)) {
    case RoutineId::St4bRetreatOpening: scriptOpening(stateAddress(TableId::St4bScriptsRetreatCamera), stateAddress(TableId::St4bScriptsRetreatSteps), true, [] {}); return;
    case RoutineId::St4bRetreatSteps: runStepsEndingAtPhase1(retreatStep); return;
    case RoutineId::St4bRetreatEnd:
        switch (s.subState) {
        case 0:
            padVibrate(0, 0x1000000);
            voiceFadeIfPlaying();
            nextPhase();
            return;
        case 1:
            if (!voiceClipIdle() || fadeState().busy != 0) return;
            if (s.flags & kScriptSkipTaken) {
                scriptFade(1, 0);
                nextPhase();
                return;
            }
            nextPhase();
            return;
        case 2:
            actorsSweep(kSweepAll, kSweepFree);
            spawnRecords(stateAddress(TableId::St4bScriptsRetreatSpawns), 1); // exe 0x511090
            playerBackToPlay();        // exe 0x5723c0
            scriptReplayPadOff();
            playerBackAtEnd();
            rngSceneRestore();
            scriptEndBlock();
            scriptCamera().roll = 0;
            return;
        default: return;
        }
    default: notPortedAt(kStoryPort, "script 0x5a state", s.state);
    }
}

// ==== script 0x5b (0x535b00, states 0x8ab610 = 0x535b20 / 0x535c50 / 0x535ca0; camera 0x8ab4f8 (op 0x40: records
// 0x8ab4d0 the boss 0x4bce70, 0x8ab4e4 the still figure 0x48ade0), steps 0x8ab5e8 = 0x535db0, 0x52ef00, 0x535e50,
// 0x535e90) =========================================================================================================
// ST4B sub 5 on flag 0x681. State 0 as script 0x5a (no window close) with the sound fade 0x666 of the mask 1.
// Steps: 0x535db0 (anim 0, fade 2, voice 0x1c, 0x5a ticks), 0x535e50 (anim 0x8b, at 0x96 anim 0x8e), 0x535e90 (from
// tick 0xf a blast (static type 7 variant 0x21) on the ticks not multiple of 8; at 0x1e sound 0x8b (pan 0, atten
// 0x60) and fade 0x1c; the white screen (render); fade 0x14). End: vibration off, the voice faded; once idle the
// request (state 2, only the stage / sub / fade in 0xff written) to ST52 sub 0, actors 0x494 swept; once taken the
// player back, the RNG restored, the block ended.
const DataTable<RoutineTable<3>> escapeStates{TableId::St4bScriptsEscapeStates};
constexpr RoutineId kEscapeVoice = RoutineId::St4bScriptsEscapeVoice, kEscapeLook = RoutineId::St4bScriptsEscapeLook,
                    kEscapeBlasts = RoutineId::St4bScriptsEscapeBlasts;
enum class BlastPhase : uint8_t { WaitFlash = 0, Flash = 1, WaitWhite = 2, White = 3 };
// exe 0x535f40: a blast (static global type 7 variant 0x21) at (0, 0xf90, 0), size 0x100
void sceneBlast() {
    const uint32_t slot = allocActor60(0); // exe 0x5e1300
    if (slot == 0) return;
    StaticActor& blast = staticActorAt(slot);
    BlastParams params(actorHeaderAt(slot));
    blast.flags = kActorLive;
    blast.type = typeByte(GlobalStaticType::Blast);
    blast.variant = 0x21;
    blast.loopFlags |= kActorSweepGroup2;
    blast.pos.x.whole = 0;
    blast.pos.y.whole = 0xf90;
    blast.pos.z.whole = 0;
    params.size = 0x100;
    params.attack = 0;
}
void escapeStep(const RoutineRef& routine) {
    ScriptState& s = scriptState();
    const int32_t t = tick();
    switch (routine.id()) {
    case kEscapeVoice: voiceStep(0, 0x1c, 0x5a); return;
    case kStepIdle: return;
    case kEscapeLook:
        if (t == 0) playerScriptAnimId(0x8b, 0, 1);
        if (t == 0x96) playerScriptAnimId(0x8e, 0, 1);
        return;
    case kEscapeBlasts:
        if (t >= 0xf && (t & 7) != 0) sceneBlast();
        switch (BlastPhase(s.subState)) {
        case BlastPhase::WaitFlash:
            if (t != 0x1e) return;
            soundPlayPanned(kBlastSound, 0, 0x60); // exe 0x5d5340
            s.flags |= kScriptUnskippable;
            scriptFade(0x1c, 0);
            nextPhase();
            return;
        case BlastPhase::Flash: // exe 0x5f30e0 (the white screen) is render output
            if (fadeState().busy != 0) return;
            nextPhase();
            return;
        case BlastPhase::WaitWhite:
            if (fadeState().busy != 0) return;
            scriptFade(0x14, 0);
            nextPhase();
            return;
        default: return; // White: the white screen only
        }
    default: notPortedAt(kStoryPort, "script 0x5b step", routine.pointerValue());
    }
}
void escapeScript() {
    ScriptState& s = scriptState();
    switch (escapeStates->idAt(s.state)) {
    case RoutineId::St4bEscapeOpening:
        scriptOpening(stateAddress(TableId::St4bScriptsEscapeCamera), stateAddress(TableId::St4bScriptsEscapeSteps), false, [] { soundFadeOut(1, 0x666, 0); }); // 0x5d5780
        return;
    case RoutineId::St4bEscapeSteps: runStepsSkipping(escapeStep); return;
    case RoutineId::St4bEscapeEnd:
        switch (s.subState) {
        case 0:
            padVibrate(0, 0x1000000);
            voiceFadeIfPlaying();
            nextPhase();
            return;
        case 1:
            if (voiceClipIdle() && fadeState().busy == 0) nextPhase();
            return;
        case 2: {
            nextPhase();
            TransitionRequest& r = transitionRequest();
            r.state = kTransitionAccepted;
            r.keepPosition = 0;
            r.exitId = 0;
            r.exitByte1 = 0;
            r.destStage = 0x52;
            r.destRoom = 0;
            r.fadeIn = kNoFade;
            actorsSweep(0x494, kSweepFree);
            return;
        }
        case 3:
            if (transitionRequest().state != kTransitionNone) return;
            playerBackToPlay(); // exe 0x5723c0
            scriptReplayPadOff();
            player().setDrawn(true);
            rngSceneRestore();
            scriptEndBlock();
            return;
        default: return;
        }
    default: notPortedAt(kStoryPort, "script 0x5b state", s.state);
    }
}

// ==== the cast ====================================================================================================
// The class tails below run after a variant / state that may have freed the actor (no model in the room bank: the
// exe goes on with the freed actor's animation); the port stops there instead (the actor is gone either way).

// the volley shots the boss's sparks are (pool 0x40 stage type 0x30: content/late_game/st4b_fx)
constexpr uint8_t kSparkShotType = 0x30;

// ---- the retreating boss (exe 0x4bc9c0: ST4B pool 0x20 type 0x3f sub 2, script 0x5a record 0x8ab368) ---------------
// states [0x8949e4 + state * 4] = 0x4bc9e0 (bind) / 0x4bca50 (run) / 0x468fa0 (free). The run follows the script's
// step sub and its tick: step 1 ticks 0x60..0x7f the sparks of 0x4bcc40 (sound 0x2aa on the even ones), step 2 placed
// at (0, -0x800, 0x800) and the sparks of 0x4bcd70 up to tick 0x18, step 3 hidden, step 4 placed at (0, -0xe00,
// -0x2000) rising (vy 0x800 slowing by 0x34) with its pitch easing; the velocity, the animation (set / step), its
// root lift applied to the decoded frame's entries 5 / 6, the pose.
const DataTable<RoutineTable<3>> retreatingBossStates{TableId::St4bScriptsRetreatingBossStates};
// The boss's own bytes: the lift of its root (the hit points' word)
struct RetreatingBossFields {
    explicit RetreatingBossFields(const AnimatedActor& actor) : RetreatingBossFields(const_cast<AnimatedActor&>(actor)) {}
    explicit RetreatingBossFields(AnimatedActor& actor) : rootLift(actor.hitPoints) {}
    int16_t& rootLift;
};
// exe 0x4bcc40(actor, side): a spark (variant 1) at bone 4 (or 5) offset (-+0xc0, 0x18, -0xc0), pitch 0xe0 + rnd &
// 0xf, yaw +-(0x10 - rnd & 7), speed 0x1800
void handSpark(const AnimatedActor& actor, uint32_t side) {
    const uint32_t slot = allocActor40(kActorStageClass); // exe 0x5e1280
    if (slot == 0) return;
    uint32_t r = rngStep();
    int32_t d = 0x10 - int32_t(r & 7);
    int16_t out[3] = {0, 0, 0};
    if (side == 0) {
        const int16_t v[3] = {-0xc0, 0x18, -0xc0};
        bonePointOffset(actor, 4, v, out); // exe 0x5e1970
    } else {
        const int16_t v[3] = {0xc0, 0x18, -0xc0};
        bonePointOffset(actor, 5, v, out);
        d = -d;
    }
    ProjectileActor& shot = projectileActorAt(slot);
    shot.flags = kActorLive;
    shot.type = kSparkShotType;
    shot.variant = 1;
    shot.loopFlags |= kActorSweepGroup2;
    shot.pos.x.whole = out[0];
    shot.pos.y.whole = out[1];
    shot.pos.z.whole = out[2];
    r = rngStep();
    shot.rotation.x = int16_t(((r >> 16) & 0xf) + 0xe0);
    shot.rotation.y = int16_t(uint16_t(actor.rotation.y) + uint32_t(d));
    shot.rotation.z = 0;
    shot.velocity.x = 0;
    shot.velocity.y = 0;
    shot.velocity.z = 0x1800;
    shot.setParamsWord(0);
}
// exe 0x4bcd70(actor, side): the same spark (variant 0) at bone 4 / 5 `ahead` +-0xc0, pitch 8 - rnd & 0xf, speed 0x1000
void aheadSpark(const AnimatedActor& actor, uint32_t side) {
    const uint32_t slot = allocActor40(kActorStageClass);
    if (slot == 0) return;
    uint32_t r = rngStep();
    int32_t d = 0x10 - int32_t(r & 7);
    int16_t out[3] = {0, 0, 0};
    if (side == 0) {
        bonePointAhead(actor, 4, 0xc0, out); // exe 0x5e18d0
    } else {
        bonePointAhead(actor, 5, -0xc0, out);
        d = -d;
    }
    ProjectileActor& shot = projectileActorAt(slot);
    shot.flags = kActorLive;
    shot.type = kSparkShotType;
    shot.variant = 0;
    shot.loopFlags |= kActorSweepGroup2;
    shot.pos.x.whole = out[0];
    shot.pos.y.whole = out[1];
    shot.pos.z.whole = out[2];
    r = rngStep();
    shot.rotation.x = int16_t(8 - int32_t((r >> 16) & 0xf));
    shot.rotation.y = int16_t(uint16_t(actor.rotation.y) + uint32_t(d));
    shot.rotation.z = 0;
    shot.velocity.x = 0;
    shot.velocity.y = 0;
    shot.velocity.z = 0x1000;
    shot.setParamsWord(0);
}
void retreatingBoss(AnimatedActor& actor) {
    RetreatingBossFields boss(actor);
    switch (retreatingBossStates->idAt(actor.state)) {
    case RoutineId::St4bRetreatBind:
        if (!bindActorModel(actor, actor.variant)) { // exe 0x5e0bc0
            freeActor(actor);
            return;
        }
        actor.rotation.x = 0xc0;
        actor.velocity.y = 0x600;
        actor.flags |= kActorScenery;
        actor.rotation.z = 0;
        actor.velocity.x = 0;
        actor.velocity.z = 0x1000;
        actor.acceleration.clearWords();
        boss.rootLift = 0x200;
        actor.anim = 0;
        animSetPose(actor, 0, 0); // exe 0x5e1c30
        ++actor.state;
        return;
    case RoutineId::St4bRetreatRun: {
        actor.savePreviousPose();
        actor.prevAnim = actor.anim;
        const int32_t t = tick();
        switch (currentStepSub()) {
        case 1:
            if (t >= 0x60 && t < 0x80) {
                if (!(t & 1)) soundPlayAtPos(0x2aa, kSoundAtListener); // exe 0x5d5380
                handSpark(actor, uint32_t(t) & 1);
            }
            break;
        case 2:
            if (t == 0) break;
            if (t == 1) {
                actor.pos.x.whole = 0;
                actor.pos.y.whole = int16_t(0xf800);
                actor.pos.z.whole = 0x800;
                actor.rotation.x = 0;
                actor.velocity.y = 0;
                actor.velocity.z = 0;
            }
            if (t <= 0x18) aheadSpark(actor, uint32_t(t) & 1);
            break;
        case 3:
            if (t == 0) actor.setDrawn(false);
            break;
        case 4:
            if (t == 0) break;
            if (t == 1) {
                actor.pos.x.whole = 0;
                actor.pos.y.whole = int16_t(0xf200);
                actor.setDrawn(true);
                actor.pos.z.whole = int16_t(0xe000);
                actor.rotation.x = 0x200;
                actor.rotation.y = 0x800;
                actor.velocity.y = 0x800;
                actor.velocity.z = 0x800;
                actor.acceleration.y = int16_t(0xffcc);
                boss.rootLift = 0;
            }
            if (actor.velocity.y < 0) {
                actor.velocity.y = 0;
                actor.acceleration.y = 0;
            }
            if (actor.rotation.x > 0) actor.rotation.x = int16_t(uint16_t(actor.rotation.x) - 0x10u);
            break;
        default: break;
        }
        actor.velocity.x = int16_t(uint16_t(actor.velocity.x) + uint16_t(actor.acceleration.x));
        actor.velocity.z = int16_t(uint16_t(actor.velocity.z) + uint16_t(actor.acceleration.z));
        actor.velocity.y = int16_t(uint16_t(actor.velocity.y) + uint16_t(actor.acceleration.y));
        integrateVelocity(actor, 0, actor.velocity.y, -int(actor.velocity.z));
        if (actor.anim != actor.prevAnim) animSet(actor, actor.anim, 0); // exe 0x5e1c50
        else animStep(actor);                                           // exe 0x5e1ea0
        DecodedFrame& frame = decodedFrame();
        frame.entries[5].y = int16_t(uint16_t(frame.entries[5].y) - uint16_t(boss.rootLift));
        frame.entries[6].y = int16_t(uint16_t(boss.rootLift) + uint16_t(frame.entries[6].y));
        animPose(actor); // exe 0x5e2d90
        return;
    }
    case RoutineId::St4bCastFree: freeActor(actor); return;
    default: notPortedAt(kStoryPort, "0x4bc9c0 state (table 0x8949e4 has 3)", actor.state);
    }
}

// ---- the escaping boss (exe 0x4bce70: ST4B pool 0x20 type 0x3f sub 3, script 0x5b record 0x8ab4d0) ---------------
// states [0x8949f0] = 0x4bcea0 (bind, anim 7) / 0x4bcf00 (run) / 0x468fa0 (free), then the shadow. The run: in step 2
// anim 7 (forced) at tick 1, 0x15 at 0x65, 0x16 at 0xbf with sounds 0x2a9 / 0x2a8 and the speed 0x600; every 4th
// frame a trail static (exe 0x4bd030: stage type 3 variant 0x19, its spawn byte rnd & 0xf % 0xd, following the boss);
// speed += accel, moved back along its yaw, the animation.
const DataTable<RoutineTable<3>> escapingBossStates{TableId::St4bScriptsEscapingBossStates};
constexpr uint8_t kTrailVariant = 0x19; // a spark emitter (the stage's type 3, game/spark_emitter) on the boss's bone
// exe 0x4bd030
void bossTrail(AnimatedActor& actor, uint8_t spawnByte, uint8_t variant) {
    const uint32_t slot = allocActor60(kActorStageClass); // exe 0x5e12f0
    if (slot == 0) return;
    StaticActor& trail = staticActorAt(slot);
    SparkEmitterParams& params = classState<SparkEmitterParams>(staticActorAt(slot));
    trail.pos.x.whole = 0;
    trail.variant = variant;
    trail.spawnByte = spawnByte;
    trail.pos.y.whole = 0;
    trail.pos.z.whole = 0;
    trail.flags = kActorLive;
    trail.type = kSparkEmitterStageType;
    trail.loopFlags |= kActorSweepGroup2;
    trail.setParamsWord(0xa0000);
    params.parent = &actor;
}
void escapingBoss(AnimatedActor& actor) {
    switch (escapingBossStates->idAt(actor.state)) {
    case RoutineId::St4bEscapeBind:
        if (!bindActorModel(actor, actor.variant)) {
            freeActor(actor);
            return; // (the exe goes on to the shadow)
        }
        actor.flags |= kActorScenery;
        actor.rotation.x = 0;
        actor.rotation.z = 0;
        actor.velocity.assign(0, 0, 0);
        actor.acceleration.assign(0, 0, 0);
        actor.anim = 7;
        animSetPose(actor, 7, 0);
        ++actor.state;
        break;
    case RoutineId::St4bEscapeRun: {
        actor.savePreviousPose();
        actor.prevAnim = actor.anim;
        if (currentStepSub() == 2) {
            const int32_t t = tick();
            if (t == 1) {
                actor.anim = 7;
                actor.prevAnim = 0;
            }
            if (t == 0x65) actor.anim = 0x15;
            if (t == 0xbf) {
                soundPlayAtPos(0x2a9, kSoundAtListener);
                soundPlayAtPos(0x2a8, kSoundAtListener);
                actor.anim = 0x16;
                actor.velocity.z = 0x600;
            }
        }
        if (!(gameState().frame & 3)) {
            const uint32_t r = rngStep();
            bossTrail(actor, uint8_t((r & 0xf) % 0xd), kTrailVariant);
        }
        actor.velocity.z = int16_t(uint16_t(actor.velocity.z) + uint16_t(actor.acceleration.z));
        integrateVelocity(actor, 0, 0, -int(actor.velocity.z));
        if (actor.anim != actor.prevAnim) animSetPose(actor, actor.anim, 0);
        else animStepPose(actor);
        break;
    }
    case RoutineId::St4bCastFree: freeActor(actor); return;
    default: notPortedAt(kStoryPort, "0x4bce70 state (table 0x8949f0 has 3)", actor.state);
    }
    // exe 0x5f20f0: the shadow (render output)
}

// ---- the still figure (exe 0x48ade0: ST4B pool 0x20 type 0x27 sub 0, script 0x5b record 0x8ab4e4) ------------------
// states [0x892448] = 0x48ae70 (model variant 0, scenery / drawn, anim 0, scale 0x3000, motion / rotation cleared, its
// class word 0) / 0x52ef00 (still) / 0x468fa0 (free); the tint neutral every tick; the animation
const DataTable<RoutineTable<3>> stillFigureStates{TableId::St4bScriptsStillFigureStates};
constexpr uint8_t kActorFlag20 = 0x20; // set with the scenery bit (its reader is not traced)
void stillFigure(AnimatedActor& actor) {
    actor.prevAnim = actor.anim;
    actor.tint = kTintNeutral;
    actor.savePreviousPose();
    actor.savePreviousContact();
    switch (stillFigureStates->idAt(actor.state)) {
    case RoutineId::St4bStillBind:
        if (!bindActorModel(actor, 0)) { // exe 0x5e0ba0
            freeActor(actor);
            return;
        }
        actor.prevAnim = 0xff;
        actor.flags |= uint8_t(kActorScenery | kActorFlag20 | kActorDrawn);
        actor.anim = 0;
        actor.scale.z = 0x3000;
        actor.scale.y = 0x3000;
        actor.scale.x = 0x3000;
        actor.velocity.z = 0;
        actor.velocity.y = 0;
        actor.velocity.x = 0;
        actor.rotation.z = 0;
        actor.rotation.x = 0;
        classState<StillFigureFields>(actor).classWord = 0;
        actor.enterState(1);
        break;
    case RoutineId::EmptyRoutine: break;
    case RoutineId::St4bCastFree: freeActor(actor); return;
    default: notPortedAt(kStoryPort, "0x48ade0 state (table 0x892448 has 3)", actor.state);
    }
    if (actor.anim == actor.prevAnim) animStepPose(actor);
    else animSetPose(actor, actor.anim, 0);
}

} // namespace

bool st4bScriptPorted(uint8_t id) {
    switch (storyScriptHandler(id).id()) {
    case RoutineId::St4bRetreatScript: // script 0x5a
    case RoutineId::St4bEscapeScript:  // 0x5b
        return true;
    default: return false;
    }
}

bool st4bScriptTick(const PlayerEnv&, uint8_t id) {
    switch (storyScriptHandler(id).id()) {
    case RoutineId::St4bRetreatScript: retreatScript(); return true;
    case RoutineId::St4bEscapeScript: escapeScript(); return true;
    default: return false;
    }
}

bool st4bScriptWhiteScreen() {
    // script 0x5b's step 0x535e90 calls exe 0x5f30e0 from its phase 1 (when it advances) on: after the tick that is
    // the phase >= 2 while the block still runs that step
    const ScriptState& s = scriptState();
    if (!(s.flags & kScriptRunning) || storyScriptHandler(s.id).id() != RoutineId::St4bEscapeScript)
        return false;
    if (CutsceneState(s.state) != CutsceneState::Steps) return false;
    const ScriptStep current = scriptStepAt(s.steps.value());
    if (current.id == kScriptStepEnd ||
        !stateRoutine(current.routine).is(kEscapeBlasts))
        return false;
    return s.subState >= 2 && display().drawingOff == 0;
}

void bossRetreatCastUpdate(const PlayerEnv&, uint32_t actor) { retreatingBoss(animatedActorAt(actor)); }
void bossEscapeCastUpdate(const PlayerEnv&, uint32_t actor) { escapingBoss(animatedActorAt(actor)); }
void stillFigureCastUpdate(const PlayerEnv&, uint32_t actor) { stillFigure(animatedActorAt(actor)); }

} // namespace od::port
