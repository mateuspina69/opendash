// ST33 (the fire ruin, story 13): script 0x2f, its guardian figure and the key item it drops (hypothesis names). The
// boss's death in the second fight (flag 0x4c) sets flag 0x4a; the room handler then runs script 0x2f -- the only
// spawner of the key item in ST33.
//
// The key item (static ST33 type 0x40 sub 0, exe 0x5b1cd0; states [0x8d072c]: 0 start 0x5b1d20, 1 run 0x5b1dd0, 2
// freed 0x5afdd0, 3 its fly 0x5b1e80 (the table runs into the sub-state table [0x8d0738])): its model, scale 0x80,
// thrown up (-0x280) and back (-0x80) with gravity 0x80; bounces twice, then spins. Touched: the item-get event
// (message 0x1e) and flag 0x3ed (the room handler then clears the door locks 0x713 / 0x710 / 0x712), freed.
//
// The figure (pool 0x20 ST33 type 0x45 sub 2, exe 0x4ccf50; states [0x89518c]: 0 bind 0x4cd010, 1 by sub-state
// 0x4cd120 [0x895198], 2 freed, 3 its step routine 0x4cd140): the guardian's glower (content/saul_kada/st33_guardian)
// with its own animation tail.
//
// Script 0x2f (exe 0x527360; states [0x8a5ca0]: 0 0x527380, 1 0x527470, 2 0x5274f0; camera 0x8a5b3c, steps
// 0x8a5c60).
#include "content/saul_kada/st33_key_script.hpp"
#include "content/saul_kada/st33_key_script_state.hpp"

#include "content/saul_kada/st33_guardian.hpp"

#include "engine/routine_ids.hpp"
#include "game/actor_animation.hpp"
#include "game/actor_model.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actor_stats.hpp"
#include "game/actor_sweeps.hpp"
#include "game/audio_logic.hpp"
#include "game/camera_state.hpp"
#include "game/collision.hpp"
#include "game/display.hpp"
#include "game/event.hpp"
#include "game/fade_state.hpp"
#include "game/hit_list.hpp"
#include "game/message.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/script_engine.hpp"
#include "game/script_state.hpp"
#include "game/spawn.hpp"
#include "game/stage_files.hpp"
#include "game/story.hpp"
#include "game/story_flags.hpp"
#include "game/transition.hpp"
#include "game/dat_names.hpp"

namespace od::port {

using namespace st33_key_script_state;

namespace {

// ---- the key item (exe 0x5b1cd0) ----------------------------------------------------------------------------------
enum class KeyState : uint8_t { Start = 0, Run = 1, Free = 2, Fly = 3 };
constexpr uint8_t kBouncesBeforeSpin = 2; // its step counts its bounces
constexpr uint8_t kKeyItemType = 0x40, kKeyItemVariant = 1;
constexpr uint32_t kKeyItemScale = 0x800080;
constexpr uint32_t kFlagKeyTaken = 0x3ed;
constexpr uint32_t kItemGetEvent = 0x20000; // the event's mode 2: an item got
constexpr uint16_t kItemGetMessage = 0x1e;

// exe 0x5b1e80: thrown, bouncing off the floor twice (half its fall back), then spinning
void keyFly(const PlayerEnv& env, StaticActor& actor) {
    if (actor.step >= kBouncesBeforeSpin) {
        actor.rotation.y = int16_t((uint16_t(actor.rotation.y) + 0x100u) & 0xfff);
        return;
    }
    integrateVelocity(actor, 0, actor.velocity.y, actor.velocity.z); // exe 0x5e3690
    actor.velocity.y = int16_t(uint16_t(uint16_t(actor.velocity.y) + uint16_t(actor.acceleration.y)));
    if (actor.velocity.y <= 0) return;
    if (!(actorPushOut(env, actor) & kWallOnY)) return; // exe 0x410960
    actor.velocity.y = int16_t(uint16_t(-(int32_t(actor.velocity.y) / 2)));
    ++actor.step;
}
// exe 0x5b1dd0: touched -> the item-get event, the flag, freed; else its fly and its hit entry
void keyRun(const PlayerEnv& env, StaticActor& actor, KeyItemFields& key) {
    if (key.hitBox.result & kHitAttackBump) {
        eventStartAtActor(actor, kItemGetEvent, 0, kItemGetMessage); // exe 0x543af0
        setStoryFlag(kFlagKeyTaken);                                 // exe 0x510e10
        freeActor(actor);                                            // 0x5afdd0 -> exe 0x5e1540
        return;
    }
    if (actor.subState == 0) keyFly(env, actor); // [0x8d0738]
    else notPortedAt(kStoryPort, "0x5b1cd0 sub-state (table 0x8d0738)", actor.subState);
    key.hitBox.previousX = actor.prevPos.x.whole;
    key.hitBox.previousZ = actor.prevPos.z.whole;
    key.hitBox.result = 0;
    const int16_t pos[3] = {actor.pos.x.whole, actor.pos.y.whole, actor.pos.z.whole};
    hitListSubmit(pos, key.hitBox, hitOwner(actor.slot) | hitGroup(kHitGroupHeavyThrown) | hitRadius(0x64),
                  kHitAttackStrong); // exe 0x5e4120
}
// exe 0x5b1d20: its model, its scale, the hit points of its row, thrown; then its first run at once
void keyStart(const PlayerEnv& env, StaticActor& actor, KeyItemFields& key) {
    if (!bindActorModel(actor, actor.variant)) { // exe 0x5e0bc0
        freeActor(actor);                        // exe 0x5e1540
        return;
    }
    actor.flags |= kActorDrawn;
    actor.airborne = 0;
    key.setScaleWords(kKeyItemScale);
    actor.tint = kTintNeutral;
    actor.onPlatform = 0;
    actor.collisionBox.pointAt(TableId::St33KeyScriptKeyItemBox);
    actor.wallHit = 0;
    actor.actorHit = 0;
    const uint16_t hp = hasStaticStats(actor.type) ? staticStats(actor.type, 0).hitPoints : 0;
    actor.maxHitPoints = int16_t(hp);
    actor.hitPoints = int16_t(hp);
    actor.velocity.y = int16_t(0xfd80);
    actor.acceleration.y = 0x80;
    actor.velocity.z = int16_t(0xff80);
    key.hitBox.result = 0;
    actor.subState = 0;
    ++actor.state;
    keyRun(env, actor, key);
}
void keyItem(const PlayerEnv& env, StaticActor& actor) { // exe 0x5b1cd0
    KeyItemFields& key = classState<KeyItemFields>(actor);
    actor.savePreviousPose();
    actor.savePreviousContact();
    switch (KeyState(actor.state)) {
    case KeyState::Start: keyStart(env, actor, key); return;
    case KeyState::Run: keyRun(env, actor, key); return;
    case KeyState::Free: freeActor(actor); return; // 0x5afdd0 -> exe 0x5e1540
    case KeyState::Fly: keyFly(env, actor); return; // [0x8d072c + 3 * 4] = 0x5b1e80 (the sub-state table follows)
    default: notPortedAt(kStoryPort, "0x5b1cd0 state (table 0x8d072c)", actor.state);
    }
}
// a key item (variant 1) at the script's place for it: sweep group 2, its rotation 0 but the yaw
uint32_t spawnKeyItem(int16_t yaw) {
    const uint32_t slot = allocActor60(kActorStageClass); // exe 0x5e12f0
    if (slot == 0) return 0;
    StaticActor& key = staticActorAt(slot);
    key.flags = kActorLive;
    key.loopFlags |= kActorSweepGroup2;
    key.type = kKeyItemType;
    key.subtype = 0;
    key.variant = kKeyItemVariant;
    key.spawnByte = 0;
    key.rotation.z = 0;
    key.rotation.x = 0;
    key.rotation.y = yaw;
    return slot;
}

// ---- the figure (exe 0x4ccf50) ------------------------------------------------------------------------------------
enum class FigureState : uint8_t { Bind = 0, BySubState = 1, Free = 2, Step = 3 };
void figure(AnimatedActor& actor) {
    const GuardianFields& pose = classState<GuardianFields>(actor);
    actor.prevAnim = actor.anim;
    actor.tint = kTintNeutral;
    actor.prevPos = actor.pos;
    actor.prevRotation = actor.rotation;
    actor.savePreviousContact();
    switch (FigureState(actor.state)) {
    case FigureState::Bind: bindGlowerGuardian(actor); break; // exe 0x4cd010
    case FigureState::BySubState:                             // exe 0x4cd120: [0x895198]
        if (actor.subState != 0) {
            notPortedAt(kStoryPort, "0x4cd120 sub-state (table 0x895198)", actor.subState);
            return;
        }
        glowerGuardianStep(actor); // exe 0x4cd140
        break;
    case FigureState::Free: freeActor(actor); break; // 0x468fa0 -> exe 0x5e1490
    case FigureState::Step: glowerGuardianStep(actor); break;
    default: notPortedAt(kStoryPort, "0x4ccf50 state (table 0x89518c)", actor.state); return;
    }
    if (actor.anim == actor.prevAnim) {
        if (pose.glowerHoldPose & 1) animDecodeCurrent(actor); // exe 0x5e1f30
        else animStep(actor);                            // exe 0x5e1ea0
    } else {
        animSet(actor, actor.anim, pose.glowerAnimKey); // exe 0x5e1c50
    }
    animPose(actor); // exe 0x5e2d90
}

// ---- script 0x2f (exe 0x527360) ----------------------------------------------------------------------------------
enum class KeySetUp : uint8_t { Start = 0, Place = 1, WaitFiles = 2, Voice = 3, WaitVoice = 4 };
enum class KeyWrapUp : uint8_t { FadeVoice = 0, WaitVoice = 1, WaitMusic = 2, Load = 3, End = 4, SkipFade = 5,
                                 SkipWait = 6 };
constexpr RoutineId kFallStep = RoutineId::St33KeyScriptFallStep, kTurnStep = RoutineId::St33KeyScriptTurnStep, kDropStep = RoutineId::St33KeyScriptDropStep, kPlaceStep = RoutineId::St33KeyScriptPlaceStep;
constexpr uint32_t kFigure = 0; // the script's actor: the guardian figure (record 0x8a5ad0)
constexpr uint32_t kDropBone = 3;
constexpr int16_t kDropYaw = 0x200;
constexpr uint16_t kRuinMusic = 0x3c;
constexpr DatFile kKeyDat{"ST3301.DAT"};
constexpr uint8_t kSkipFade = 0x22, kWrapUpFade = 0x14, kEndFade = 1;
constexpr uint32_t kVoiceFade = 8;

bool fileLoading() { return (fileLoads().loading & kFileLoading) != 0; }
bool streamBusy() { return (fileLoads().loading & kStreamBusy) != 0; }
void setPlayerPos(uint32_t x, uint32_t y, uint32_t z, int16_t yaw) {
    Player& p = player();
    p.pos.x = int32_t(x);
    p.pos.y = int32_t(y);
    p.pos.z = int32_t(z);
    p.rotation.y = yaw;
}

void keyStep(const RoutineRef& routine) {
    ScriptState& s = scriptState();
    const int32_t t = s.stepTick;
    const uint32_t cast = slotIdentityWord(s.actors[kFigure]);
    switch (routine.id()) {
    case kFallStep:
        if (t == 0xa) {
            GuardianParams(animatedActorAt(cast)).signal = 1;
            soundFadeOut(kMusicFades, 0xcc, 0); // exe 0x5d5780
        } else if (t == 0x50) {
            scriptSequenceSkip(1); // exe 0x5115c0
            actorHeaderAt(cast).step = uint8_t(GlowerStep::Turn);
        }
        return;
    case kTurnStep:
        if (t == 0x3c) {
            scriptSequenceSkip(1);
            actorHeaderAt(cast).step = uint8_t(GlowerStep::Turn2);
        }
        return;
    case kDropStep: // the key item dropped from the figure's bone 3
        if (t == 3) {
            if (const uint32_t slot = spawnKeyItem(kDropYaw)) {
                static constexpr int16_t kOffset[3] = {0, 0, -0x80};
                int16_t point[3];
                bonePointOffset(animatedActorAt(cast), kDropBone, kOffset, point); // exe 0x5e1970
                StaticActor& key = staticActorAt(slot);
                key.pos.x = int32_t(uint32_t(int32_t(point[0])) << 16);
                key.pos.y = int32_t(uint32_t(int32_t(point[1])) << 16);
                key.pos.z = int32_t(uint32_t(int32_t(point[2])) << 16);
                key.step = 0;
            }
        }
        return;
    case kPlaceStep:
        if (t == 1) setPlayerPos(0xfc000000, 0, 0xf8000000, 0x800);
        return;
    default: notPortedAt(kStoryPort, "script 0x2f step", routine.pointerValue());
    }
}
void keyDrop() {
    ScriptState& s = scriptState();
    switch (s.cutsceneState()) {
    case CutsceneState::SetUp: // exe 0x527380
        switch (KeySetUp(s.subState)) {
        case KeySetUp::Start:
            scriptReplayPadOn();                // exe 0x5117f0
            windowCloseAll(WindowClose::Force); // exe 0x5577d0
            ++s.subState;
            return;
        case KeySetUp::Place:
            actorsSweep(kSweepActors, kSweepFree);   // exe 0x5e05d0
            scriptSequenceStart(stateAddress(TableId::St33KeyScriptKeyDropCamera), stateAddress(TableId::St33KeyScriptKeyDropSteps)); // exe 0x511390
            scriptActorSpawnRecord(stateAddress(TableId::St33KeyScriptKeyDropFigureRecord));        // exe 0x511740: the figure
            setPlayerPos(0xee000000, 0, 0, 0xc00);
            scriptSequenceTick(); // exe 0x5114d0
            ++s.subState;
            return;
        case KeySetUp::WaitFiles:
            if (voiceClipIdle() && !fileLoading()) s.subState = uint8_t(KeySetUp::Voice); // exe 0x601260
            return;
        case KeySetUp::Voice:
            if (!streamBusy()) {
                voiceClipPlay(0x15); // exe 0x601050
                s.subState = uint8_t(KeySetUp::WaitVoice);
            }
            return;
        case KeySetUp::WaitVoice:
            if (voiceClipPlaying()) s.enterState(CutsceneState::Steps); // exe 0x601240
            return;
        default: return;
        }
    case CutsceneState::Steps: { // exe 0x527470 (the skip: fade 0x22, the wrap-up from its skip fade)
        const ScriptStep step = scriptStepAt(s.steps.value());
        if (step.id == kScriptStepEnd) {
            s.enterState(CutsceneState::WrapUp);
            soundStopSfxVoices(); // exe 0x5d59b0
            scriptSequenceTick();
            return;
        }
        if (const RoutineRef routine = stateRoutine(step.routine)) keyStep(routine);
        if (scriptSkipRequested() && display().drawBuffer != 0) { // exe 0x511790
            scriptFade(kSkipFade, 0);                              // exe 0x53f6b0
            s.enterState(CutsceneState::WrapUp);
            s.subState = uint8_t(KeyWrapUp::SkipFade);
            soundStopSfxVoices();
            soundFadeOut(kMusicFades, 0xfff, 0);
        }
        scriptSequenceTick();
        return;
    }
    case CutsceneState::WrapUp: // exe 0x5274f0
        switch (KeyWrapUp(s.subState)) {
        case KeyWrapUp::FadeVoice:
        case KeyWrapUp::SkipFade:
            if (voiceClipPlaying()) voiceClipFadeOut(kVoiceFade); // exe 0x601220
            ++s.subState;
            return;
        case KeyWrapUp::WaitVoice:
            if (voiceClipIdle() && fadeState().busy == 0) {
                scriptFade(kWrapUpFade, 0);
                soundFadeOut(kMusicFades, 0xfff, 0);
                ++s.subState;
            }
            return;
        case KeyWrapUp::WaitMusic:
            if (fadeState().busy == 0 && soundDone(kMusicFades)) ++s.subState; // exe 0x5d5900
            return;
        case KeyWrapUp::Load:
            datLoadRequest(kKeyDat); // exe 0x40efe0
            ++s.subState;
            return;
        case KeyWrapUp::End:
            if (fileLoading()) return;
            soundPlayFlatForced(kRuinMusic); // exe 0x5d5260
            scriptFade(kEndFade, 0);
            actorsSweep(kSweepActors, kSweepFree);
            spawnRecords(stateAddress(TableId::St33KeyScriptKeyDropEndSpawns), 3); // exe 0x511090
            spawnRecords(stateAddress(TableId::St33KeyScriptKeyDropEndMoreSpawns), 1);
            if (const uint32_t slot = spawnKeyItem(0)) {
                StaticActor& key = staticActorAt(slot);
                key.pos.x = int32_t(0xfc150000);
                key.pos.y = int32_t(0xffc00000);
                key.pos.z = int32_t(0xfa950000);
                key.step = kBouncesBeforeSpin; // already landed: spinning
            }
            scriptReplayPadOff(); // exe 0x511890
            setPlayerPos(0xfc000000, 0, 0xf8000000, 0x800);
            player().rotation.x = 0;
            player().rotation.z = 0;
            cameraState().lookPitch = 0;
            scriptEndBlock(); // exe 0x5116a0
            return;
        case KeyWrapUp::SkipWait:
            if (voiceClipIdle() && fadeState().busy == 0) {
                soundFadeOut(kMusicFades, 0xfff, 0);
                s.subState = uint8_t(KeyWrapUp::WaitMusic);
            }
            return;
        default: return;
        }
    default: notPortedAt(kStoryPort, "script 0x2f state (table 0x8a5ca0)", s.state);
    }
}

} // namespace

void keyItemUpdate(const PlayerEnv& env, uint32_t actor) { keyItem(env, staticActorAt(actor)); }
void keyScriptFigureUpdate(const PlayerEnv&, uint32_t actor) { figure(animatedActorAt(actor)); }
bool keyDropScriptPorted(RoutineId handler) { return handler == RoutineId::St33KeyDropScript; }
bool keyDropScriptTick(const PlayerEnv&, RoutineId handler) {
    if (!keyDropScriptPorted(handler)) return false;
    keyDrop();
    return true;
}

} // namespace od::port
