// ST4C (Elysium): scripts 0x6b / 0x6c, their casts and the classes they spawn. See content/late_game/st4c_story.hpp.
//
// Script 0x6b (0x53a4a0, states 0x8ad354 = 0x53a4c0 / 0x53a5e0 / 0x53a640; camera 0x8ad0d8, steps 0x8ad2e0 =
// 0x53a790 / 0x53a840 / 0x53a860 / 0x53a9a0 / 0x53a9f0): the boss intro, run by the ST4C sub 4 handler 0x58b480 on
// flag 0x680. Saves the RNG (its own bytes) and seeds it 0x873ca9e6, fades the music (0x5d5780(0xf, 0x222, 0)), the
// player scripted, voice 0, then the camera; the camera's op 0x40 spawns the intro cast 0x5086c0 (record 0x8ad0b0).
// The steps pose the player (0x80..0x87) and its face; step 2 plays voice 1; step 4 fades 0x12 at tick 0x280. End
// (0x53a640): actors swept (0x494), the player put back at (0, 0, -0x500) yaw 0x800, fade 1, the boss record 0x8ad0c4
// tracked (exe 0x5110e0), sound 0x4e, flag 0x710, the cell segment of the room to variant 1, the RNG restored.
// Script 0x6c (0x53aa60, states 0x8ad580 = 0x53aa80 / 0x53a5e0 / 0x53aba0, then 0x53ae20 every tick; camera
// 0x8ad39c, steps 0x8ad540 = 0x53ad00 / 0x53ad80): the boss's end, run by the handler on flag 0x681. The RNG saved in
// the stage's block and seeded 0x873ca9e4, voice 2. Step 0 spawns the defeated boss 0x508cd0 (record 0x8ad360) for
// 1000 ticks; step 1 flashes the screen white (exe 0x5f3150, 8 ticks from tick 0xf5), shakes at 0xfa and from tick
// 0x10e the rumble 0x53ae20 grows (camera shake, pad vibration, a white tint ramp) until the end at tick 400. End:
// unless skipped, fade 0x1a + flag 0x5b0; the request to ST5C sub 0 at z -0x80 (fade in 0xff when flag 0x5b0, else 2).
//
// Render output left out: the shadow 0x5f2220 of the intro cast (and its ground probe), the ribbon 0x5bdd00 of the
// trail 0x5bda80 (POLY_G4 pairs in the OT; its draw-mode word is still written), the visibility projection 0x5f2dd0 /
// 0x40c6e0 at the top of 0x5bfa00 (result unused). Drawn through the side channels: the screen tints (dc::screenTint),
// the 0x5bfa00 rays (dc::otLineG2, ends as camera-input points; OTZ = the near end's depth >> 2 as exe 0x40d010 after
// the RTPT 0x40c930), the draw-command records (dc::emit). Names are hypotheses from the observed behaviour.
#include "content/late_game/st4c_story.hpp"
#include "content/late_game/st4c_story_state.hpp"

#include <algorithm>
#include <cstdlib>

#include "engine/data_source.hpp"
#include "game/loaded_data.hpp"
#include "game/actor_animation.hpp"
#include "game/actor_model.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actor_sweeps.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/breakable_walls.hpp"
#include "game/camera.hpp"
#include "game/cutscene_actor.hpp"
#include "game/display.hpp"
#include "game/draw_commands.hpp"
#include "game/explosions.hpp"
#include "game/fade_state.hpp"
#include "game/field_alias.hpp"
#include "game/vectors.hpp"
#include "game/game_state.hpp"
#include "game/global_class_types.hpp"
#include "game/keyframe_tracks.hpp"
#include "game/matrix.hpp"
#include "game/player/player_input.hpp"
#include "game/player/player_script_control.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/projection.hpp"
#include "game/projection_state.hpp"
#include "game/random.hpp"
#include "game/minimap.hpp"
#include "game/script_engine.hpp"
#include "game/script_state.hpp"
#include "game/spawn.hpp"
#include "game/stage_files.hpp"
#include "game/stage_state.hpp"
#include "game/story.hpp"
#include "game/story_flags.hpp"
#include "game/transition.hpp"
#include "game/transition_state.hpp"

using namespace od::port::st4c_story_state;

namespace od::port {

using namespace st4c_story_state;

namespace {

int16_t t16(int64_t v) { return int16_t(uint16_t(uint64_t(v))); }
int sh() { return stage().coordinateShift & 0x1f; }

// ---- script helpers ------------------------------------------------------------------------------------------------
constexpr uint32_t kSceneSeedIntro = 0x873ca9e6u, kSceneSeedOutro = 0x873ca9e4u;
constexpr uint8_t kSkipFade = 0x22;

St4cScriptFields& fields() {
    return scriptState().part<St4cScriptFields>();
}
int32_t tick() { return scriptState().stepTick; }
uint8_t stepSub() { return scriptState().stepSub; }
KeyframeTracks& faces() { return scriptState().faceTracks; }
void nextPhase() { scriptState().subState += 1u; }
void nextStepPhase() { scriptState().stepPhase += 1u; }
bool fileLoading() { return (fileLoads().loading & kFileLoading) != 0; }
bool streamBusy() { return (fileLoads().loading & kStreamBusy) != 0; }

void anim(uint8_t a) { playerScriptAnimId(a, 0, 1); } // exe 0x5723d0(a, 0, 1)
void playerPos(uint32_t x, uint32_t y, uint32_t z, uint16_t yaw) {
    Player& p = player();
    p.pos.x = int32_t(x);
    p.pos.y = int32_t(y);
    p.pos.z = int32_t(z);
    p.rotation.y = int16_t(yaw);
}
void skip() { scriptSequenceSkip(1); } // exe 0x5115c0(1)
void faceValues(uint8_t eyes, uint8_t mouth) {
    faceTrackStop(faces(), FaceTrack::Eyes, eyes);   // exe 0x5e35f0
    faceTrackStop(faces(), FaceTrack::Mouth, mouth); // exe 0x5e3610
}

// exe 0x53a5e0 (state 1 of both scripts): the current step's routine, a skip (start) fades 0x22 and ends the run (the
// wrap-up); then the sequence tick and the player's face tracks (exe 0x5e3410)
template <class F> void runSteps(F step) {
    ScriptState& s = scriptState();
    const ScriptStep current = scriptStepAt(s.steps.value());
    if (current.id != kScriptStepEnd) {
        if (const RoutineRef routine = stateRoutine(current.routine)) step(routine);
        if (scriptSkipRequested() == 0 || display().drawBuffer == 0) {
            scriptSequenceTick(); // exe 0x5114d0
            faceTracksTick(faces());
            return;
        }
        scriptFade(kSkipFade, 0); // exe 0x53f6b0
    }
    s.enterState(CutsceneState::WrapUp);
    scriptSequenceTick();
    faceTracksTick(faces());
}

// ---- script 0x6b ---------------------------------------------------------------------------------------------------
void introStep(const RoutineRef& routine) {
    ScriptState& s = scriptState();
    const int32_t t = tick();
    switch (routine.id()) {
    case RoutineId::St4cIntroStep1: // step 0
        if (t == 0) {
            anim(0x80);
            playerPos(0, 0xffc00000u, 0xf5e00000u, 0);
        } else if (t == 0x14) {
            anim(0x81);
            faces().first.select(0, 0); // exe 0x5e33d0
        } else if (t == 0x208) {
            anim(0x82);
            faceValues(0x14, 0x14);
        }
        return;
    case RoutineId::St4cIntroStep2: // step 1
        if (t == 0) anim(0x83);
        return;
    case RoutineId::St4cIntroStep3:
        // step 2: voice 1 after the current one fades (from tick 0x28, not before tick 0x78)
        if (t == 0) {
            s.clearStepPhase();
            playerPos(0, 0, 0xfb000000u, 0x800);
            anim(0x86);
            faceValues(0x1d, 0x1d);
        } else if (t == 10) {
            faces().first.select(1, 0);
        }
        switch (s.stepPhase) {
        case 0:
            if (t != 0x28) return;
            if (voiceClipPlaying()) voiceClipFadeOut(8); // exe 0x601220
            s.flags |= kScriptUnskippable;
            nextStepPhase();
            return;
        case 1:
            if (voiceClipIdle()) nextStepPhase();
            return;
        case 2:
            if (!fileLoading()) nextStepPhase();
            return;
        case 3:
            if (streamBusy() || t < 0x78) return;
            voiceClipPlay(1); // exe 0x601050
            nextStepPhase();
            return;
        case 4:
            if (!voiceClipPlaying()) return;
            s.clearStepPhaseBytes();
            s.flags &= uint8_t(~kScriptUnskippable);
            skip();
            return;
        default: return;
        }
    case RoutineId::St4cIntroStep4: // step 3
        if (t == 0) {
            anim(0x86);
            faceValues(0x20, 0x21);
        } else if (t == 0x168) {
            anim(0x87);
        }
        return;
    case RoutineId::St4cIntroStep5: // step 4: fade 0x12 at tick 0x280, then the wrap-up
        if (s.stepPhase == 0) {
            if (t == 0) {
                faceValues(0x1d, 0x20);
            } else if (t == 0x280) {
                scriptFade(0x12, 0);
                s.flags |= kScriptUnskippable;
                nextStepPhase();
            }
        } else if (s.stepPhase == 1 && fadeState().busy == 0) {
            s.enterState(CutsceneState::WrapUp);
        }
        return;
    default: notPortedAt(kStoryPort, "script 0x6b step", routine.pointerValue());
    }
}
const DataTable<RoutineTable<3>> introStates{TableId::St4cStoryIntroStates};
void introScript() { // 0x53a4a0
    ScriptState& s = scriptState();
    switch (introStates->idAt(s.state)) {
    case RoutineId::St4cIntro0:
        switch (s.subState) {
        case 0:
            scriptReplayPadOn();         // exe 0x5117f0
            soundFadeOut(0xf, 0x222, 0); // 0x5d5780
            fields().clearedA = 0;
            fields().savedRng = rngNext(rngLastValue()); // the RNG saved (one step ahead), the cutscene's own seed
            rngSetState(kSceneSeedIntro);
            nextPhase();
            [[fallthrough]];
        case 1:
            if (playerToScriptIfFree() != 0) return; // exe 0x572300
            player().clearDirectionOffsetWords();
            faces().init(stateAddress(TableId::St4cStoryIntroEyes), stateAddress(TableId::St4cStoryIntroMouth)); // exe 0x5e33a0
            faceValues(0x1a, 0x1a);
            nextPhase();
            return;
        case 2:
            if (!fileLoading()) nextPhase();
            return;
        case 3:
            if (streamBusy() || !soundDone(0xf)) return; // 0x5d5900
            voiceClipPlay(0);
            nextPhase();
            return;
        case 4:
            if (!voiceClipPlaying()) return;
            scriptSequenceStart(stateAddress(TableId::St4cStoryIntroCamera), scriptStepList<TableId::St4cStoryIntroSteps>); // exe 0x511390
            s.enterState(CutsceneState::Steps);
            return;
        default: return;
        }
    case RoutineId::ScriptStepsState: runSteps(introStep); return;
    case RoutineId::St4cIntro2:
        switch (s.subState) {
        case 0:
            actorsSweep(0x494, 0x80); // exe 0x5e05d0
            anim(0);
            playerPos(0, 0, 0xfb000000u, 0x800);
            if (voiceClipPlaying()) voiceClipFadeOut(8);
            nextPhase();
            [[fallthrough]];
        case 1:
            if (voiceClipIdle() && fadeState().busy == 0) s.subState = 3;
            return;
        case 2:
            if (!fileLoading()) s.subState = 3;
            return;
        case 3:
            scriptFade(1, 0);
            spawnRecordTracked(stateAddress(TableId::St4cStoryIntroBossRecord)); // exe 0x5110e0: the boss 0x507280
            soundPlayFlatForced(0x4e);    // exe 0x5d5260
            scriptReplayPadOff();         // exe 0x511890
            playerBackToPlay();           // exe 0x5723c0
            minimap().flags &= uint8_t(~kMinimapAllowed);
            setStoryFlag(0x710);
            switchGridCellVariant(0x40, 0x3a, 1); // 0x58d5f0 on the cell (0x40, 0x3a)
            rngSetState(rngNext(fields().savedRng));
            scriptEndBlock(); // exe 0x5116a0
            return;
        default: return;
        }
    default: notPortedAt(kStoryPort, "script 0x6b state", s.state);
    }
}

// ---- script 0x6c ---------------------------------------------------------------------------------------------------
// the white tint of the step-1 flash / the rumble: arg = ((e & 0xff00 | 0x220000) << 8) | e >> 8 | e & 0xff00
uint32_t whiteTint(uint32_t e) {
    const uint32_t c = e & 0xff00u;
    return ((c | 0x220000u) << 8) | (e >> 8) | c;
}
void outroStep(const RoutineRef& routine) {
    ScriptState& s = scriptState();
    const int32_t t = tick();
    switch (routine.id()) {
    case RoutineId::St4cOutroStep1: // step 0: the defeated boss for 1000 ticks
        if (s.subState == 0) {
            actorsSweep(0xffe, 0x80);
            playerPos(0, 0, 0x200000, 0);
            anim(0x90);
            scriptActorSpawnRecord(stateAddress(TableId::St4cStoryOutroBossRecord)); // exe 0x511740: 0x508cd0
            s.subState = 1;
        }
        if (t != 1000) return;
        s.subState = 0;
        s.clearStepPhase();
        skip();
        return;
    case RoutineId::St4cOutroStep2:
        // step 1: the flash (8 ticks from 0xf5), a shake at 0xfa, the rumble from 0x10e; 400 ticks
        if (t == 0xf5) s.stepPhase = 8;
        else if (t == 0xfa) cameraShake(1, 0x300, 0); // exe 0x40ea80
        else if (t == 0x10e) fields().rumble = 1;
        if (s.stepPhase != 0) {
            const uint8_t v = uint8_t(s.stepPhase - 1u);
            s.stepPhase = v;
            dc::screenTint(whiteTint(((uint32_t(v) << 8) - v) << 5)); // exe 0x5f3150
        }
        if (t != 400) return;
        s.subState = 0;
        s.clearStepPhase();
        skip();
        return;
    default: notPortedAt(kStoryPort, "script 0x6c step", routine.pointerValue());
    }
}
// exe 0x53ae20: the rumble once its level runs: the level + 1 (min(level, 0x7f)): every other frame a camera shake 0x300
// + level * 2 (byte), pad vibration (0, (level * 2 | 0x100) << 8 | level + 0x50), a white tint
void rumble() {
    St4cScriptFields& f = fields();
    if (f.rumble == 0) return;
    const int16_t c = int16_t(f.rumble + 1);
    f.rumble = c;
    const uint32_t v = c > 0x7f ? 0x7fu : uint32_t(int32_t(c));
    if (gameState().frame & 1) cameraShake(1, uint32_t(uint8_t(v << 1)) + 0x300u, 0);
    padVibrate(0, ((uint32_t(uint8_t(v << 1)) | 0x100u) << 8) | ((v + 0x50u) & 0xffu)); // exe 0x55cd10
    dc::screenTint(whiteTint(((v << 8) - v) << 1));
}
const DataTable<RoutineTable<3>> outroStates{TableId::St4cStoryOutroStates};
void outroScript() { // 0x53aa60
    ScriptState& s = scriptState();
    switch (outroStates->idAt(s.state)) {
    case RoutineId::St4cOutro0:
        switch (s.subState) {
        case 0:
            scriptReplayPadOn();
            fields().clearedB = 0;
            nextPhase();
            stageBlock().part<StageBlockHead>().word = rngNext(rngLastValue()); // the RNG saved (one step ahead), the cutscene's own seed
            rngSetState(kSceneSeedOutro);
            soundFadeOut(0xf, 0x222, 0);
            [[fallthrough]];
        case 1:
            if (playerToScriptWhenStanding() != 0) break; // exe 0x572320
            player().clearDirectionOffsetWords();
            faces().init(stateAddress(TableId::St4cStoryOutroEyes), stateAddress(TableId::St4cStoryOutroMouth));
            faceValues(0x14, 0x14);
            nextPhase();
            break;
        case 2:
            if (!fileLoading()) nextPhase();
            break;
        case 3:
            if (streamBusy()) break;
            voiceClipPlay(2);
            nextPhase();
            break;
        case 4:
            if (!voiceClipPlaying()) break;
            scriptSequenceStart(stateAddress(TableId::St4cStoryOutroCamera), scriptStepList<TableId::St4cStoryOutroSteps>);
            s.enterState(CutsceneState::Steps);
            break;
        default: break;
        }
        break;
    case RoutineId::ScriptStepsState: runSteps(outroStep); break;
    case RoutineId::St4cOutro2:
        switch (s.subState) {
        case 0:
            padVibrate(0, 0x1000000);
            if (voiceClipPlaying()) voiceClipFadeOut(8);
            if (!(s.flags & kScriptSkipTaken)) { // not skipped
                scriptFade(0x1a, 0);
                setStoryFlag(0x5b0);
            }
            s.subState = 1;
            [[fallthrough]];
        case 1:
            if (voiceClipIdle()) nextPhase();
            break;
        case 2:
            if (fadeState().busy != 0) break;
            actorsSweep(0xffe, 0x80);
            nextPhase();
            requestTransition(kTransitionAccepted, 0x5c, 0, 0, 0, -0x80, 0, 2, kNoFade);
            transitionRequest().fadeIn = storyFlag(0x5b0) ? kNoFade : 2u;
            break;
        case 3:
            if (transitionRequest().state != kTransitionNone) break;
            rngSetState(rngNext(stageBlock().part<StageBlockHead>().word));
            anim(0);
            scriptReplayPadOff();
            playerBackToPlay(); // exe 0x5723c0
            scriptEndBlock();
            nextPhase();
            break;
        default: break;
        }
        break;
    default: notPortedAt(kStoryPort, "script 0x6c state", s.state);
    }
    rumble();
}

// ---- the casts (pool 0x20 type 0x7d subs 1 / 2) ------------------------------------------------------------------
// exe 0x5e1810: bone n's colour (the exe writes through it unchecked)
BoneColour boneColourOf(const AnimatedActor& actor, uint32_t n) {
    BoneBlock* block = boneBlockOf(actor, int(n >> 2));
    if (!block) notPortedAt(kStoryPort, "st4c cast: exe 0x5e1810 null, bone", n);
    return block->colour(n & 3);
}
void setAnimKeyed(AnimatedActor& a, CastFields& cast, uint8_t v) { requestAnim(a, cast.animKey, v); }

// The teleporter effects of ST40 (pool 0xa0 stage type 0x1e, content/late_game/st40_fx): the ring (variant 2) is
// released by its first parameter byte
constexpr uint8_t kTeleporterType = 0x1e, kTeleportRingVariant = 2;
// The intro cast's bytes past the shared cast layout: the teleport ring's slot
struct IntroCastFields {
    explicit IntroCastFields(const AnimatedActor& actor) : IntroCastFields(const_cast<AnimatedActor&>(actor)) {}
    explicit IntroCastFields(AnimatedActor& actor) : ringSlot(actor.townBirdHits) {}
    uint8_t& ringSlot;
};
enum class IntroCastState : uint8_t { Bind = 0, Rise = 1, Wait = 2, Talk = 3, End = 4 };
// exe 0x508800 (variant 0 of 0x5086c0), keyed on the script's step sub and tick k. State 0: bind (box 0x897f10, anim
// 0x20, tracks 0x897f60 / 0x897f98, cells 0) and a teleport ring (variant 2 at (0, -0x10, -0x200), its slot kept); 1
// (step 1): rises (velocity y -0x20) from tick 0x39 to 0x149; 2 (step 2): waits; 3 (step 3): the ring released at tick
// 0x1f, anims 0x21..0x24 and the texture tracks; 4 (step 4): anim 0x25 and tracks.
void introCastVariant(AnimatedActor& a, CastFields& cast, IntroCastFields& intro) {
    const int32_t k = tick();
    auto eyesCell = [&](uint32_t n) { textureTrackStop(a, cast.faceTracks, FaceTrack::Eyes, n); };
    auto mouthCell = [&](uint32_t n) { textureTrackStop(a, cast.faceTracks, FaceTrack::Mouth, n); };
    auto eyes = [&](uint8_t n) { cast.faceTracks.first.select(n, 0); };
    auto mouth = [&](uint8_t n) { cast.faceTracks.second.select(n, 0); };
    switch (IntroCastState(a.state)) {
    case IntroCastState::Bind: {
        if (!bindActorModel(a, 0)) { // 0x5e0ba0
            freeActor(a);            // exe 0x5e1490
            return;
        }
        a.collisionBox.pointAt(TableId::St4cStoryIntroCastBox);
        a.clearClassBlock(); // exe 0x40dc40
        a.enterState(uint8_t(IntroCastState::Rise));
        a.anim = 0x20;
        a.prevAnim = kNoAnimShown;
        clearVelocity(a);
        cast.animKey = 0;
        cast.faceTracks.init(stateAddress(TableId::St4cStoryIntroCastFirstTracks), stateAddress(TableId::St4cStoryIntroCastSecondTracks));
        eyesCell(0);  // exe 0x5e3630
        mouthCell(0); // exe 0x5e3660
        const uint32_t slot = allocActorA0(kActorStageClass); // exe 0x5e13a0
        if (slot != 0) {
            EffectActor& ring = effectActorAt(slot);
            ring.flags = kActorLiveDrawn;
            ring.type = kTeleporterType;
            ring.loopFlags |= kActorSweepGroup2 | kActorLateLoop;
            ring.variant = kTeleportRingVariant;
            ring.pos.x = 0;
            ring.pos.y = int16_t(0xfff0);
            ring.pos.z = int16_t(0xfe00);
            ring.setRecordYaw(0);
            ring.setParamsWord(0);
            intro.ringSlot = ring.slot;
        }
        [[fallthrough]];
    }
    case IntroCastState::Rise:
        if (stepSub() != 1) {
            a.enterState(uint8_t(IntroCastState::Wait));
            return;
        }
        if (k == 0x39) a.velocity.y = int16_t(0xffe0);
        else if (k == 0x149) a.velocity.y = 0;
        return;
    case IntroCastState::Wait:
        if (stepSub() != 2) a.enterState(uint8_t(IntroCastState::Talk));
        return;
    case IntroCastState::Talk:
        if (stepSub() != 3) {
            a.enterState(uint8_t(IntroCastState::End));
            return;
        }
        switch (k) {
        case 0xb5: case 0x137: case 0x191: case 0x1e1: case 0x295: case 0x321: case 0x3e9: case 0x4cf: case 0x5a1:
        case 0x5fb: mouthCell(4); return;
        case 0x88: case 0x173: case 0x1c3: case 0x21a: case 0x2bd: case 0x461: case 0x4ed: mouth(0); return;
        case 0xdd: eyes(0); mouth(0); return;
        case 0x1f: actorHeaderAt(actorBySlot(intro.ringSlot)).params[0] = 1; return; // exe 0x5e0fa0: the ring
        case 0x353: setAnimKeyed(a, cast, 0x21); mouth(0); return;
        case 0x41b: setAnimKeyed(a, cast, 0x22); eyes(1); mouth(0); return;
        case 0x443: setAnimKeyed(a, cast, 0x23); eyes(2); mouthCell(4); return;
        case 0x5b5: eyes(3); mouth(0); return;
        case 0x619: setAnimKeyed(a, cast, 0x24); eyes(4); return;
        default: return;
        }
    case IntroCastState::End:
        if (stepSub() != 4) return;
        switch (k) {
        case 1: case 0x1c3: mouth(0); return;
        case 0x10f: case 0x19b: case 0x1e1: case 0x23b: mouthCell(4); return;
        case 0x17d: eyes(5); mouth(0); return;
        case 0x213: setAnimKeyed(a, cast, 0x25); mouth(0); return;
        default: return;
        }
    default: return;
    }
}
// exe 0x5086c0: previous pose, the variant [0x897f08 + variant * 4], then (live) the move by the velocity, the ground
// (on-the-ground flag), the texture tracks, the animation; the shadow 0x5f2220 (its shadow flag) is render output
void introCast(const PlayerEnv& env, AnimatedActor& a) {
    CastFields& cast = classState<CastFields>(a);
    IntroCastFields intro(a);
    castSavePose(a);
    switch (a.variant) {
    case 0: introCastVariant(a, cast, intro); break;
    default: notPortedAt(kStoryPort, "0x5086c0 variant (table 0x897f08)", a.variant);
    }
    if (!(a.flags & kActorLive)) return;
    if (a.velocity.x != 0 || a.velocity.y != 0 || a.velocity.z != 0)
        integrateVelocity(a, a.velocity.x, a.velocity.y, a.velocity.z); // exe 0x5e3690
    if (cast.castFlags & kCastOnGround) a.pos.y.whole = int16_t(actorGround(env, a)); // exe 0x40f190
    textureTracksTick(a, cast.faceTracks);                                           // exe 0x5e34f0
    castAnimate(a, cast.animKey, cast.castFlags, kCastHeld);
}

// ---- the defeated boss (exe 0x508dd0, variant 0 of 0x508cd0) ------------------------------------------------------
enum class DefeatedState : uint8_t { Bind = 0, Glow = 1, Fall = 2, Gone = 3 };
// the defeated boss's bone fade thresholds (exe data, 11)
const DataTable<ScalarTable<int8_t, 11>> defeatFadeThresholds{TableId::St4cStoryDefeatFadeThresholds};
// a trail cue (exe data 0x898004, 4 bytes): its tick / 10 (0xff ends), the bone, the trail's two bytes
struct TrailCue : DataRecord {
    static constexpr uint32_t kBytes = 0x4;
    int8_t tickTenths = 0; // +0x0
    uint8_t bone = 0; // +0x1
    uint8_t life = 0; // +0x2
    uint8_t shape = 0; // +0x3
    template <class R> static TrailCue load(const R& r) {
        TrailCue v;
        v.tickTenths = r.template read<int8_t>(0x0);
        v.bone = r.template read<uint8_t>(0x1);
        v.life = r.template read<uint8_t>(0x2);
        v.shape = r.template read<uint8_t>(0x3);
        return v;
    }
};
// a burst cue (exe data 0x8980b4, 8 bytes): its tick, the bone, the burst's angle bases
struct BurstCue : DataRecord {
    static constexpr uint32_t kBytes = 0x8;
    int16_t tick = 0; // +0x0
    int16_t bone = 0; // +0x2
    uint16_t angleBase = 0; // +0x4
    uint16_t yawBase = 0; // +0x6
    template <class R> static BurstCue load(const R& r) {
        BurstCue v;
        v.tick = r.template read<int16_t>(0x0);
        v.bone = r.template read<int16_t>(0x2);
        v.angleBase = r.template read<uint16_t>(0x4);
        v.yawBase = r.template read<uint16_t>(0x6);
        return v;
    }
};
// the sparks' points (exe data 0x89804c: 16 s16 triples)
ShortVec3 sparkPoint(uint32_t bx) { return dataShortVec3(stateAddress(TableId::St4cStorySparkPoints) + uint32_t(int32_t(int16_t(bx))) * 2); }

// the lightning bolt's spawn bytes (pool 0xe0 stage type 0xf, content/nino/platform_boss): its length, width,
// amplitude, point and parent's slot (the header's state bytes: its start angle, angle step and fade)
struct LightningBoltParams {
    explicit LightningBoltParams(const ControllerActor& actor) : LightningBoltParams(const_cast<ControllerActor&>(actor)) {}
    explicit LightningBoltParams(ControllerActor& actor)
        : length(actor.params, 0), width(actor.params[2]), amplitude(actor.params[3]), pos(actor.recordPoint),
          parent(actor.recordPoint.spare) {}
    BytesField<uint16_t, 4> length;
    FieldAlias<int8_t, uint8_t> width;
    FieldAlias<int8_t, uint8_t> amplitude;
    ShortVec3& pos;
    int16_t& parent;
};
constexpr uint8_t kTrailType = 0x5f, kSparkType = 0x22, kRayBurstType = 0x63, kMotesType = 0x65, kBoltType = 0xf;
constexpr uint8_t kBoltOnAnimated = 3;

// exe 0x509250: the fade started -- drawn, the counter 0x30, drawn per part additive
void defeatFadeStart(AnimatedActor& a, DefeatedBossFields& boss) {
    a.setDrawn(true);
    boss.fadeCounter = 0x30;
    a.semiTransparency = semiTransparencyBits(1, 1); // exe 0x5e1a00(actor, 1, 1)
}
// exe 0x509280: the 11 bones fade out by their thresholds against the counter (grey max(c - th + 0x10, 0), neutral
// below); counter < 1: hidden, done (1)
bool defeatFadeOut(AnimatedActor& a, DefeatedBossFields& boss) {
    const int c = boss.fadeCounter;
    a.tint = 0;
    for (uint32_t n = 0; n <= 10; ++n) {
        const int th = defeatFadeThresholds[n];
        BoneColour colour = boneColourOf(a, n);
        if (c <= th) {
            int v = c - th + 0x10;
            if (v < 0) v = 0;
            colour = uint16_t(((v << 5 | v) << 5) | v);
        } else {
            colour = kTintNeutral;
        }
    }
    if (c - 1 < 0) {
        a.setDrawn(false);
        return true;
    }
    boss.fadeCounter = int16_t(c - 1);
    return false;
}
// exe 0x509310: at the cue's tick * 10 a trail on this actor (its slot, the cue's bone and bytes); the cue + 4 bytes
void defeatTrails(const AnimatedActor& a, DefeatedBossFields& boss) {
    const uint32_t i = uint32_t(int32_t(boss.trailCue));
    const TrailCue cue = dataRecord<TrailCue>(stateAddress(TableId::St4cStoryDefeatTrails) + i, TrailCue::kBytes);
    if (tick() != int32_t(cue.tickTenths) * 10) return;
    const uint32_t slot = allocActor60(kActorStageClass); // exe 0x5e12f0
    if (slot != 0) {
        StaticActor& trail = staticActorAt(slot);
        TrailParams params(staticActorAt(slot));
        trail.flags = kActorLive;
        trail.type = kTrailType;
        trail.loopFlags |= kActorLateLoop;
        params.parentSlot = int8_t(a.slot);
        params.bone = int8_t(cue.bone);
        params.lifeSixteenths = int8_t(cue.life);
        params.shape = int8_t(cue.shape);
    }
    boss.trailCue = int16_t(uint16_t(boss.trailCue) + 4u);
}
// exe 0x509390: 1 tick in 4 (RNG) a spark (pool 0xa0 stage type 0x22) at one of the spark points (8 random; from tick
// 0x226 the next 8), its size (0x20.. / 0x10.. random), its parent this actor; sound 0x2c8 (panned 0, 0x7b)
void defeatSparks(const AnimatedActor& a) {
    const uint32_t r = rngStep();
    if (r & 3) return;
    const uint32_t r2 = rngStep();
    const uint32_t r3 = rngStep();
    uint32_t idx, e;
    if (tick() >= 0x226) {
        idx = ((r2 >> 4) & 7) + 8;
        e = ((r3 >> 8) & 0x1f) + 0x10;
    } else {
        idx = (r2 >> 4) & 7;
        e = ((r3 >> 8) & 0x1f) + 0x20;
    }
    const uint32_t bx = idx * 3;
    const uint32_t slot = allocActorA0(kActorStageClass); // exe 0x5e13a0
    if (slot == 0) return;
    soundPlayPanned(0x2c8, 0, 0x7b); // exe 0x5d5340
    EffectActor& spark = effectActorAt(slot);
    SpriteParams params(effectActorAt(slot));
    params.size = int8_t(e);
    spark.flags = kActorLive;
    spark.type = kSparkType;
    spark.loopFlags |= kActorLateLoop;
    params.holdTicks = uint8_t(bx);
    params.paramB = 0;
    params.paramD = 0;
    const ShortVec3 point = sparkPoint(bx);
    spark.pos.x = point.x;
    spark.pos.y = point.y;
    spark.pos.z = point.z;
    params.parent = int16_t(a.slot);
}
// exe 0x5094b0: after tick 0x32 the pillars' counter + 1; at the counts 0 / 4 / .. / 0x1c a lightning bolt (on this
// actor) 0xc0 out at the angle ((n & ~3) + 1) * 0x80, RNG shape, and in any case a camera shake (1, 0x100, 0x80) + pad
// vibration (0x201, 0x205a0)
void defeatPillars(const AnimatedActor& a, DefeatedBossFields& boss) {
    if (tick() <= 0x32) return;
    boss.pillars += 1u;
    const uint32_t n = boss.pillars;
    if (n & 0xffe3u) return;
    const int angle = int((((n & 0xfffcu) + 1u) << 7) & 0xfffu);
    const uint32_t slot = allocActorE0(); // exe 0x5e1440
    if (slot != 0) {
        ControllerActor& bolt = controllerActorAt(slot);
        LightningBoltParams params(controllerActorAt(slot));
        bolt.flags = kActorLive;
        bolt.type = kBoltType;
        bolt.variant = kBoltOnAnimated;
        bolt.loopFlags |= kActorLateLoop;
        params.pos.y = int16_t(0xfc00);
        params.pos.x = int16_t(uint32_t((int32_t(fixedCos(angle)) * 0xc0) >> 12) + uint16_t(a.pos.x.whole));
        params.pos.z = int16_t(uint32_t((int32_t(fixedSin(angle)) * 0xc0) >> 12) + uint16_t(a.pos.z.whole));
        params.parent = int16_t(a.slot);
        params.length = 0x400;
        params.width = int8_t(0x40u - (rngStep() & 7));
        params.amplitude = int8_t(0x1au - (rngStep() & 7));
        bolt.subState = uint8_t(rngStep() & 0x3f);
        if (rngStep() & 1) bolt.step = uint8_t((rngStep() & 3) + 8);
        else bolt.step = uint8_t(0xf8u - (rngStep() & 3));
        bolt.stepCounter = uint8_t(0xc8u - uint8_t(boss.pillars));
    }
    cameraShake(1, 0x100, 0x80);
    padVibrate(0x201, 0x205a0);
}
// exe 0x509670: at the cue's tick a ray burst (variant 2) at the cue's bone (exe 0x5e1970, offset 0); the cue + 4
void defeatBursts(AnimatedActor& a, DefeatedBossFields& boss) {
    const BurstCue cue = dataRecord<BurstCue>(stateAddress(TableId::St4cStoryDefeatBursts) + uint32_t(int32_t(boss.burstCue)) * 2, BurstCue::kBytes);
    if (tick() != int32_t(cue.tick)) return;
    const int16_t v[3] = {0, 0, 0};
    int16_t o[3];
    bonePointOffset(a, uint32_t(int32_t(cue.bone)), v, o);
    const uint32_t slot = allocActor60(kActorStageClass);
    if (slot != 0) {
        StaticActor& burst = staticActorAt(slot);
        RayBurstParams params(staticActorAt(slot));
        burst.flags = kActorLive;
        burst.loopFlags |= kActorLateLoop;
        burst.type = kRayBurstType;
        burst.variant = 2;
        burst.pos.x = int32_t(uint32_t(int32_t(o[0])) << 16);
        burst.pos.y = int32_t(uint32_t(int32_t(o[1])) << 16);
        burst.pos.z = int32_t(uint32_t(int32_t(o[2])) << 16);
        params.angleBase = cue.angleBase;
        params.yawBase = cue.yawBase;
    }
    boss.burstCue = int16_t(uint16_t(boss.burstCue) + 4u);
}
// exe 0x509740: every 16th frame motes at this actor (variant (k >> 4) & 3 of kind 1, size k >> 4); from tick 0x104 a
// second (kind 2, phase 0x1e - (k >> 4))
void defeatMotes(const AnimatedActor& a) {
    if (gameState().frame & 0xf) return;
    const int32_t k = tick();
    const uint32_t slot = allocActor60(kActorStageClass);
    if (slot != 0) {
        StaticActor& motes = staticActorAt(slot);
        MoteParams params(staticActorAt(slot));
        motes.flags = kActorLive;
        motes.loopFlags |= kActorLateLoop;
        motes.type = kMotesType;
        motes.variant = uint8_t(uint32_t(k >> 4) & 3u);
        motes.spawnByte = 0;
        motes.pos = a.pos;
        params.phase = int8_t(uint32_t(k >> 4));
        params.speed = int8_t(uint32_t(k >> 4));
        params.mask = uint8_t(uint32_t(k >> 3));
        params.life = 0x20;
    }
    if (k < 0x104) return;
    const uint32_t second = allocActor60(kActorStageClass);
    if (second == 0) return;
    StaticActor& motes = staticActorAt(second);
    MoteParams params(staticActorAt(second));
    motes.flags = kActorLive;
    motes.loopFlags |= kActorLateLoop;
    motes.type = kMotesType;
    motes.variant = 0;
    motes.spawnByte = 1;
    motes.pos = a.pos;
    params.phase = int8_t(0x1eu - uint32_t(k >> 4));
    params.mask = 0x3f;
    params.life = 0x20;
    params.speed = int8_t(uint32_t((k >> 4) % 3));
}
// States: 0 bind (box 0x897fa8, anim 0x30, tracks 0x897fc4 / 0x897ff0, acceleration y 1); 1 (step 0): the animation
// held at its first frame (none shown) unless playing (set at the ticks below, cleared when the animation ends), the
// texture tracks by tick, sparks / trails / bursts; step 1 -> 2: anim 0x31 at tick 1 (the bob stops), a blast (static
// global 7 variant 3) at 0xfa, the bone fade-out from 0x15e (then state 3), pillars, motes. Every state but a stopped
// bob (its step): velocity y += acceleration y (reversed beyond +-8), y -= acceleration y.
void defeatedBossVariant(AnimatedActor& a, DefeatedBossFields& boss) {
    switch (DefeatedState(a.state)) {
    case DefeatedState::Bind:
        if (!bindActorModel(a, 0)) { // 0x5e0ba0
            freeActor(a);            // exe 0x5e1490
            return;
        }
        a.collisionBox.pointAt(TableId::St4cStoryDefeatedBossBox);
        a.clearClassBlock();
        a.anim = 0x30;
        a.enterState(uint8_t(DefeatedState::Glow));
        a.prevAnim = kNoAnimShown;
        a.velocity.z = 0;
        a.velocity.x = 0;
        a.acceleration.z = 0;
        a.acceleration.x = 0;
        a.acceleration.y = 1;
        a.velocity.y = 0;
        boss.animKey = 0;
        boss.castFlags &= uint8_t(~kCastShadow);
        boss.faces.init(stateAddress(TableId::St4cStoryDefeatedBossFirstTracks), stateAddress(TableId::St4cStoryDefeatedBossSecondTracks));
        textureTrackStop(a, boss.faces, FaceTrack::Eyes, 0);
        textureTrackStop(a, boss.faces, FaceTrack::Mouth, 0);
        [[fallthrough]];
    case DefeatedState::Glow: {
        if (boss.playing == 0) {
            boss.animKey = 0;
            a.prevAnim = kNoAnimShown;
        } else if (a.animEnded()) {
            boss.playing = 0;
        }
        switch (tick()) {
        case 0x64: case 0xa0: case 0x136: boss.faces.second.select(1, 0); break;
        case 0x96: boss.faces.first.select(1, 0); boss.faces.second.select(0, 0); break;
        case 0x12c: case 0x1d6: boss.faces.second.select(0, 0); break;
        case 0xb: case 0x1f: case 0x47: case 0x6f: case 0xa1: case 0xb5: case 0x137: case 0x209: case 0x263:
        case 0x28b: case 0x29f: case 0x32b: case 0x35d: case 0x399: case 0x3d5: boss.playing = 1; break;
        default: break;
        }
        defeatSparks(a);
        defeatTrails(a, boss);
        defeatBursts(a, boss);
        if (stepSub() == 1) a.state = uint8_t(DefeatedState::Fall);
        break;
    }
    case DefeatedState::Fall: {
        const int32_t k = tick();
        if (k == 1) {
            a.anim = 0x31;
            a.step = 1;
        } else if (k == 0xfa) {
            const uint32_t slot = allocActor60(0); // exe 0x5e1300: the blast (static global type 7)
            if (slot != 0) {
                StaticActor& blast = staticActorAt(slot);
                BlastParams params(actorHeaderAt(slot));
                blast.flags = kActorLive;
                blast.loopFlags |= kActorSweepGroup1;
                blast.type = typeByte(GlobalStaticType::Blast);
                blast.variant = 3;
                blast.pos = a.pos;
                params.size = 0x100;
                params.attack = 0;
            }
        } else if (k == 0x15e) {
            defeatFadeStart(a, boss);
            a.subState = 1;
        }
        defeatPillars(a, boss);
        defeatMotes(a);
        if (a.subState != 0 && defeatFadeOut(a, boss)) a.state = uint8_t(DefeatedState::Gone);
        break;
    }
    default: break;
    }
    if (a.step != 0) return;
    const int16_t v = a.velocity.y;
    if ((v < 0 ? -int32_t(v) : int32_t(v)) > 8) a.acceleration.y = int16_t(-int32_t(a.acceleration.y));
    const int16_t d = a.acceleration.y;
    a.pos.y.whole = int16_t(a.pos.y.whole - d);
    a.velocity.y = int16_t(v + d);
}
// exe 0x508cd0: previous pose, the variant [0x897fa0 + variant * 4], then (live) the planar velocities += their
// accelerations, the move (x / z), the ground (its on-the-ground flag), the texture tracks, the animation
void defeatedBoss(const PlayerEnv& env, AnimatedActor& a) {
    DefeatedBossFields& boss = classState<DefeatedBossFields>(a);
    castSavePose(a);
    switch (a.variant) {
    case 0: defeatedBossVariant(a, boss); break;
    default: notPortedAt(kStoryPort, "0x508cd0 variant (table 0x897fa0)", a.variant);
    }
    if (!(a.flags & kActorLive)) return;
    a.velocity.x = int16_t(a.velocity.x + a.acceleration.x);
    a.velocity.z = int16_t(a.velocity.z + a.acceleration.z);
    if (a.velocity.x != 0 || a.velocity.z != 0) integrateVelocity(a, a.velocity.x, 0, a.velocity.z);
    if (boss.castFlags & kCastOnGround) a.pos.y.whole = int16_t(actorGround(env, a));
    textureTracksTick(a, boss.faces);
    castAnimate(a, boss.animKey, boss.castFlags, kCastHeld);
}

// ---- the bone trail (static stage type 0x5f, exe 0x5bda80) -----------------------------------------------------------
// States [0x8d0dbc](actor, block +0x28): its length (up to 0x10), its head (0..0xf), the ribbon's shape (rise, angle),
// the draw mode, the colour step, the width; the points s16 x / y / z. Its spawn: the pool-0x20 parent's slot, its
// bone, its life / 16.
enum class TrailState : uint8_t { Start = 0, Follow = 1, Fade = 2 };
constexpr uint32_t kRibbonModeA = 0xe1000260, kRibbonModeB = 0xe1000220;
// the x half of exe 0x40c860 (0x40c200 + 0x40cb10) of a camera-space point, as game/projection's projectThrough (the
// focal, the centre x, the near plane; every x87 step rounded to a float, the result truncated)
int16_t screenX(int32_t x, int32_t z) {
    auto fl = [](double v) { return float(v); };
    const ProjectionState& proj = projection();
    const int32_t h = proj.focal, cx = proj.centreX, nearZ = proj.nearDepth;
    float fx;
    if (z >= nearZ) {
        fx = fl(double(fl(double(int32_t(uint32_t(h) * uint32_t(x))) / double(z))) + double(cx));
    } else if (z <= 0) {
        fx = fl(double(fl(double(x) + double(x))) + double(cx));
    } else {
        const float r = fl(double(z) / double(nearZ));
        float ax = fl(double(x) * double(r));
        ax = fl(double(ax) * double(h));
        ax = fl(double(ax) / double(z));
        fx = fl(double(ax) + double(cx));
    }
    return t16(int64_t(fx));
}
// one point of the ribbon 0x5bdd00: the camera gate -- view z in 0x30 .. the effects' far depth * 4, screen x + 0x160 /
// y + 0x88 within 0x400 -- then the half width: the view point (16-bit parts) through the identity matrix, its screen x
// and that of view x + width; w = |dx| << the stage shift pixels. Port: w as a camera-plane offset in camera-input
// units (w pixels = w * width / |dx| units at the point's depth; 0 when |dx| = 0)
struct RibbonPoint {
    int16_t v[3];
    float half;
};
bool ribbonPoint(const int16_t p[3], int32_t width, RibbonPoint& o) {
    const CameraPoint c = cameraPoint(p); // 0x40c3c0 / 0x40c3e0 (0x6d0014), 0x40c6a0, 0x40c800, 0x40caa0
    if (!(c.view[2] <= int32_t(uint32_t(stage().effectsFar) * 4) && c.view[2] >= 0x30)) return false;
    if (uint16_t(c.sx + 0x160) > 0x400 || uint16_t(c.sy + 0x88) > 0x400) return false; // 0x40c860 / 0x40cb10
    const int16_t ax = t16(c.view[0]), az = t16(c.view[2]);
    const int16_t bx = t16(int32_t(t16(width)) + ax);
    const int d = std::abs(int(screenX(bx, az)) - int(screenX(ax, az)));
    std::copy(p, p + 3, o.v);
    const int w = int(int16_t(uint16_t(uint32_t(d) << sh()))); // the exe's (s16) half width in pixels
    o.half = d != 0 ? float(w) * float(std::abs(int(bx) - int(ax))) / float(d) : 0.0f;
    return true;
}
// exe 0x5bdd00(static, block): the ribbon along the trail's points, newest (the head) to oldest. The head is the point
// itself; segment k (1 .. length - 1, the slot (head - k) & 0xf) moves by r = k - 1 (* 4 when the bone >= 9): x +=
// (cos(shape angle) * r) >> 8, y -= rise * r, z += (sin(..) * r) >> 8. Each point's half width comes from width w =
// the width << 4, + the width after each segment. While a point passes the gate (the walk stops at the first that does
// not), the segment from the previous point P to the new one N is a DR_MODE (the draw mode) then two POLY_G4 0x3a: (P -
// w, P, N - w, N) and (P, P + w, N, N + w) (screen x offsets), the centre line coloured k (P) / k - step (N), the edges
// black; k = step * length | 0x3a000000, - step per segment; linked at OT (N's view z >> 2, >= 1). Port: the corners
// as camera-input points + the half width in the camera plane (dc::otQuadG4); the GTE matrix loads (0xa59a28 /
// 0xa59a3c), the packet cursor and the OT links are presentation only and not written.
void trailRibbon(const StaticActor& a, const BoneTrailFields& trail) {
    const uint32_t step = trail.colourStep;
    const uint8_t abr = uint8_t((trail.drawMode >> 5) & 3);
    int32_t width = int32_t(uint32_t(trail.width) << 4);
    uint32_t idx = uint32_t(int32_t(trail.head));
    const int16_t head[3] = {trail.xs[idx], trail.ys[idx], trail.zs[idx]};
    RibbonPoint prev;
    if (!ribbonPoint(head, width, prev)) return;
    int32_t n = trail.length;
    uint32_t k = step * uint32_t(n) | 0x3a000000u, kn = k - step;
    while (n >= 2) {
        idx = (idx - 1) & 0xf;
        int32_t r = int32_t(trail.length) - n;
        if (trail.bone >= 9) r *= 4;
        const int angle = int(uint32_t(trail.shapeAngle) & 0xfff);
        const int16_t p[3] = {t16(int32_t(uint32_t(int32_t(fixedCos(angle)) * r) >> 8) + trail.xs[idx]),
                              t16(int32_t(trail.ys[idx]) - int32_t(trail.rise) * r),
                              t16(int32_t(uint32_t(int32_t(fixedSin(angle)) * r) >> 8) + trail.zs[idx])};
        RibbonPoint cur;
        if (!ribbonPoint(p, width, cur)) break;
        const int16_t q[4][3] = {{prev.v[0], prev.v[1], prev.v[2]}, {prev.v[0], prev.v[1], prev.v[2]},
                                 {cur.v[0], cur.v[1], cur.v[2]}, {cur.v[0], cur.v[1], cur.v[2]}};
        const uint32_t c1[4] = {0x3a000000u, k, 0x3a000000u, kn};
        const float o1[4][2] = {{-prev.half, 0.0f}, {0.0f, 0.0f}, {-cur.half, 0.0f}, {0.0f, 0.0f}};
        dc::otQuadG4(a.at, c1, abr, q, o1);
        const uint32_t c2[4] = {k, 0x3a000000u, kn, 0x3a000000u};
        const float o2[4][2] = {{0.0f, 0.0f}, {prev.half, 0.0f}, {0.0f, 0.0f}, {cur.half, 0.0f}};
        dc::otQuadG4(a.at, c2, abr, q, o2);
        prev = cur;
        width += int32_t(trail.width);
        --n;
        k -= step;
        kn -= step;
    }
}
// the ribbon drawn twice (draw modes abr 3, then abr 1)
void trailRibbons(const StaticActor& a, BoneTrailFields& trail) {
    trail.drawMode = kRibbonModeA;
    trailRibbon(a, trail);
    trail.drawMode = kRibbonModeB;
    trailRibbon(a, trail);
}
// the trail's parent (a pool-0x20 slot by the exe's arithmetic)
AnimatedActor& trailParent(const BoneTrailFields& trail) {
    return animatedActorAt(animatedPoolSlot(int32_t(trail.parentSlot)));
}
void trailBonePoint(const BoneTrailFields& trail, int16_t o[3]) {
    const int16_t z[3] = {0, 0, 0};
    bonePointOffset(trailParent(trail), uint32_t(int32_t(trail.bone)), z, o); // exe 0x5e1970
}
// exe 0x5bdaa0
void trailStart(StaticActor& a, BoneTrailFields& trail) {
    trail.head = 0;
    trail.length = 0;
    trail.rise = int16_t((rngStep() & 0xf) + 0x20);
    const uint32_t r = rngStep();
    trail.shapeAngle = uint16_t(int32_t(trail.shape) * 0x40 + int32_t(r & 0x3f) - 0x20);
    trail.life = uint16_t(uint32_t(int32_t(trail.lifeSixteenths)) << 4);
    int16_t o[3];
    trailBonePoint(trail, o);
    trail.xs[0] = o[0];
    trail.ys[0] = o[1];
    trail.zs[0] = o[2];
    trail.colourStep = 0x60606;
    trail.width = 1;
    ++a.state;
    trailRibbons(a, trail);
}
// exe 0x5bdbb0
void trailFollow(StaticActor& a, BoneTrailFields& trail) {
    trail.length = int16_t(uint16_t(trail.length) + 1u);
    if (trail.length > 0x10) trail.length = 0x10;
    const uint32_t h = (trail.headLow + 1u) & 0xfu;
    trail.head = int16_t(h);
    if (a.pos.x != 0 || a.pos.z != 0) { // its own move (the stage shift) moves the whole trail
        for (int i = 0; i < trail.length; ++i) {
            trail.xs[size_t(i)] = int16_t(uint16_t(trail.xs[size_t(i)]) + uint16_t(a.pos.x.whole));
            trail.zs[size_t(i)] = int16_t(uint16_t(trail.zs[size_t(i)]) + uint16_t(a.pos.z.whole));
        }
        a.pos.x = 0;
        a.pos.z = 0;
    }
    int16_t o[3];
    trailBonePoint(trail, o);
    trail.xs[h] = o[0];
    trail.ys[h] = o[1];
    trail.zs[h] = o[2];
    trail.life -= 1u;
    if (trail.life == 0) ++a.state;
    trailRibbons(a, trail);
}
// exe 0x5bdcb0
void trailFade(StaticActor& a, BoneTrailFields& trail) {
    trail.length = int16_t(uint16_t(trail.length) - 1u);
    if (trail.length < 0) {
        freeActor(a);
        return;
    }
    trailRibbons(a, trail);
}
void boneTrail(StaticActor& a) { // exe 0x5bda80
    BoneTrailFields& trail = classState<BoneTrailFields>(a);
    switch (TrailState(a.state)) {
    case TrailState::Start: trailStart(a, trail); return;
    case TrailState::Follow: trailFollow(a, trail); return;
    case TrailState::Fade: trailFade(a, trail); return;
    default: notPortedAt(kStoryPort, "0x5bda80 state (table 0x8d0dbc)", a.state);
    }
}

// ---- the ray burst (static stage type 0x63, exe 0x5bf8a0) ------------------------------------------------------------
// Its row (exe data 0x8d0e08 + variant * 8) {angle mask, add, count, life, u16 colour scale, s16 length}; its block from
// +0x68: per ray an x angle, a y angle, a speed; its count, its ticks left, its life (the state bytes).
// exe 0x5bfa00: every ray's x angle eases toward 0xc0 (+-2 a tick); drawn from the far end (length (life - left + 1) *
// speed * 16 + the row's length, colour {v, v, 0}) to the near one (without the row's length, {v, 0, 0}), v =
// min(scale * left * 16 >> 8, 0xff); DR_MODE 0xe1000220 (additive) + LINE_G2 0x52 at OTZ (the near end's depth >> 2)
// when 0x30 <= OTZ <= the effects' far depth
enum class RayBurstState : uint8_t { Start = 0, Run = 1 };
struct RayBurstRow : DataRecord {
    static constexpr uint32_t kBytes = 0x8;
    uint8_t mask = 0; // +0x0
    uint8_t add = 0; // +0x1
    uint8_t count = 0; // +0x2
    uint8_t life = 0; // +0x3
    uint16_t colourScale = 0; // +0x4
    int16_t length = 0; // +0x6
    template <class R> static RayBurstRow load(const R& r) {
        RayBurstRow v;
        v.mask = r.template read<uint8_t>(0x0);
        v.add = r.template read<uint8_t>(0x1);
        v.count = r.template read<uint8_t>(0x2);
        v.life = r.template read<uint8_t>(0x3);
        v.colourScale = r.template read<uint16_t>(0x4);
        v.length = r.template read<int16_t>(0x6);
        return v;
    }
};
RayBurstRow rayBurstRow(const StaticActor& a) { return dataRecord<RayBurstRow>(stateAddress(TableId::St4cStoryRayBurstRows) + uint32_t(a.variant) * 8, RayBurstRow::kBytes); }
void raysDraw(const StaticActor& a, RayBurstFields& rays) {
    // 0x5f2dd0 / 0x40c6e0 on the position: render-only, result unused
    const RayBurstRow row = rayBurstRow(a);
    const uint32_t count = a.subState, left = a.step, life = a.stepCounter;
    uint32_t v5 = uint32_t(int32_t(uint32_t(row.colourScale) * (left << 4)) >> 8);
    if (v5 > 0xff) v5 = 0xff;
    const int32_t base = row.length;
    const int s = sh();
    const int16_t centre[3] = {a.pos.x.whole, a.pos.y.whole, a.pos.z.whole};
    for (uint32_t i = 0; i < count; ++i) {
        uint8_t ax = rays.xAngles[i];
        const uint32_t e = (ax + 0x40u) & 0xffu;
        if (e <= 2) ax = 0xc0;
        else if (e > 0x80) ax = uint8_t(ax + 2);
        else ax = uint8_t(ax - 2);
        rays.xAngles[i] = ax;
        const int16_t rot[3] = {int16_t(uint32_t(rays.xAngles[i]) << 4), int16_t(uint32_t(rays.yAngles[i]) << 4), 0};
        const int32_t len = int32_t(((life - left + 1u) * rays.speeds[i]) << 4);
        int32_t of[3], on[3];
        rotateVector(rot, 0, 0, len + base, of); // exe 0x5e3970
        rotateVector(rot, 0, 0, len, on);
        int16_t v[2][3];
        for (int k = 0; k < 3; ++k) {
            v[0][k] = t16(((of[k] >> 16) << s) + centre[k]);
            v[1][k] = t16(((on[k] >> 16) << s) + centre[k]);
        }
        const CameraPoint cn = cameraPoint(v[1]);
        const int16_t otz = int16_t(cameraPointDepth(cn) >> 2); // exe 0x40d010 after the RTPT 0x40c730 / 0x40c930
        if (int32_t(otz) > int32_t(stage().effectsFar) || otz < 0x30) continue;
        const uint32_t c[2] = {((v5 | 0x520000u) << 8) | v5, v5};
        static constexpr float kNoOff2[2][2] = {};
        dc::otLineG2(a.at, c, (0x220 >> 5) & 3, v, kNoOff2);
    }
}
// exe 0x5bf8c0
void raysStart(StaticActor& a, RayBurstFields& rays) {
    const RayBurstRow row = rayBurstRow(a);
    a.runFlags |= kActorWarpKept;
    const uint8_t mask = row.mask, add = row.add;
    a.subState = row.count;
    a.stepCounter = row.life;
    a.step = row.life;
    for (uint32_t i = 0; i < a.subState; ++i) {
        const uint32_t r1 = rngStep();
        rays.xAngles[i] = uint8_t((((r1 >> 4) & 0x1ffu) + rays.angleBase) >> 4);
        const uint32_t r2 = rngStep();
        rays.yAngles[i] = uint8_t((((r2 >> 8) & 0x1ffu) + rays.yawBase - 0x800u) >> 4);
        const uint32_t r3 = rngStep();
        rays.speeds[i] = uint8_t((uint8_t(r3 >> 0xc) & mask) + add);
    }
    raysDraw(a, rays);
    ++a.state;
}
void rayBurst(StaticActor& a) { // exe 0x5bf8a0: [0x8d0e20 + state * 4](actor, actor + 0x68)
    RayBurstFields& rays = classState<RayBurstFields>(a);
    switch (RayBurstState(a.state)) {
    case RayBurstState::Start: raysStart(a, rays); return;
    case RayBurstState::Run: // 0x5bf9d0
        if (a.step != 0) {
            a.step -= 1u;
            raysDraw(a, rays);
            return;
        }
        freeActor(a);
        return;
    default: notPortedAt(kStoryPort, "0x5bf8a0 state (table 0x8d0e20)", a.state);
    }
}

// ---- the swirling motes (static stage type 0x65, exe 0x5c0d20) -------------------------------------------------------
// Its life, its count; per mote s16 x / y / z / speed and a size byte. Each tick one more mote (8 / 16 at most), then
// every mote swirls around the centre (the pitch toward it + 0x400 scaled by its speed: the vertical part on y, the
// horizontal part along the yaw +-0x400).
enum class MotesState : uint8_t { Init = 0, Ring = 1, Swirl = 2, Burst = 3 };
// exe 0x5c1590(level, mode): the colour of a level (>= 8: & 7, 0..3 mirrored to 7..4)
uint32_t moteColour(uint32_t l, uint32_t mode) {
    if (l > 7) {
        l &= 7;
        if (l < 4) l = 7 - l;
    }
    const uint32_t e = (((l << 8) - l) << 5) >> 8;
    switch (mode) {
    case 0: return ((((e & 0xfe) << 8) | (e & 0xfe)) << 7) | e;
    case 1: return ((((e & 0xff) << 9) | (e & 0xfe)) << 7) | (e >> 1);
    case 2: return ((((e & 0xfe) << 7) | (e & 0xff)) << 8) | (e >> 1);
    case 3: return ((((e & 0xfe) << 7) | (e & 0xff)) << 8) | e;
    case 4: return ((((e & 0xff) << 8) | (e & 0xff)) << 8) | (e >> 1);
    case 5: return ((((e & 0xfc) << 8) | (e & 0xfc)) << 6) | (e >> 2);
    default: notPortedAt(kStoryPort, "exe 0x5c1590 mode (the exe leaves the colour unwritten)", mode);
    }
}
void moteMove(Mote& p, const int16_t* c, int yawOff) {
    const int16_t point[3] = {p.x, p.y, p.z};
    const uint32_t pi = (uint32_t(pitchOf(c, point)) + 0x400u) & 0xfffu;
    const int32_t s = p.speed;
    const int32_t vert = (int32_t(fixedSin(int(pi))) * s) >> 12, horiz = (int32_t(fixedCos(int(pi))) * s) >> 12;
    const uint32_t yi = (uint32_t(angleOf(c[0], c[2], p.x, p.z)) + uint32_t(yawOff)) & 0xfffu;
    p.x = int16_t(uint16_t(p.x) + uint32_t((int32_t(fixedSin(int(yi))) * horiz) >> 12));
    p.y = int16_t(uint16_t(p.y) + uint32_t(vert));
    p.z = int16_t(uint16_t(p.z) + uint32_t((int32_t(fixedCos(int(yi))) * horiz) >> 12));
}
// exe 0x5c0d80: up to 8 motes on a ring of radius phase + 0xe0, two rings each
void motesRing(StaticActor& a, MotesFields& m) {
    if (m.life == 0) {
        freeActor(a);
        return;
    }
    m.life -= 1u;
    const int32_t i = m.count;
    if (i < 8) {
        const int32_t R = int16_t(m.phase + 0xe0);
        const int angle = int((uint32_t(i) << 9) & 0xfffu);
        Mote& p = m.motes[size_t(i)];
        p.x = int16_t(uint32_t((int32_t(fixedCos(angle)) * R) >> 12) + uint16_t(a.pos.x.whole));
        p.y = int16_t((rngStep() & 0xffu) + uint16_t(a.pos.y.whole));
        p.z = int16_t(uint32_t((int32_t(fixedSin(angle)) * R) >> 12) + uint16_t(a.pos.z.whole));
        p.speed = int16_t(uint32_t(int32_t(m.speed)) << 1);
        m.sizes[size_t(i)] = int8_t((m.mask & rngStep()) + 3u);
        ++m.count;
    }
    const int16_t c[3] = {a.pos.x.whole, a.pos.y.whole, a.pos.z.whole};
    const uint32_t col = moteColour(m.life, a.variant);
    for (int32_t k = m.count - 1; k >= 0; --k) {
        Mote& p = m.motes[size_t(k)];
        moteMove(p, c, 0x400);
        const uint32_t xy = uint32_t(uint16_t(p.y)) << 16 | uint16_t(p.x);
        const uint32_t z = uint32_t(uint16_t(p.z)) | 0x30000u;
        dc::emit({0x4000u, dc::ringColour(col), xy, dc::kSemiAdditive, z, 0x400000u}, a.at);
        dc::emit({0x4000u, dc::ringColour(col), xy, dc::kSemiAdditive, z, 0x200000u}, a.at);
    }
}
// exe 0x5c1010: up to 16 motes stepping round (the phase += 4), billboards
void motesSwirl(StaticActor& a, MotesFields& m) {
    if (m.life == 0) {
        freeActor(a);
        return;
    }
    m.life -= 1u;
    if (m.count < 0x10) {
        const int32_t R = int16_t(((rngStep() >> 4) & 0x7fu) + 0xe0u);
        const int angle = int((uint32_t(uint8_t(m.phase)) & 0x3fu) << 6);
        const uint32_t i = uint32_t(int32_t(m.count));
        Mote& p = m.motes[i];
        p.x = int16_t(uint32_t((int32_t(fixedCos(angle)) * R) >> 12) + uint16_t(a.pos.x.whole));
        p.y = int16_t((rngStep() & 0xffu) + uint16_t(a.pos.y.whole));
        p.z = int16_t(uint32_t((int32_t(fixedSin(angle)) * R) >> 12) + uint16_t(a.pos.z.whole));
        p.speed = int16_t((uint32_t(uint16_t(int16_t(int8_t(m.mask)))) & rngStep()) + uint32_t(int32_t(m.phase)));
        m.sizes[i] = int8_t((m.mask & rngStep()) + 0x28u);
        m.phase = int8_t(uint8_t(m.phase) + 4u);
        ++m.count;
    }
    const int16_t c[3] = {a.pos.x.whole, a.pos.y.whole, a.pos.z.whole};
    const uint32_t col = moteColour(m.life, 5);
    const int s = sh();
    const uint32_t e = uint32_t(int32_t(m.speed)) << 5;
    for (int32_t k = m.count - 1; k >= 0; --k) {
        Mote& p = m.motes[size_t(k)];
        moteMove(p, c, -0x400);
        const uint32_t xs = uint16_t(uint32_t(uint16_t(p.x)) << s), ys = uint16_t(uint32_t(uint16_t(p.y)) << s),
                       zs = uint16_t(uint32_t(uint16_t(p.z)) << s);
        dc::emit({0, dc::billboardColour(col), ys << 16 | xs, e | 0x3c78a000u, zs, (e + 0x1f) | 0x2ea000u,
                  uint32_t(int32_t(m.sizes[size_t(k)])), e | 0xbf00u, 0, (e + 0x1f) | 0xbf00u},
                 a.at, false);
    }
}
// exe 0x5c1310: 16 motes at once on a ring of radius 0x70 from a random angle (4 apart), sizes the angles' steps,
// speed 0x10, swirling up as the ring's (the yaw -0x400)
void motesBurst(StaticActor& a, MotesFields& m) {
    if (m.life == 0) {
        freeActor(a);
        return;
    }
    m.life -= 1u;
    if (a.step == 0) {
        m.count = 0;
        uint32_t k = (rngStep() >> 6) & 0x3fu;
        for (; m.count <= 0xf; ++m.count) {
            Mote& p = m.motes[size_t(int32_t(m.count))];
            p.x = int16_t(int16_t((int32_t(fixedCos64(k)) * 0xe0) >> 13) + a.pos.x.whole);
            p.y = a.pos.y.whole;
            p.z = int16_t(int16_t((int32_t(fixedSin64(k)) * 0xe0) >> 13) + a.pos.z.whole);
            p.speed = 0x10;
            m.sizes[size_t(int32_t(m.count))] = int8_t(k);
            k += 4;
        }
        ++a.step;
    }
    const int16_t c[3] = {a.pos.x.whole, a.pos.y.whole, a.pos.z.whole};
    const uint32_t col = moteColour(m.life, a.variant);
    for (int32_t k = m.count - 1; k >= 0; --k) {
        Mote& p = m.motes[size_t(k)];
        moteMove(p, c, -0x400);
        const uint32_t xy = uint32_t(uint16_t(p.y)) << 16 | uint16_t(p.x);
        const uint32_t z = uint32_t(uint16_t(p.z)) | 0x40000u;
        dc::emit({0x4000u, dc::ringColour(col), xy, dc::kSemiAdditive, z, 0x400000u}, a.at);
        dc::emit({0x4000u, dc::ringColour(col), xy, dc::kSemiAdditive, z, 0x200000u}, a.at);
    }
}
void swirlingMotes(StaticActor& a) { // exe 0x5c0d20: [0x8d0e40 + state * 4](actor)
    MotesFields& m = classState<MotesFields>(a);
    switch (MotesState(a.state)) {
    case MotesState::Init: // 0x5c0d40: hidden, the count 0, the state = its kind + 1 (1..3)
        a.setDrawn(false);
        m.count = 0;
        if (a.spawnByte <= 2) a.enterState(uint8_t(a.spawnByte + 1u));
        return;
    case MotesState::Ring: motesRing(a, m); return;
    case MotesState::Swirl: motesSwirl(a, m); return;
    case MotesState::Burst: motesBurst(a, m); return;
    default: notPortedAt(kStoryPort, "0x5c0d20 state (table 0x8d0e40)", a.state);
    }
}

void spriteDraw(const EffectActor& a, const SpriteFields& f, uint32_t w6) {
    const int s = sh();
    const uint32_t xs = uint16_t(uint32_t(uint16_t(a.pos.x)) << s), ys = uint16_t(uint32_t(uint16_t(a.pos.y)) << s),
                   zs = uint16_t(uint32_t(uint16_t(a.pos.z)) << s);
    const uint32_t u = uint32_t(int32_t(f.u));
    dc::emit({0, dc::billboardColour(0x808080), ys << 16 | xs, u | 0x3c388000u, zs, (u + 0x1f) | 0x2e8000u, w6,
              u | 0x9f00u, 0, (u + 0x1f) | 0x9f00u},
             a.at, false);
}
// exe 0x43fc90 (variant 0 of 0x43fc70): rotated, follows its pool-0x20 parent
void animatedSprite(EffectActor& a) {
    SpriteFields& f = classState<SpriteFields>(a);
    if (a.state == 0) {
        a.loopFlags |= kActorLateLoop;
        f.clearBlockBytes();
        f.frames = 5;
        f.frameTicks = 1;
        f.rotation = uint16_t((rngStep() >> 2) & 0xfffu);
        f.parent = int16_t(uint16_t(f.parent) - 0x20u);
        a.state = 1;
    }
    const AnimatedActor& p = animatedActorAt(kAnimatedPool + uint32_t(int32_t(f.parent)) * kAnimatedSlotSize);
    a.pos.x = int16_t(uint16_t(a.pos.x) + uint16_t(uint16_t(p.pos.x.whole) - uint16_t(p.prevPos.x.whole)));
    a.pos.y = int16_t(uint16_t(a.pos.y) + uint16_t(uint16_t(p.pos.y.whole) - uint16_t(p.prevPos.y.whole)));
    a.pos.z = int16_t(uint16_t(a.pos.z) + uint16_t(uint16_t(p.pos.z.whole) - uint16_t(p.prevPos.z.whole)));
    spriteDraw(a, f, (uint32_t(int32_t(int16_t(f.rotation))) << 16) | uint32_t(int32_t(f.size)));
    if (f.frames != 0) {
        f.frameTicks -= 1u;
        if (f.frameTicks != 0) return;
        f.u = int16_t(uint16_t(f.u) + 0x20u);
        f.frames -= 1u;
        f.frameTicks = 1;
        return;
    }
    if (f.holdTicks != 0) {
        f.holdTicks -= 1u;
        return;
    }
    freeActor(a);
}
// exe 0x43fe50: still, 6 frames of 2 ticks
void stillSprite(EffectActor& a) {
    SpriteFields& f = classState<SpriteFields>(a);
    if (a.state == 0) {
        a.loopFlags |= kActorLateLoop;
        f.clearBlockBytes();
        f.stillFrames = 6;
        f.stillTicks = 2;
        a.state = 1;
    }
    spriteDraw(a, f, uint32_t(int32_t(f.size)));
    if (f.stillFrames != 0) {
        f.stillTicks -= 1u;
        if (f.stillTicks != 0) return;
        f.u = int16_t(uint16_t(f.u) + 0x20u);
        f.stillTicks = 2;
        f.stillFrames -= 1u;
        return;
    }
    if (f.holdTicks != 0) {
        f.holdTicks -= 1u;
        return;
    }
    freeActor(a);
}

} // namespace

bool st4cScriptPorted(uint8_t id) {
    const RoutineId h = storyScriptHandler(id).id();
    return h == RoutineId::St4cIntroScript || h == RoutineId::St4cOutroScript;
}
bool st4cScriptTick(const PlayerEnv&, uint8_t id) {
    switch (storyScriptHandler(id).id()) {
    case RoutineId::St4cIntroScript: introScript(); return true;
    case RoutineId::St4cOutroScript: outroScript(); return true;
    default: return false;
    }
}

void elysiumIntroCastUpdate(const PlayerEnv& env, uint32_t actor) { introCast(env, animatedActorAt(actor)); }
void elysiumDefeatCastUpdate(const PlayerEnv& env, uint32_t actor) { defeatedBoss(env, animatedActorAt(actor)); }
void boneTrailUpdate(const PlayerEnv&, uint32_t actor) { boneTrail(staticActorAt(actor)); }
void rayBurstUpdate(const PlayerEnv&, uint32_t actor) { rayBurst(staticActorAt(actor)); }
void swirlingMotesUpdate(const PlayerEnv&, uint32_t actor) { swirlingMotes(staticActorAt(actor)); }
void animatedSpriteEffectUpdate(const PlayerEnv&, uint32_t actor) { // [0x88d990 + variant * 4]
    EffectActor& a = effectActorAt(actor);
    switch (a.variant) {
    case 0: animatedSprite(a); return;
    case 1: stillSprite(a); return;
    default: notPortedAt(kStoryPort, "0x43fc70 variant (table 0x88d990)", a.variant);
    }
}
void animatedFollowerSpriteUpdate(const PlayerEnv&, uint32_t actor) { animatedSprite(effectActorAt(actor)); }
void stillFollowerSpriteUpdate(const PlayerEnv&, uint32_t actor) { stillSprite(effectActorAt(actor)); }
void stillSpriteEffectUpdate(const PlayerEnv&, uint32_t actor) { // [0x88d994 + variant * 4]
    EffectActor& a = effectActorAt(actor);
    switch (a.variant) {
    case 0: stillSprite(a); return;
    default: notPortedAt(kStoryPort, "0x43fe30 variant (table 0x88d994)", a.variant);
    }
}

} // namespace od::port
