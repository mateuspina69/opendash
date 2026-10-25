// The end of the game: the room-script tick's inline stage case ST5C (exe 0x58cb10) and the cutscene scripts 0x75
// (exe 0x53e880), 0x6d (exe 0x53aec0), 0x3f (exe 0x52bc40), 0x6f (exe 0x53c3f0) and 0x6e (exe 0x53bb60). The script
// block: its state / sub-state, the step routines' phases, the step tick, the cast (its actors), the step list, the
// player's face tracks; each script names its own bytes. One function per exe function (addresses in the comments).
// Render-only calls: the exe 0x5f3150 tints go through od::dc::screenTint; sprite / page draws are not drawn here.
#include "content/late_game/end_scripts.hpp"
#include "content/late_game/end_scripts_state.hpp"

#include <utility>

#include "content/late_game/st5c.hpp"

#include "engine/routine_ids.hpp"
#include "game/loaded_data.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actor_sweeps.hpp"
#include "game/audio_logic.hpp"
#include "game/camera.hpp"
#include "game/display.hpp"
#include "game/draw_commands.hpp"
#include "game/explosions.hpp"
#include "game/fade_state.hpp"
#include "game/field_alias.hpp"
#include "game/game_state.hpp"
#include "game/global_class_types.hpp"
#include "game/keyframe_tracks.hpp"
#include "game/message.hpp"
#include "game/player/player_input.hpp"
#include "game/player/player_script_control.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/room_scripts.hpp"
#include "game/script_engine.hpp"
#include "game/script_state.hpp"
#include "game/spawn.hpp"
#include "game/stage_files.hpp"
#include "game/stage_state.hpp"
#include "game/story.hpp"
#include "game/story_flags.hpp"
#include "game/transition.hpp"
#include "game/transition_state.hpp"
#include "game/vram_moves.hpp"

using namespace od::port::end_scripts_state;

namespace od::port {

namespace {

StaffRollHost g_host;

void skip() { scriptSequenceSkip(1); }                                   // exe 0x5115c0(1)
void anim(uint8_t a, uint8_t sub = 0) { playerScriptAnimId(a, sub, 1); } // exe 0x5723d0(a, sub, 1)
KeyframeTracks& faces() { return scriptState().faceTracks; }
void eyes(uint8_t seq) { faces().first.select(seq, 0); }   // exe 0x5e33d0(tracks, seq, 0)
void mouth(uint8_t seq) { faces().second.select(seq, 0); } // exe 0x5e33f0(tracks, seq, 0)
void faceValues(uint8_t eyesValue, uint8_t mouthValue) {
    faceTrackStop(faces(), FaceTrack::Eyes, eyesValue);   // exe 0x5e35f0
    faceTrackStop(faces(), FaceTrack::Mouth, mouthValue); // exe 0x5e3610
}
void mouthValue(uint8_t v) { faceTrackStop(faces(), FaceTrack::Mouth, v); } // exe 0x5e3610
void playerPos(uint32_t x, uint32_t y, uint32_t z, uint16_t yaw) {
    Player& p = player();
    p.pos.x = int32_t(x);
    p.pos.y = int32_t(y);
    p.pos.z = int32_t(z);
    p.rotation.y = int16_t(yaw);
}
void playerWholes(int16_t x, int16_t y, int16_t z) {
    Player& p = player();
    p.pos.x.whole = x;
    p.pos.y.whole = y;
    p.pos.z.whole = z;
}
bool fadeBusy() { return fadeState().busy != 0; }
bool fileLoading() { return (fileLoads().loading & kFileLoading) != 0; }
bool streamBusy() { return (fileLoads().loading & kStreamBusy) != 0; }
bool transitionPending() { return transitionRequest().state != 0; }
void unskippable(bool on) {
    if (on) scriptState().flags |= kScriptUnskippable;
    else scriptState().flags &= uint8_t(~kScriptUnskippable);
}
ActorHeader& castActor(uint32_t i) { return (*scriptState().actors[i]); }
Actor& castMover(uint32_t i) { return actorLayoutOf(*scriptState().actors[i]); }
void placeCast(uint32_t i, uint32_t x, uint32_t y, uint32_t z, uint16_t yaw) {
    Actor& c = castMover(i);
    c.pos.x = int32_t(x);
    c.pos.y = int32_t(y);
    c.pos.z = int32_t(z);
    c.rotation.y = int16_t(yaw);
}
// the half-level grey tint of the steps 0x53eaa0 / 0x53edc0: exe 0x5f3150(((X >> 8 | X & 0xfe00 | 0x44000000) >> 1)
// | (X & 0xff00) << 8) with X = n * 0xff0 (draw mode byte 0x22: additive)
void greyTint(uint32_t n) {
    const uint32_t x = n * 0xff0u;
    dc::screenTint((((x >> 8) | (x & 0xfe00u) | 0x44000000u) >> 1) | ((x & 0xff00u) << 8));
}
// the player pushed (0, 0, speed) while the speed is not 0 (exe 0x5e3690)
void walkPlayer(int16_t speed) {
    if (speed != 0) integrateVelocity(player(), 0, 0, speed);
}
// the shared state-1 runner (exe 0x53a5e0 with fade 0x22, exe 0x53b040 with fade 0x21): the step, the list's end or
// the skip (fade) -> the wrap-up; the sequence and the face tracks tick
template <class Step> void runFace(uint8_t fade, Step step) {
    ScriptState& s = scriptState();
    const ScriptStep current = scriptStepAt(s.steps.value());
    if (current.id == kScriptStepEnd) {
        s.enterState(CutsceneState::WrapUp);
    } else {
        if (const RoutineRef routine = stateRoutine(current.routine)) step(routine);
        if (scriptSkipRequested() && display().drawBuffer != 0) { // exe 0x511790
            scriptFade(fade, 0);
            s.enterState(CutsceneState::WrapUp);
        }
    }
    scriptSequenceTick();    // exe 0x5114d0
    faceTracksTick(faces()); // exe 0x5e3410
}
// the player back (exe 0x5723c0, exe 0x511890), on the ground, shown, its hands' shapes 0
void endPlayerBack() {
    playerBackToPlay();
    scriptReplayPadOff();
    Player& p = player();
    p.airborne = 0;
    p.setDrawn(true);
    p.handShapes[0] = 0;
    p.handShapes[1] = 0;
}
void playerScripted() { player().clearDirectionOffsetWords(); }
constexpr uint32_t kSceneSeedA = 0x873ca9e4u, kSceneSeedB = 0x873ca9e6u;

// ---- ST5C (case 0x58cb10): step 0: flag 0x680 cleared; flag 0x5b0 -> script 0x75, flag 0x5b0 cleared (the step
// kept); else the record 0x8cd8c0 + sound 0x4f, step 1. Step 1: flag 0x680 -> script 0x6d, step 2

// ---- script 0x75 (exe 0x53e880, states 0x8ae6f0, camera 0x8ae5b4, steps 0x8ae6a0, face tracks 0x8ae6dc /
Script75Fields& script75Fields() {
    return scriptState().part<Script75Fields>();
}
uint32_t& savedSceneRng() { return stageBlock().part<StageBlockHead>().word; } // the RNG kept in the stage's block
void script75Step(const RoutineRef& routine) {
    ScriptState& s = scriptState();
    Script75Fields& f = script75Fields();
    Player& p = player();
    switch (routine.id()) {
    case RoutineId::EndScript75Step1: {
        // the cast 0x8ae5a0, Megaman (anim 0x91) waits; then driven forward (anim 0x92 by the floor
        // state), spark bursts every 0x20 ticks before 0x9e, shake / vibration; 0xbe: next
        switch (s.stepPhase) {
        case 0:
            actorsSweep(0xffe, 0x80); // exe 0x5e05d0
            scriptActorSpawnRecord(stateAddress(TableId::EndScriptsScript75Record));
            playerWholes(0, 0, int16_t(0xff80));
            anim(0x91);
            scriptFade(0xa, 0); // exe 0x53f6b0
            s.stepPhase += 1u;
            [[fallthrough]];
        case 1:
            if (!fileLoading()) s.stepPhase += 1u;
            break;
        case 2:
            if (!streamBusy() && !fadeBusy() && s.stepTick >= 0x78) s.stepPhase = 3;
            break;
        case 3: s.stepPhase = 4; break;
        case 4:
            switch (s.stepSubPhase) {
            case 0:
                eyes(1);
                mouth(0);
                anim(0x92, 0x13);
                s.stepSubPhase += 1u;
                f.counter = 3;
                f.poseTicks = 0;
                [[fallthrough]];
            case 1: {
                bool tail = true;
                const uint16_t n = f.poseTicks;
                const uint8_t frame = p.body.key.frame;
                if (n != 0) {
                    const uint16_t m = uint16_t(n - 1u);
                    f.poseTicks = m;
                    if (f.side == 0) anim(0x92, m == 0 ? 0x12 : 0x11);
                    else anim(0x92, m == 0 ? 0 : 0x23);
                } else if (frame == 0x10) {
                    f.poseTicks = 0x12;
                    f.side = 0;
                    tail = false;
                } else if (uint8_t(p.body.key.frame) == 0x22) {
                    f.poseTicks = 0x12;
                    f.side = 1;
                    tail = false;
                } else {
                    bool done = false;
                    if (p.body.key.flags & kKeyJump) {
                        f.counter -= 1u;
                        if (f.counter == 0) {
                            anim(0x93);
                            s.stepSubPhase += 1u;
                            done = true;
                        }
                    }
                    if (done) {
                        tail = false;
                    } else {
                        integrateVelocity(p, 0, 0, 0x28); // exe 0x5e3690
                    }
                }
                if (tail) {
                    if (uint32_t(s.stepTick) == 0xb4) s.stepSubPhase += 1u;
                    f.frame = p.body.key.frame;
                }
                break;
            }
            case 2: anim(0x92, f.frame); break;
            default: break;
            }
            break;
        default: break;
        }
        if (s.stepTick < 0x9e && !(gameState().frame & 0x1f)) {
            if (const uint32_t slot = allocActor60(kActorStageClass)) { // exe 0x5e12f0
                StaticActor& burst = staticActorAt(slot);
                SparkBurstParams params(staticActorAt(slot));
                const uint8_t loop = burst.loopFlags;
                burst.flags = kActorLive;
                burst.type = kSparkBurstType;
                burst.loopFlags = uint8_t(loop | kActorLateLoop);
                burst.variant = 0;
                burst.spawnByte = 3;
                burst.pos.x.whole = 0;
                burst.pos.y.whole = int16_t(0xfff6);
                burst.pos.z.whole = int16_t(0xfa30);
                params.phase = 0;
                const uint32_t r = rngStep();
                params.mask = 0xf;
                params.life = 0x20;
                params.speed = int8_t(((r >> 4) & 0xf) + 8u);
            }
        }
        greyTint((gameState().frame & 1u) | 2u); // exe 0x5f3150
        if (gameState().frame & 1) cameraShake(true, 0x100, 0); // exe 0x40ea80
        padVibrate(0, 0x16e6e);                                 // exe 0x55cd10
        if (uint32_t(s.stepTick) == 0xbe) {
            s.clearStepPhase();
            skip();
        }
        return;
    }
    case RoutineId::EndScript75Step2:
        // Megaman placed (0, 0, 0x34) yaw 0, anim 0x93; 0xdc ticks of shake / vibration (the last one
        // stronger); 0x172: next
        if (s.stepPhase == 0) {
            unskippable(false);
            playerWholes(0, 0, 0x34);
            p.rotation.y = 0;
            anim(0x93);
            f.shakeTicks = 0xdc;
            s.stepPhase += 1u;
        }
        if (f.shakeTicks != 0) {
            const uint8_t c = uint8_t(f.shakeTicks - 1u);
            f.shakeTicks = c;
            if (c != 0) {
                if (gameState().frame & 1) cameraShake(true, 0x600, 0x80);
                padVibrate(0, 0x18c8c);
            } else {
                cameraShake(true, 0xa00, 0x80);
                padVibrate(0x1001, 0x872ff);
            }
            greyTint(f.shakeTicks > 0xf ? 0xfu : uint32_t(f.shakeTicks)); // exe 0x5f3150
        }
        if (uint32_t(s.stepTick) == 0x172) {
            s.clearStepPhase();
            skip();
        }
        return;
    case RoutineId::EndScript75Step3: // 0xf0: the voice faded; once idle: next
        switch (s.stepPhase) {
        case 0:
            if (uint32_t(s.stepTick) != 0xf0) return;
            if (voiceClipPlaying()) voiceClipFadeOut(8); // exe 0x601220
            unskippable(true);
            s.stepPhase += 1u;
            return;
        case 1:
            if (voiceClipIdle()) {
                s.clearStepPhase();
                skip();
            }
            return;
        default: return;
        }
    default: notPortedAt(kStoryPort, "script 0x75 step", routine.pointerValue());
    }
}

void script75() {
    ScriptState& s = scriptState();
    switch (CutsceneState(s.state)) {
    case CutsceneState::SetUp: // exe 0x53e8a0
        switch (s.subState) {
        case 0:
            scriptReplayPadOn(); // exe 0x5117f0
            script75Fields().cleared = 0;
            s.subState += 1u;
            savedSceneRng() = rngNext(rngLastValue());
            rngSetState(kSceneSeedA);
            [[fallthrough]];
        case 1:
            if (playerToScriptIfFree() != 0) return; // exe 0x572300
            playerScripted();
            anim(0x80);
            faces().init(stateAddress(TableId::EndScriptsScript75Eyes), stateAddress(TableId::EndScriptsScript75Mouth)); // exe 0x5e33a0
            eyes(2);
            mouth(0);
            s.subState += 1u;
            return;
        case 2:
            if (!fileLoading()) s.subState += 1u;
            return;
        case 3:
            if (streamBusy()) return;
            voiceClipPlay(6); // exe 0x601050
            s.subState += 1u;
            return;
        case 4:
            if (!voiceClipPlaying()) return;
            scriptFade(0xb, 0);
            scriptSequenceStart(stateAddress(TableId::EndScriptsScript75Camera), stateAddress(TableId::EndScriptsScript75Steps)); // exe 0x511390
            s.enterState(CutsceneState::Steps);
            return;
        default: return;
        }
    case CutsceneState::Steps: runFace(0x22, script75Step); return; // exe 0x53a5e0
    case CutsceneState::WrapUp:                                     // exe 0x53e9c0
        switch (s.subState) {
        case 0:
            padVibrate(0, 0x1000000);
            if (voiceClipPlaying()) voiceClipFadeOut(8);
            if (!(s.flags & kScriptSkipTaken)) scriptFade(0x11, 0);
            s.subState = 1;
            [[fallthrough]];
        case 1:
            if (voiceClipIdle()) s.subState += 1u;
            return;
        case 2:
            if (fadeBusy()) return;
            actorsSweep(0xffe, 0x80);
            scriptFade(1, 0);
            s.subState += 1u;
            return;
        case 3:
            rngSetState(rngNext(savedSceneRng()));
            anim(0);
            scriptReplayPadOff(); // exe 0x511890
            playerBackToPlay();   // exe 0x5723c0
            scriptEndBlock();     // exe 0x5116a0
            s.subState += 1u;
            return;
        default: return;
        }
    default: notPortedAt(kStoryPort, "script 0x75 state", s.state);
    }
}

// ---- script 0x6d (exe 0x53aec0, states 0x8adacc, camera 0x8ad5cc, steps 0x8ad9a8, face tracks 0x8ada3c /
SavedRngWalkFields& rngWalkFields() {
    return scriptState().part<SavedRngWalkFields>();
}
// step 0x53b1d0's debris (a blast, variant 3) at the cast 0's position + (dx, dy, dz), its size
void defeatDebris(int16_t dx, int16_t dy, int16_t dz, uint16_t size) {
    const uint32_t slot = allocActor60(0); // exe 0x5e1300
    if (!slot) return;
    StaticActor& b = staticActorAt(slot);
    BlastParams params(actorHeaderAt(slot));
    const uint8_t loop = b.loopFlags;
    b.flags = kActorLive;
    b.type = typeByte(GlobalStaticType::Blast);
    b.variant = 3;
    b.loopFlags = uint8_t(loop | kActorSweepGroup2);
    const Actor& cast = castMover(0);
    b.pos.x = cast.pos.x;
    b.pos.y = cast.pos.y;
    params.attack = 0;
    b.pos.z = cast.pos.z;
    b.pos.x.whole = int16_t(uint16_t(b.pos.x.whole) + uint16_t(dx));
    b.pos.y.whole = int16_t(uint16_t(b.pos.y.whole) + uint16_t(dy));
    b.pos.z.whole = int16_t(uint16_t(b.pos.z.whole) + uint16_t(dz));
    params.size = size;
}
constexpr uint8_t kDustPuffsVariant = 2; // the stage dust effect's dust puffs (content/late_game/st5c)
void defeatStep(const RoutineRef& routine) {
    ScriptState& s = scriptState();
    SavedRngWalkFields& f = rngWalkFields();
    const uint32_t t = uint32_t(s.stepTick);
    switch (routine.id()) {
    case RoutineId::EndDefeatStep1: {
        // the bank swap, the cast 0x8ad590, Megaman at (0x80, 0, 0x208); debris at fixed ticks, a dust
        // effect every even tick past 0x3c, the player pushed by the walk (anim 0x81 / 0x82)
        if (s.stepPhase == 0) {
            actorsSweep(0x6dc, 0x80);
            stage().bankBuffer ^= 1u;
            stageBankWalk(); // exe 0x5f3200
            scriptActorSpawnRecord(stateAddress(TableId::EndScriptsDefeatRecord));
            playerPos(0x800000, 0, 0x2080000, 0);
            f.walk = 0;
            s.flags &= uint8_t(~kScriptSkipLocked);
            s.stepPhase += 1u;
        }
        switch (t) {
        case 0x19: case 0x1e: defeatDebris(-0xc0, 0x180, 0x118, 0x3c); break;
        case 0x50: case 0x55: defeatDebris(0x80, 0xc0, 0xb0, 0x46); break;
        case 0x7d: case 0x82: defeatDebris(-0x60, 0x30, 0xb0, 0x50); break;
        case 0xa0: case 0xa5: defeatDebris(0x58, -0x20, 0x30, 0x64); break;
        case 0xb4: case 0xb9: defeatDebris(0x30, -0x50, 0x50, 0x28); break;
        case 0xc8: case 0xcd: defeatDebris(-0x40, -0xb0, 0x30, 0x3c); break;
        case 0x1a4:
            anim(0x81);
            f.walk = int16_t(0xfec9);
            break;
        case 0x1b5: anim(0x82); break;
        case 0x1b6: f.walk = int16_t(0xffaf); break;
        case 0x1bc: f.walk = 0; break;
        default: break;
        }
        if (s.stepTick > 0x3c && !(uint8_t(s.stepTick) & 1)) { // the exe's count: (t != 0 ? -7 : 0) + 8 = 1
            if (const uint32_t slot = allocActorA0(kActorStageClass)) { // exe 0x5e13a0
                EffectActor& e = effectActorAt(slot);
                const uint8_t loop = e.loopFlags;
                e.flags = kActorLiveDrawn;
                e.type = kStageDustEffectType;
                e.variant = kDustPuffsVariant;
                e.loopFlags = uint8_t(loop | kActorSweepGroup2);
                e.pos.x = 0x80;
                e.pos.y = int16_t(0xfee0);
                e.pos.z = int16_t(0xfa80);
            }
        }
        walkPlayer(f.walk);
        return;
    }
    case RoutineId::EndDefeatStep2:
        // Megaman at (0x80, 0, 0x97) driven by the walk (anims 0x83 / 0x84), then anims 0x85..0x87 + face
        if (t == 0) {
            s.clearStepPhase();
            playerPos(0x800000, 0, 0x970000, 0);
        }
        switch (t) {
        case 0:
            anim(0x83);
            f.walk = int16_t(0xffaf);
            break;
        case 0x41: anim(0x84); break;
        case 0x42: f.walk = int16_t(0xff9e); break;
        case 0x47: f.walk = int16_t(0xffe0); break;
        case 0x4d:
            f.walk = 0;
            player().pos.z = int32_t(0xff200000u);
            break;
        case 0x78: anim(0x85); break;
        case 0x604:
            anim(0x86);
            faceValues(0x17, 0x16);
            break;
        case 0x690:
            anim(0x87);
            faceValues(0x17, 0x14);
            break;
        default: break;
        }
        walkPlayer(f.walk);
        return;
    case RoutineId::EndDefeatStep3:
        if (t == 0) {
            ActorHeader& cast = castActor(0);
            s.clearStepPhase();
            cast.setDrawn(false);
            mouthValue(0x1a);
        } else if (t == 0xaa) {
            anim(0x88);
        }
        return;
    case RoutineId::EndDefeatStep4:
        if (t == 0) {
            s.clearStepPhase();
            castActor(0).setDrawn(true);
            anim(0x89);
        }
        return;
    case RoutineId::EndDefeatStep5: { // the cast 1 / 2 placed (x -0x38 / -0x70, z -0x60), Megaman moved at 0 / 0x3d4
        auto placeBoth = [](uint32_t x) {
            placeCast(1, x, 0, 0xffa00000u, 0);
            placeCast(2, x, 0, 0xffa00000u, 0);
        };
        if (t == 0x30c) {
            placeBoth(0xff900000u);
        } else if (t == 0) {
            s.clearStepPhase();
            playerPos(0x200000, 0, 0xff300000u, 0);
            placeBoth(0xffc80000u);
        } else if (t == 0x2b2) {
            anim(0x8a);
            eyes(2);
            mouth(1);
        } else if (t == 0x3d4) {
            anim(0x8b);
            playerPos(0xfff00000u, 0, 0xfef00000u, 0x800);
            placeBoth(0xffc80000u);
        }
        return;
    }
    case RoutineId::EndDefeatStep6:
        if (t == 0) {
            s.clearStepPhase();
            placeCast(2, 0xffd00000u, 0, 0xff720000u, 0);
        } else if (t == 0x276) {
            placeCast(2, 0xffd00000u, 0, 0xff500000u, 0);
        }
        return;
    case RoutineId::EndDefeatStep7:
        if (t == 0) {
            s.clearStepPhase();
            player().setDrawn(false);
        }
        return;
    case RoutineId::EndDefeatStep8:
        if (t == 0) {
            s.clearStepPhase();
            player().setDrawn(true);
            placeCast(1, 0xffd00000u, 0, 0xff680000u, 0xc00);
            placeCast(2, 0x200000, 0, 0xff700000u, 0xc00);
        }
        return;
    case RoutineId::EndDefeatStep9: // anims 0x8c / 0x8d + face keys; 0x15e: fade 0x1c; then -> the wrap-up
        if (t == 0) s.clearStepPhase();
        switch (s.stepPhase) {
        case 0:
            switch (t) {
            case 0:
                anim(0x8c);
                faceValues(0x21, 0x1e);
                return;
            case 0x28: mouth(2); return;
            case 0x43: case 0x8e: case 0xf0: case 0x122: mouthValue(0x1e); return;
            case 0x82:
                eyes(5);
                mouth(2);
                return;
            case 0xc8:
                anim(0x8d);
                eyes(4);
                mouth(3);
                return;
            case 0x104: mouth(3); return;
            case 0x140: faceValues(0x1e, 0x21); return;
            case 0x15e:
                scriptFade(0x1c, 0);
                unskippable(true);
                s.stepPhase += 1u;
                return;
            default: return;
            }
        case 1:
            if (!fadeBusy()) s.enterState(CutsceneState::WrapUp);
            return;
        default: return;
        }
    default: notPortedAt(kStoryPort, "script 0x6d step", routine.pointerValue());
    }
}

void script6d() {
    ScriptState& s = scriptState();
    SavedRngWalkFields& f = rngWalkFields();
    switch (CutsceneState(s.state)) {
    case CutsceneState::SetUp: // exe 0x53aee0
        switch (s.subState) {
        case 0:
            scriptReplayPadOn();
            soundFadeOut(0xf, 0x222, 0); // exe 0x5d5780
            f.savedRng = rngNext(rngLastValue());
            rngSetState(kSceneSeedB);
            f.walk = 0;
            s.flags |= kScriptSkipLocked;
            s.subState += 1u;
            [[fallthrough]];
        case 1:
            if (playerToScriptWhenStanding() != 0) return; // exe 0x572320
            playerScripted();
            anim(0);
            faces().init(stateAddress(TableId::EndScriptsScript6dEyes), stateAddress(TableId::EndScriptsScript6dMouth));
            faceValues(0x14, 0x14);
            s.subState += 1u;
            return;
        case 2:
            if (fileLoading()) return;
            datLoadRequest(0x157); // exe 0x40efe0
            s.subState += 1u;
            return;
        case 3:
            if (!fileLoading()) s.subState += 1u;
            return;
        case 4:
            if (streamBusy() || !soundDone(0xf)) return; // exe 0x5d5900
            voiceClipPlay(7);
            s.subState += 1u;
            return;
        case 5:
            if (!voiceClipPlaying()) return;
            scriptSequenceStart(stateAddress(TableId::EndScriptsScript6dCamera), stateAddress(TableId::EndScriptsScript6dSteps));
            s.enterState(CutsceneState::Steps);
            return;
        default: return;
        }
    case CutsceneState::Steps: runFace(0x21, defeatStep); return; // exe 0x53b040
    case CutsceneState::WrapUp: // exe 0x53b0a0: -> ST57 sub 0 (request 2 at (0, -1, 0), fades 0xc / 0xff)
        switch (s.subState) {
        case 0:
            padVibrate(0, 0x1000000);
            anim(0);
            actorsSweep(0xffe, 0x80);
            windowCloseAll(WindowClose::Gentle); // exe 0x5577d0
            if (voiceClipPlaying()) voiceClipFadeOut(8);
            s.subState += 1u;
            [[fallthrough]];
        case 1:
            if (!voiceClipIdle() || fadeBusy()) return;
            s.subState += 1u;
            requestTransition(2, 0x57, 0, 0, -1, 0, 0, 0xc, 0xff);
            return;
        case 2:
            if (transitionPending()) return;
            endPlayerBack();
            rngSetState(rngNext(f.savedRng));
            scriptEndBlock();
            return;
        default: return;
        }
    default: notPortedAt(kStoryPort, "script 0x6d state", s.state);
    }
}

// ---- script 0x3f (exe 0x52bc40, ST52; states 0x8a80f8, camera 0x8a7a84, steps 0x8a8038, face tracks 0x8a80b4 /
Script3fFields& script3fFields() {
    return scriptState().part<Script3fFields>();
}
constexpr uint32_t kFaderSlot = 0x12;
enum class FlipPhase : uint8_t { Start = 0, Held = 1, Slide = 2, Done = 3 };
// exe 0x52cd70: the VRAM flip-book of step 0x52c110: start (copy (0x700, 0x140) 0x40 x 0x80 -> (0x640, 0x100)), then
// held; sliding: the window slides down (y 0x100 + the row, the row from its value up to 0x80), then done
void vramFlipBook() {
    Script3fFields& f = script3fFields();
    switch (FlipPhase(f.flip)) {
    case FlipPhase::Start:
        f.flip = uint8_t(FlipPhase::Held);
        f.flipRow = 0x40;
        vramMove(0x700, 0x140, 0x40, 0x80, 0x640, 0x100); // exe 0x40b710 packet
        return;
    case FlipPhase::Slide:
        if (f.flipRow > 0x80) {
            f.flip = uint8_t(FlipPhase::Done);
            return;
        }
        vramMove(0x700, 0x100 + f.flipRow, 0x40, 0x80, 0x640, 0x100);
        f.flipRow += 1u;
        return;
    default: return;
    }
}
void faderStep(int d) { castActor(kFaderSlot).params[3] += uint32_t(d); } // the fader's brightness step
void faderSpawn(uint8_t v) {
    scriptActorSpawnRecord(stateAddress(TableId::EndScriptsFaderRecord));
    castActor(kFaderSlot).params[3] = v;
}
// the shared fade-in ticks 0x10..0x38 (every 8: the brightness step - 1) and 0x40 (the fader freed); true when t
// matched
bool faderFadeIn(uint32_t t) {
    switch (t) {
    case 0x10: case 0x18: case 0x20: case 0x28: case 0x30: case 0x38: faderStep(-1); return true;
    case 0x40: scriptActorFreeRecord(stateAddress(TableId::EndScriptsFaderRecord)); return true;
    default: return false;
    }
}

void returnStep(const RoutineRef& routine) {
    ScriptState& s = scriptState();
    Script3fFields& f = script3fFields();
    Player& p = player();
    const uint32_t t = uint32_t(s.stepTick);
    switch (routine.id()) {
    case RoutineId::EndReturnStep1: // the message window 0 at 0x1c8, closed at 0x244; 0x4ce: fade 0x12; -> ST52 sub 1
        switch (s.stepPhase) {
        case 0:
            scriptFade(2, 0);
            faderSpawn(7);
            s.stepPhase += 1u;
            [[fallthrough]];
        case 1:
            if (faderFadeIn(t)) return;
            switch (t) {
            case 0x1b8:
                scriptFade(0x12, 0);
                unskippable(true);
                return;
            case 0x1c8: windowOpen(0, 0x73c000, 0); return; // exe 0x557390
            case 0x244:
                windowClose(0, WindowClose::Force); // exe 0x5576b0
                scriptFade(2, 0);
                return;
            case 0x254: unskippable(false); return;
            case 0x35c: faderSpawn(1); return;
            case 0x36c: case 0x37c: case 0x38c: case 0x39c: case 0x3ac: case 0x3bc: faderStep(1); return;
            case 0x4ce:
                scriptFade(0x12, 0);
                unskippable(true);
                s.stepPhase += 1u;
                return;
            default: return;
            }
        case 2:
            if (fadeBusy()) return;
            scriptActorFreeRecord(stateAddress(TableId::EndScriptsFaderRecord));
            requestTransition(0xff, 0x52, 1, 0, 0, 0, 0, 0xff, 0xff);
            s.clearStepPhase();
            skip();
            return;
        default: return;
        }
    case RoutineId::EndReturnStep2: // the VRAM flip-book from 0x1f4; 0xb00: fade 0x14; -> ST52 sub 2 at (0, -0x78, 0)
        switch (s.stepPhase) {
        case 0:
            actorsSweep(0xefe, 0x80);
            scriptFade(2, 0);
            faderSpawn(7);
            unskippable(false);
            s.stepPhase += 1u;
            [[fallthrough]];
        case 1:
            if (!faderFadeIn(t)) {
                switch (t) {
                case 0x1f4: f.flip = uint8_t(FlipPhase::Slide); break;
                case 0xa8c: faderSpawn(1); break;
                case 0xa9c: case 0xaac: case 0xabc: case 0xacc: case 0xadc: case 0xaec: faderStep(1); break;
                case 0xb00:
                    scriptFade(0x14, 0);
                    unskippable(true);
                    s.stepPhase += 1u;
                    break;
                default: break;
                }
            }
            break;
        case 2:
            if (fadeBusy()) break;
            scriptActorFreeRecord(stateAddress(TableId::EndScriptsFaderRecord));
            p.airborne = kAirborneHanging;
            requestTransition(0xff, 0x52, 2, 0, -0x78, 0, 0, 0xff, 0xff);
            s.clearStepPhase();
            skip();
            return;
        default: break;
        }
        vramFlipBook();
        return;
    case RoutineId::EndReturnStep3:
        // Megaman (0, -0x78, 0): 0xdc the casts 0x8a7a20 / 0x8a7a34, anims 0x80..0x82; 0x316 hidden;
        // 0x41a fade 0x14; 0x4ec -> ST52 sub 3
        switch (s.stepPhase) {
        case 0:
            actorsSweep(0xffe, 0x80);
            p.airborne = kAirborneHanging;
            playerPos(0, 0xff880000u, 0, 0);
            scriptFade(2, 0);
            unskippable(false);
            s.stepPhase += 1u;
            [[fallthrough]];
        case 1:
            switch (t) {
            case 0xdc:
                scriptActorSpawnRecord(stateAddress(TableId::EndScriptsReturnFirstRecord));
                scriptActorSpawnRecord(stateAddress(TableId::EndScriptsReturnSecondRecord));
                p.setDrawn(true);
                anim(0x80);
                faceValues(0x14, 0x14);
                return;
            case 0xe6: anim(0x81); return;
            case 0x118: anim(0x82); return;
            case 0x316: p.setDrawn(false); return;
            case 0x41a:
                scriptFade(0x14, 0);
                unskippable(true);
                return;
            case 0x4ec: s.stepPhase += 1u; return;
            default: return;
            }
        case 2:
            if (fadeBusy()) return;
            requestTransition(0xff, 0x52, 3, 0, 0, 0, 0, 0xff, 0xff);
            s.clearStepPhase();
            skip();
            return;
        default: return;
        }
    case RoutineId::EndReturnStep4:
        // the cast 4 placed at 0x26c; 0x960: fade 0x14; -> ST52 sub 2 at (0x80, -0x78, 0xdc)
        switch (s.stepPhase) {
        case 0:
            actorsSweep(0xffe, 0x80);
            p.setDrawn(false);
            scriptFade(2, 0);
            faderSpawn(7);
            unskippable(false);
            s.stepPhase += 1u;
            [[fallthrough]];
        case 1:
            if (faderFadeIn(t)) return;
            switch (t) {
            case 0x26c: {
                Actor& c = castMover(4);
                c.pos.x.whole = 0x500;
                c.pos.y.whole = int16_t(0xff49);
                c.pos.z.whole = 0x500;
                c.rotation.y = 0x800;
                return;
            }
            case 0x8e8: faderSpawn(1); return;
            case 0x8f8: case 0x908: case 0x918: case 0x928: case 0x938: case 0x948: faderStep(1); return;
            case 0x960:
                scriptFade(0x14, 0);
                unskippable(true);
                s.stepPhase += 1u;
                return;
            default: return;
            }
        case 2:
            if (fadeBusy()) return;
            scriptActorFreeRecord(stateAddress(TableId::EndScriptsFaderRecord));
            requestTransition(0xff, 0x52, 2, 0x80, -0x78, 0xdc, 0, 0xff, 0xff);
            s.clearStepPhase();
            skip();
            return;
        default: return;
        }
    case RoutineId::EndReturnStep5:
        // Megaman at (0x80, -0x78, 0xdc) anim 0x83, the casts 0x8a7a20 / 0x8a7a34 (+ 0x8a7994 / 0x8a7a5c /
        // 0x8a79a8 later, their step counter 1 starts them), anims 0x84..0x89 driven by the walk; 0xeec: fade 0x12, the
        // voice out, DAT 0x13d, the voice 0x1a; -> ST52 sub 4 at (0x80, 0, 0x80)
        switch (s.stepPhase) {
        case 0:
            actorsSweep(0xffe, 0x80);
            anim(0x83);
            scriptActorSpawnRecord(stateAddress(TableId::EndScriptsReturnFirstRecord));
            scriptActorSpawnRecord(stateAddress(TableId::EndScriptsReturnSecondRecord));
            p.setDrawn(true);
            p.airborne = kAirborneHanging;
            playerPos(0x800000, 0xff880000u, 0xdc0000, 0);
            f.walk = 0;
            scriptFade(2, 0);
            unskippable(false);
            s.stepPhase += 1u;
            [[fallthrough]];
        case 1:
            switch (t) {
            case 0x78:
                eyes(0);
                mouth(0);
                break;
            case 0x186: scriptActorSpawnRecord(stateAddress(TableId::EndScriptsReturnThirdRecord)); break;
            case 0x460:
                anim(0x84);
                faceValues(0x1b, 0x14);
                p.airborne = 0;
                playerPos(0x800000, 0, 0xdc0000, 0);
                castActor(14).stepCounter = 1;
                castActor(15).stepCounter = 1;
                scriptActorSpawnRecord(stateAddress(TableId::EndScriptsReturnFourthRecord));
                castActor(7).stepCounter = 1;
                break;
            case 0x4c4:
                anim(0x85);
                f.walk = int16_t(0xffe8);
                break;
            case 0x4c8: castActor(7).stepCounter = 1; break;
            case 0x4cb: f.walk = int16_t(0xffe0); break;
            case 0x4ec: scriptActorSpawnRecord(stateAddress(TableId::EndScriptsReturnFifthRecord)); break;
            case 0x4f2:
                p.pos.z = 0x800000;
                f.walk = 0;
                break;
            case 0x4fb: anim(0x86); break;
            case 0x58c: anim(0x87); break;
            case 0x5b4: mouth(1); break;
            case 0x5dc: eyes(1); break;
            case 0x5f0: castActor(8).stepCounter = 1; break;
            case 0x640: anim(0x88); break;
            case 0x744: anim(0x89); break;
            case 0xeec:
                unskippable(true);
                s.stepPhase += 1u;
                break;
            default: break;
            }
            break;
        case 2:
            scriptFade(0x12, 0);
            if (voiceClipPlaying()) voiceClipFadeOut(8);
            s.stepPhase += 1u;
            break;
        case 3:
            if (voiceClipIdle()) {
                datLoadRequest(0x13d);
                s.stepPhase += 1u;
            }
            break;
        case 4:
            if (!fileLoading()) s.stepPhase += 1u;
            break;
        case 5:
            if (!streamBusy() && !fadeBusy()) {
                voiceClipPlay(0x1a);
                s.stepPhase += 1u;
            }
            break;
        case 6:
            if (voiceClipPlaying()) {
                requestTransition(0xff, 0x52, 4, 0x80, 0, 0x80, 0, 0xff, 0xff);
                s.clearStepPhase();
                skip();
            }
            break;
        default: break;
        }
        walkPlayer(f.walk);
        return;
    case RoutineId::EndReturnStep6:
        // the bank swap, Megaman anim 0x89 sub 0xd; anims 0x8a / 0x8b + face keys, the cast 12 / 13
        // placed at 0x8f2; 0xcd0: fade 0x12; then next (-> the wrap-up)
        switch (s.stepPhase) {
        case 0:
            actorsSweep(0xffe, 0x80);
            stage().bankBuffer ^= 1u;
            stageBankWalk(); // exe 0x5f3200
            p.setDrawn(true);
            anim(0x89, 0xd);
            faceValues(0x1d, 0x14);
            scriptFade(2, 0);
            unskippable(false);
            s.stepPhase += 1u;
            [[fallthrough]];
        case 1:
            switch (t) {
            case 0x744: anim(0x8b); return;
            case 0x8f2: {
                anim(0x8a);
                faceValues(0x1d, 0x17);
                Actor& c = castMover(12);
                c.pos.x.whole = int16_t(0xffe0);
                c.pos.y.whole = 0;
                c.pos.z.whole = int16_t(0xfff0);
                c.rotation.y = 0xc00;
                Actor& d = castMover(13);
                d.pos.x.whole = int16_t(0xffe8);
                d.pos.y.whole = 0;
                d.pos.z.whole = 0xa0;
                d.rotation.y = 0xe80;
                return;
            }
            case 0x8fc: case 0xa1e: case 0xb72: eyes(2); return;
            case 0xc94:
                eyes(3);
                mouthValue(0x18);
                return;
            case 0xcd0:
                scriptFade(0x12, 0);
                unskippable(true);
                s.stepPhase += 1u;
                return;
            default: return;
            }
        case 2:
            if (fadeBusy()) return;
            s.clearStepPhase();
            skip();
            return;
        default: return;
        }
    default: notPortedAt(kStoryPort, "script 0x3f step", routine.pointerValue());
    }
}

void script3f() {
    ScriptState& s = scriptState();
    Script3fFields& f = script3fFields();
    Player& p = player();
    switch (CutsceneState(s.state)) {
    case CutsceneState::SetUp: // exe 0x52bc60
        switch (s.subState) {
        case 0:
            scriptReplayPadOn();
            p.setDrawn(false);
            s.subState += 1u;
            [[fallthrough]];
        case 1:
            if (playerToScriptIfFree() != 0) return;
            playerPos(0, 0, 0, 0);
            playerScripted();
            faces().init(stateAddress(TableId::EndScriptsScript3fEyes), stateAddress(TableId::EndScriptsScript3fMouth));
            faceValues(0x14, 0x14);
            anim(0);
            p.handShapes[0] = 1;
            p.handShapes[1] = 2;
            s.subState += 1u;
            return;
        case 2:
            if (!fileLoading()) s.subState += 1u;
            return;
        case 3:
            if (streamBusy()) return;
            voiceClipPlay(5);
            s.subState += 1u;
            return;
        case 4:
            if (!voiceClipPlaying()) return;
            scriptSequenceStart(stateAddress(TableId::EndScriptsScript3fCamera), stateAddress(TableId::EndScriptsScript3fSteps));
            f.savedRng = rngNext(rngLastValue());
            rngSetState(kSceneSeedB);
            s.enterState(CutsceneState::Steps);
            return;
        default: return;
        }
    case CutsceneState::Steps: runFace(0x22, returnStep); return; // exe 0x53a5e0
    case CutsceneState::WrapUp: // exe 0x52bdb0: -> ST04 sub 0 (request 2 at (0x108, -1, 0x140), fades 2 / 0xff), the
        // story + 1
        switch (s.subState) {
        case 0:
            padVibrate(0, 0x1000000);
            anim(0);
            actorsSweep(0xffe, 0x80);
            windowCloseAll(WindowClose::Gentle);
            if (voiceClipPlaying()) voiceClipFadeOut(8);
            s.subState += 1u;
            [[fallthrough]];
        case 1:
            if (!voiceClipIdle()) return;
            s.subState += 1u;
            requestTransition(2, 4, 0, 0x108, -1, 0x140, 0, 2, 0xff);
            return;
        case 2:
            if (transitionPending()) return;
            endPlayerBack();
            rngSetState(rngNext(f.savedRng));
            gameState().nextStory += 1u; // exe 0x512800
            scriptEndBlock();
            return;
        default: return;
        }
    default: notPortedAt(kStoryPort, "script 0x3f state", s.state);
    }
}

// ---- script 0x6f (exe 0x53c3f0, ST54; states 0x8ae1a0, camera 0x8ade10, steps 0x8ae078): the dispatcher copies the
Script6fFields& script6fFields() {
    return scriptState().part<Script6fFields>();
}
// The take-off's attachment (stage static type 0x67): two bytes, a word, the cast it follows
struct AttachmentParams {
    explicit AttachmentParams(const StaticActor& actor) : AttachmentParams(const_cast<StaticActor&>(actor)) {}
    explicit AttachmentParams(StaticActor& actor)
        : first(actor.params[0]), second(actor.params[1]), word(actor.params, 2), owner(actor.hitBox.result) {}
    uint8_t& first;
    uint8_t& second;
    BytesField<uint16_t, 4> word;
    uint32_t& owner; // the owner's identity word (the hit box's first word)
};
constexpr uint8_t kAttachmentType = 0x67;
// step 0x53c5f0's attachment (variant v) bound to the cast 0
void spawnAttachment(uint8_t v) {
    const uint32_t slot = allocActor60(kActorStageClass); // exe 0x5e12f0
    if (!slot) return;
    StaticActor& a = staticActorAt(slot);
    AttachmentParams params(a);
    a.flags = kActorLive;
    a.type = kAttachmentType;
    a.subtype = 0;
    a.variant = v;
    a.spawnByte = 0;
    a.pos.x = 0;
    a.pos.y = 0;
    a.pos.z = 0;
    params.first = 0;
    params.second = 0;
    params.word = 0;
    params.owner = slotIdentityWord(scriptState().actors[0]);
}
void act(uint32_t slot, uint8_t action, uint8_t phase) { // the cast's action and its phase
    ActorHeader& c = castActor(slot);
    c.subState = action;
    c.step = phase;
}
constexpr uint8_t kCastFreed = 2; // the cast's state 2: freed

void takeoffStep(const RoutineRef& routine) {
    ScriptState& s = scriptState();
    Script6fFields& f = script6fFields();
    const uint32_t t = uint32_t(s.stepTick);
    switch (routine.id()) {
    case RoutineId::EmptyRoutine: return; // an empty step
    case RoutineId::EndTakeoffStep1:
        if (t == 0x208) {
            skip();
            castActor(2).state = kCastFreed;
        }
        return;
    case RoutineId::EndTakeoffStep2:
        // the cast 0's phase 1 / 2 with the attachments 0 / 1, 2 / 4, 3; past 0xc8 fade 0x11; -> ST54 sub
        // 1 (request 2 at (0, -1, 0), fades 0xff / 0x12), fade 2, next
        switch (s.subState) {
        case 0:
            if (t != 1) return;
            castActor(0).step = 1;
            s.subState += 1u;
            spawnAttachment(0);
            spawnAttachment(1);
            return;
        case 1:
            if (t != 0x64) return;
            castActor(0).step = 2;
            s.subState += 1u;
            spawnAttachment(2);
            spawnAttachment(4);
            return;
        case 2:
            if (t == 0x78) spawnAttachment(3);
            if (s.stepTick <= 0xc8 || fadeBusy()) return;
            s.subState += 1u;
            unskippable(true);
            scriptFade(0x11, 0);
            return;
        case 3:
            if (fadeBusy()) return;
            requestTransition(2, 0x54, 1, 0, -1, 0, 0, 0xff, 0x12);
            s.subState += 1u;
            return;
        case 4:
            if (transitionPending() || fileLoading() || fadeBusy()) return;
            scriptFade(2, 0);
            s.subState += 1u;
            return;
        case 5:
            if (streamBusy() || fadeBusy()) return;
            s.subState = 0;
            skip();
            return;
        default: return;
        }
    case RoutineId::EndTakeoffStep3: // the voice 4; once it plays: next, the cast 1's phase 1
        switch (s.subState) {
        case 0:
            voiceClipPlay(4);
            s.subState += 1u;
            return;
        case 1:
            if (!voiceClipPlaying()) return;
            skip();
            s.subState = 0;
            castActor(1).step = 1;
            unskippable(false);
            return;
        default: return;
        }
    case RoutineId::EndTakeoffStep4:
        switch (s.subState) {
        case 0:
            if (s.stepTick <= 0x8c) return;
            s.subState += 1u;
            castActor(0).step = 1;
            return;
        case 1:
            if (s.stepTick <= 0x104) return;
            s.subState = 0;
            castActor(0).step = 4;
            skip();
            return;
        default: return;
        }
    case RoutineId::EndTakeoffStep5:
        // 0x28: sound 1; the casts 5 / 4 / 1 by tick; from 0x14e: next with the cast 1..3 placed
        if (t == 0x28) soundPlayFlatForced(1);
        if (t == 0xbe) {
            castActor(5).subState = 1;
        } else if (t == 0xdc) {
            castActor(4).step = 2;
        } else if (t == 0x12c) {
            act(1, 1, 0);
            castActor(5).state = kCastFreed;
            castActor(4).state = kCastFreed;
        } else if (s.stepTick >= 0x14e) {
            s.subState = 0;
            skip();
            Actor& c1 = castMover(1);
            c1.pos.x = 0x27a0000;
            c1.pos.y = 0;
            c1.pos.z = int32_t(0xfe400000u);
            Actor& c2 = castMover(2);
            c2.pos.x = 0x2100000;
            c2.pos.y = int32_t(0xfffa0000u);
            c2.pos.z = int32_t(0xfe400000u);
            Actor& c3 = castMover(3);
            c3.pos.x = 0x1aa0000;
            c3.pos.y = 0x20000;
            c3.pos.z = int32_t(0xfe500000u);
        }
        return;
    case RoutineId::EndTakeoffStep6:
        switch (s.subState) {
        case 0:
            if (t != 0x100) return;
            act(1, 2, 0);
            s.subState += 1u;
            return;
        case 1:
            if (t != 0x1a4) return;
            s.subState = 0;
            skip();
            return;
        default: return;
        }
    case RoutineId::EndTakeoffStep7:
        // the casts 2 / 3 by tick; past 0x16d: the voice idle, fade 0x11, -> ST54 sub 2 (fades 0xff / 0x12),
        // fade 2, next
        if (t == 0x1e) act(2, 1, 0);
        if (t == 0xe0) {
            act(3, 1, 0);
            return;
        }
        if (t == 0x145) {
            act(3, 2, 0);
            return;
        }
        if (s.stepTick <= 0x16d) return;
        switch (s.subState) {
        case 0:
            if (!voiceClipIdle()) return;
            s.subState = 1;
            unskippable(true);
            return;
        case 1:
            if (fadeBusy()) return;
            s.subState += 1u;
            scriptFade(0x11, 0);
            return;
        case 2:
            if (fadeBusy()) return;
            requestTransition(2, 0x54, 2, 0, 0, 0, 0, 0xff, 0x12);
            s.subState += 1u;
            return;
        case 3:
            if (transitionPending() || fileLoading() || fadeBusy()) return;
            scriptFade(2, 0);
            s.subState = 4;
            return;
        case 4:
            if (streamBusy() || fadeBusy()) return;
            s.subState = 0;
            unskippable(false);
            skip();
            return;
        default: return;
        }
    case RoutineId::EndTakeoffStep8: // the voice 5; once it plays: next
        switch (s.subState) {
        case 0:
            voiceClipPlay(5);
            s.subState += 1u;
            return;
        case 1:
            if (!voiceClipPlaying()) return;
            skip();
            s.subState = 0;
            return;
        default: return;
        }
    case RoutineId::EndTakeoffStep9:
        if (t == 2) castActor(2).step = 1;
        else if (t == 0x50) act(1, 1, 0);
        return;
    case RoutineId::EndTakeoffStep10:
        if (t == 2) {
            ActorHeader& c2 = castActor(2);
            c2.subState = 1;
            c2.step = 0;
            placeCast(2, 0, 0xfff00000u, 0xfd680000u, 0);
        } else if (t == 0x35) {
            act(0, 1, 1);
        } else if (t == 0x4b) {
            act(1, 2, 0);
        }
        return;
    case RoutineId::EndTakeoffStep11:
        switch (t) {
        case 0xa: act(0, 2, 0); return;
        case 0x5f: act(1, 3, 0); return;
        case 0xc8:
            act(2, 2, 0);
            castMover(2).pos.y = 0;
            return;
        case 0xd2:
            act(2, 2, 1);
            castMover(2).pos.y = 0;
            return;
        // past `cmp ecx, 0xd2; jg 0x53ccf1` the register still holds the tick: sub 0x124 / 6 / 0xbb are the cases
        // 0x124 / 0x12a / 0x1e5 (not 0xd2 + them)
        case 0x124: act(2, 3, 0); return;
        case 0x12a:
            act(0, 3, 0);
            act(1, 4, 0);
            return;
        case 0x1e5:
            s.subState = 0;
            skip();
            return;
        default: return;
        }
    case RoutineId::EndTakeoffStep12:
        if (t == 6) {
            act(0, 4, 0);
            act(1, 5, 0);
            soundFadeOut(0xf, 0xfff, 0);
        } else if (t == 0x19) {
            act(2, 4, 0);
        }
        return;
    case RoutineId::EndTakeoffStep13:
        switch (t) {
        case 1: act(1, 6, 0); return;
        case 0x1e:
            act(0, 5, 0);
            act(2, 5, 0);
            return;
        case 0x78: act(0, 6, 0); return;
        case 0x154: act(1, 7, 0); return;
        case 0x1f9: act(0, 7, 0); return;
        case 0x28f: act(2, 5, 6); return;
        case 0x348: act(3, 2, 2); return;
        case 0x3c0: skip(); return;
        default: return;
        }
    case RoutineId::EndTakeoffStep14:
        switch (t) {
        case 0x46: act(4, 0, 1); return;
        case 0x6e: act(5, 0, 1); return;
        case 0xd2:
            castActor(4).state = kCastFreed;
            castActor(5).state = kCastFreed;
            return;
        case 0x1c2: skip(); return;
        default: return;
        }
    case RoutineId::EndTakeoffStep15:
        // before 0x96 the grey ramps up by 2 to 0x80; then fade 0x12 and next. Every tick the sprite 0xb
        // (exe 0x53cf50, table 0x8ae108) tinted by the grey (render output)
        if (s.stepTick < 0x96) {
            f.grey = f.grey < 0x80 ? uint8_t(f.grey + 2u) : uint8_t(0x80);
        } else if (s.subState == 0) {
            scriptFade(0x12, 0);
            s.subState += 1u;
        } else if (!fadeBusy()) {
            s.subState = 0;
            skip();
        }
        return;
    default: notPortedAt(kStoryPort, "script 0x6f step", routine.pointerValue());
    }
}

void script6f() {
    ScriptState& s = scriptState();
    s.setStepTickCopy();
    switch (CutsceneState(s.state)) {
    case CutsceneState::SetUp: // exe 0x53c410
        switch (s.subState) {
        case 0:
            scriptPlayerReset(); // exe 0x511840
            soundFadeOut(0xf, 0xfff, 0);
            windowCloseAll(WindowClose::Force);
            s.subState = 1;
            return;
        case 1:
            actorsSweep(0xffe, 0x80);
            playerScripted();
            player().setDrawn(false);
            scriptSequenceStart(stateAddress(TableId::EndScriptsScript6fCamera), stateAddress(TableId::EndScriptsScript6fSteps));
            scriptSequenceTick();
            s.subState = 2;
            return;
        case 2:
            if (soundDone(0xf) && !fileLoading()) s.subState = 3;
            return;
        case 3:
            if (streamBusy()) return;
            voiceClipPlay(3);
            s.subState = 4;
            return;
        case 4:
            if (!voiceClipPlaying()) return;
            s.enterStateKeepingPhases(uint8_t(CutsceneState::Steps));
            script6fFields().grey = 0;
            script6fFields().level = 0x80;
            return;
        default: return;
        }
    case CutsceneState::Steps: { // exe 0x53c500 (skip: the music out, fade 0x22); no face tracks
        const ScriptStep current = scriptStepAt(s.steps.value());
        if (current.id == kScriptStepEnd) {
            s.enterState(CutsceneState::WrapUp);
        } else {
            if (const RoutineRef routine = stateRoutine(current.routine)) takeoffStep(routine);
            if (scriptSkipRequested() && display().drawBuffer != 0) {
                soundFadeOut(0xf, 0xfff, 0);
                scriptFade(0x22, 0);
                s.enterState(CutsceneState::WrapUp);
            }
        }
        scriptSequenceTick();
        return;
    }
    case CutsceneState::WrapUp: // exe 0x53c560
        switch (s.subState) {
        case 0:
            if (voiceClipPlaying()) voiceClipFadeOut(8);
            s.subState += 1u;
            return;
        case 1:
            if (fadeBusy() || !soundDone(0xf)) return;
            player().setDrawn(true);
            playerBackToPlay(); // exe 0x5723c0
            scriptReplayPadOff();
            scriptEndBlock();
            return;
        default: return;
        }
    default: notPortedAt(kStoryPort, "script 0x6f state", s.state);
    }
}

// ---- script 0x6e (exe 0x53bb60, ST57: the staff roll; states 0x8adc90, camera 0x8adc78, steps 0x8adc80 (one step
Script6eFields& script6eFields() {
    return scriptState().part<Script6eFields>();
}
// one page row (exe data): its end mark, its length in units, its hold ticks
struct StaffPage : DataRecord {
    static constexpr uint32_t kBytes = 0x4;
    uint8_t end = 0; // +0x0
    uint8_t length = 0; // +0x1
    uint16_t hold = 0; // +0x2
    template <class R> static StaffPage load(const R& r) {
        StaffPage v;
        v.end = r.template read<uint8_t>(0x0);
        v.length = r.template read<uint8_t>(0x1);
        v.hold = r.template read<uint16_t>(0x2);
        return v;
    }
};
constexpr uint32_t kStaffPageSize = 0x10;
constexpr uint8_t kPagesEnd = 0xff;
enum class StaffPhase : uint8_t { Start = 0, Intro = 1, Pages = 2, Finish = 3 };
enum class PagePhase : uint8_t { Show = 0, FadeIn = 1, Hold = 2, FadeOut = 3, Wait = 4 };
enum class BackgroundRamp : uint8_t { None = 0, Down = 1, Up = 2 };

void staffSegment(uint32_t seg) {
    if (g_host.staffSegment) g_host.staffSegment(staffRoll().buffer, seg);
}
void staffNextSegment() { // n = the next segment++, exe 0x40eb90(buffer, n)
    const uint32_t n = staffRoll().segment;
    staffRoll().segment = n + 1u;
    staffSegment(n);
}
void staffFree() { // exe 0x6117e5(buffer)
    if (g_host.staffFree) g_host.staffFree(staffRoll().buffer);
}
void staffUnits() { // the ticks counted by 0x13 into the units
    Script6eFields& f = script6eFields();
    const uint8_t c = f.ticks;
    f.ticks = uint8_t(c + 1u);
    if (c > 0x12) {
        f.units += 1u;
        f.ticks = 0;
    }
}

void staffStep(const RoutineRef& routine) {
    ScriptState& s = scriptState();
    Script6eFields& f = script6eFields();
    if (!routine.is(RoutineId::EndStaffRollStep)) notPortedAt(kStoryPort, "script 0x6e step", routine.pointerValue());
    switch (StaffPhase(s.stepPhase)) {
    case StaffPhase::Start: // exe 0x53be70
        f.page = stateAddress(TableId::EndScriptsStaffPages);
        f.pageBrightness = 0x80;
        f.shown = 0;
        f.ticks = 0;
        f.units = 0;
        if (g_host.musicStart) g_host.musicStart(0, 0); // exe 0x40dc90(0, 0, 0)
        staffNextSegment();
        s.stepPhase = uint8_t(StaffPhase::Intro);
        break;
    case StaffPhase::Intro: // exe 0x53bec0: 10 units, then the background dims and the pages start at unit 0x16
        unskippable(false);
        staffUnits();
        if (f.units == 0xa) {
            f.nextUnit = 0x16;
            f.ramp = uint8_t(BackgroundRamp::Down);
            s.enterStepPhase(uint8_t(StaffPhase::Pages));
        }
        break;
    case StaffPhase::Pages: // exe 0x53bf00: a page fades in (0x1e ticks), holds, fades out (0x1e), waits for its unit
        staffUnits();
        switch (PagePhase(s.stepSubPhase)) {
        case PagePhase::Show:
            f.shown = 1;
            f.pageBrightness = 0;
            f.fade = 0x1e;
            s.stepSubPhase += 1u;
            [[fallthrough]];
        case PagePhase::FadeIn: {
            const uint16_t v = uint16_t(f.fade - 1u);
            f.fade = v;
            if (v == 0) {
                f.pageBrightness = 0x80;
                f.shown = 1;
                f.fade = dataRecord<StaffPage>(f.page, StaffPage::kBytes).hold;
                s.stepSubPhase += 1u;
            } else {
                f.pageBrightness = uint8_t(((0x1e - int32_t(int16_t(v))) << 7) / 0x1e);
            }
            break;
        }
        case PagePhase::Hold: {
            const uint16_t v = uint16_t(f.fade - 1u);
            f.fade = v;
            if (v == 0) {
                f.shown = 1;
                f.fade = 0x1e;
                f.pageBrightness = 0x80;
                s.stepSubPhase += 1u;
            }
            break;
        }
        case PagePhase::FadeOut: {
            const uint16_t v = uint16_t(f.fade - 1u);
            f.fade = v;
            if (v == 0) {
                f.pageBrightness = 0;
                s.stepSubPhase += 1u;
            } else {
                f.pageBrightness = uint8_t((int32_t(int16_t(v)) << 7) / 0x1e);
            }
            break;
        }
        case PagePhase::Wait:
            if (int32_t(f.units) >= int32_t(f.nextUnit)) {
                staffNextSegment();
                const uint32_t p = f.page;
                f.nextUnit += dataRecord<StaffPage>(p, StaffPage::kBytes).length;
                f.page = p + kStaffPageSize;
                s.stepSubPhase = 0;
                if (dataRecord<StaffPage>(f.page, StaffPage::kBytes).end == kPagesEnd) {
                    s.enterStepPhase(uint8_t(StaffPhase::Finish));
                    f.ramp = uint8_t(BackgroundRamp::Up);
                }
            }
            break;
        default: break;
        }
        // exe 0x53c0c0: the page drawn (render output)
        break;
    case StaffPhase::Finish: // exe 0x53c0a0: the loader idle -> fade 0x12, next (-> the wrap-up)
        if (!fileLoading()) {
            scriptFade(0x12, 0);
            skip();
        }
        break;
    default: notPortedAt(kStoryPort, "script 0x6e step phase", s.stepPhase);
    }
    switch (BackgroundRamp(f.ramp)) { // exe 0x53c3a0: the background's ramp
    case BackgroundRamp::Down:
        f.background -= 0x20u;
        if (f.background < 0x500) {
            f.background = 0x500;
            f.ramp = uint8_t(BackgroundRamp::None);
        }
        break;
    case BackgroundRamp::Up:
        f.background += 0x20u;
        if (f.background > 0x800) {
            f.background = 0x800;
            f.ramp = uint8_t(BackgroundRamp::None);
        }
        break;
    default: break;
    }
    // exe 0x53c2b0: the background drawn (render output)
}

void staffEnd() { // exe 0x53bd40: -> ST54 sub 0 (request 2 at (0, -0x281, 0), fades 2 / 0xff)
    ScriptState& s = scriptState();
    switch (s.subState) {
    case 0:
        if (fileLoading()) return;
        s.subState += 1u;
        [[fallthrough]];
    case 1:
        // exe 0x53c2b0: the background drawn (render output)
        if (fadeBusy()) return;
        requestTransition(2, 0x54, 0, 0, -0x281, 0, 0, 2, 0xff);
        s.subState += 1u;
        return;
    case 2:
        if (transitionPending() || fadeBusy()) return;
        playerBackToPlay(); // exe 0x5723c0
        scriptReplayPadOff();
        player().setDrawn(true);
        player().airborne = 0;
        clearStoryFlag(0x245);
        staffFree();
        staffRoll().buffer = 0;
        scriptEndBlock();
        return;
    default: return;
    }
}

void staffRollScript() {
    ScriptState& s = scriptState();
    Script6eFields& f = script6eFields();
    Player& p = player();
    switch (CutsceneState(s.state)) {
    case CutsceneState::SetUp: // exe 0x53bb80
        switch (s.subState) {
        case 0:
            if (staffRoll().buffer != 0) staffFree();
            staffRoll().buffer = g_host.staffLoad ? g_host.staffLoad() : 0u; // "dat\staff.dat" read into the heap
            staffSegment(0);
            staffRoll().segment = 1;
            actorsSweep(0xffe, 0x80);
            scriptSequenceStart(stateAddress(TableId::EndScriptsStaffRollCamera), stateAddress(TableId::EndScriptsStaffRollSteps));
            scriptReplayPadOn();
            unskippable(true);
            setStoryFlag(0x245);
            playerScripted();
            p.setDrawn(false);
            p.idleFaces[0] = 0;
            p.idleFaces[1] = 0;
            playerPos(0x1000000, 0, 0xff500000u, 0);
            s.clearScriptByteRun(); // exe 0x40dc40: the script's bytes +0xc..+0x1b
            f.background = 0x800;
            s.subState += 1u;
            [[fallthrough]];
        case 1:
            if (playerToScriptIfFree() != 0) return;
            s.enterState(CutsceneState::Steps);
            anim(0);
            s.subState += 1u;
            return;
        default: return;
        }
    case CutsceneState::Steps: { // exe 0x53bcc0 (skip: taken, fade 0x22, music out 0x111); the list's end runs the
        // wrap-up at once
        const ScriptStep current = scriptStepAt(s.steps.value());
        if (current.id == kScriptStepEnd) {
            s.enterState(CutsceneState::WrapUp);
            staffEnd();
            scriptSequenceTick();
            return;
        }
        if (const RoutineRef routine = stateRoutine(current.routine)) staffStep(routine);
        if (scriptSkipRequested() && display().drawBuffer != 0) {
            staffRoll().skipped = 1;
            scriptFade(0x22, 0);
            soundFadeOut(0xf, 0x111, 0);
            s.enterState(CutsceneState::WrapUp);
        }
        scriptSequenceTick();
        return;
    }
    case CutsceneState::WrapUp: staffEnd(); return;
    default: notPortedAt(kStoryPort, "script 0x6e state", s.state);
    }
}

} // namespace

void st5cStageCase() {
    RoomScriptState& room = roomScriptState();
    switch (room.handlerStep) {
    case 0:
        clearStoryFlag(0x680);
        if (storyFlag(0x5b0)) {
            scriptRun(0x75); // exe 0x511270
            clearStoryFlag(0x5b0);
            return;
        }
        spawnRecords(stateAddress(TableId::EndScriptsSt5cSpawns), 1);
        soundPlayFlatForced(0x4f); // exe 0x5d5260
        room.handlerStep += 1u;
        return;
    case 1:
        if (!storyFlag(0x680)) return;
        scriptRun(0x6d);
        room.handlerStep += 1u;
        return;
    default: return;
    }
}

bool endScriptPorted(RoutineId handler) {
    return handler == RoutineId::EndScript75 || handler == RoutineId::EndScript6d || handler == RoutineId::EndScript3f || handler == RoutineId::EndScript6f ||
           handler == RoutineId::StaffRollScript;
}
bool endScriptTick(const PlayerEnv&, RoutineId handler) {
    if (!endScriptPorted(handler)) return false;
    switch (handler) {
    case RoutineId::EndScript75: script75(); break;
    case RoutineId::EndScript6d: script6d(); break;
    case RoutineId::EndScript3f: script3f(); break;
    case RoutineId::EndScript6f: script6f(); break;
    case RoutineId::StaffRollScript: staffRollScript(); break;
    default: break;
    }
    return true;
}

void staffRollSetHost(StaffRollHost host) { g_host = std::move(host); }

} // namespace od::port

// the ST5C case calls exe 0x5d5260 itself kHandlersTranscribed: 0x5d5260
