// ST11's script 0x2a (the arrival on the Forbidden Island) and its cast, one routine per exe routine (addresses in the
// comments); the names are hypotheses from the observed behaviour. Render output (the shadow list exe 0x5f20f0, the
// white screen exe 0x5f30e0, the OT sprites of the menus) is left out as in the other cast ports.
//
// Script 0x2a (exe 0x525680) runs in ST11 sub 0 (content/forbidden_island/island_visit, room handler 0x57e180, story
// 1): the arrival on the Forbidden Island -- the exit lights 0x5aa060, the cast 0x4b57b0, voice 0x11 + DAT 0x77; its
// end spawns the records 0x8a4a58 x 3 (the fight actor 0x4b3b10 + 2 lights).
#include "content/forbidden_island/st11_script2a.hpp"
#include "content/forbidden_island/st11_script2a_state.hpp"

#include "game/actor_animation.hpp"
#include "game/actor_model.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actor_sweeps.hpp"
#include "game/audio_logic.hpp"
#include "game/collision.hpp"
#include "game/cutscene_actor.hpp"
#include "game/display.hpp"
#include "game/dust_puff.hpp"
#include "game/fade_state.hpp"
#include "game/global_class_types.hpp"
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
#include "game/transition.hpp"

namespace od::port {

using namespace st11_script2a_state;

namespace {

// the tick's case of the switches exe 0x5259f0 (its byte table, exe 0x525a04) and exe 0x4b5a44 (exe 0x4b5a58)
const DataTable<ScalarTable<uint8_t, 0x8d>> flashTickCases{TableId::St11ArrivalFlashTickCases};
const DataTable<ScalarTable<uint8_t, 0x47>> castTickCases{TableId::St11ArrivalCastTickCases};

constexpr uint32_t kSweepScriptActors = 0xefe, kSweepKeep = 0x80; // exe 0x5e05d0's arguments of the scene
constexpr uint32_t kVoiceFadeStep = 8;
constexpr uint32_t kMusicFade = 0xf;
constexpr int16_t kMusicFadeStep = 0x333;

ScriptState& block() { return scriptState(); }

// ---- the player's places --------------------------------------------------------------------------------------------
// the player placed at the scene's start (the inlined copies of the state-0 routines): the move direction's angles and
// the idle faces cleared, the position (16.16), the yaw
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

// ---- the steps (list 0x8a4bb8) ----------------------------------------------------------------------------------------
// exe 0x5356e0: a pool-e0 controller type 0xe variant 1 in the late loop: the screen flash
constexpr uint8_t kScreenFlashType = 0xe;
void spawnScreenFlash() {
    const uint32_t address = allocActorE0(); // exe 0x5e1440
    if (address == 0) return;
    ControllerActor& c = controllerActorAt(address);
    c.flags = kActorLive;
    c.type = kScreenFlashType;
    c.variant = 1;
    c.loopFlags |= kActorLateLoop;
}

// the phases of the walk-in step (the script's sub-state)
enum class WalkInPhase : uint8_t { Wait = 0, Walk = 1, LoadVoice = 2, WaitLoad = 3, WaitStream = 4, StartVoice = 5 };
constexpr uint32_t kVoiceDat = 0x77, kVoiceArrival = 0x11;
// exe 0x525890: at 10 the player walks on the replay pad (its forward button), released at 0x48; once the music is
// quiet the voice's DAT, then the voice; when it plays the skip back and the next step
void stepWalkIn(int32_t t) {
    ScriptState& s = block();
    switch (WalkInPhase(s.subState)) {
    case WalkInPhase::Wait:
        if (t == 10) {
            playerBackToPlay(); // exe 0x5723c0
            replayPad().held = player().button(PlayerButton::Forward);
            s.subState = uint8_t(WalkInPhase::Walk);
        }
        return;
    case WalkInPhase::Walk:
        if (t == 0x48) {
            replayPad().held = 0;
            s.subState = uint8_t(WalkInPhase::LoadVoice);
        }
        return;
    case WalkInPhase::LoadVoice:
        if (voiceClipIdle() && soundDone(kMusicFade)) {
            datLoadRequest(kVoiceDat);
            s.subState = uint8_t(WalkInPhase::WaitLoad);
        }
        return;
    case WalkInPhase::WaitLoad:
        if (!(fileLoads().loading & kFileLoading)) s.subState = uint8_t(WalkInPhase::WaitStream);
        return;
    case WalkInPhase::WaitStream:
        if (!(fileLoads().loading & kStreamBusy)) {
            voiceClipPlay(kVoiceArrival);
            s.subState = uint8_t(WalkInPhase::StartVoice);
        }
        return;
    case WalkInPhase::StartVoice:
        if (voiceClipPlaying()) {
            s.flags &= uint8_t(~kScriptUnskippable);
            scriptSequenceSkip(1);
            s.subState = 0;
        }
        return;
    default: return;
    }
}

// the cast's cue (its parameter byte, set by the flash step)
constexpr uint8_t kCastCueFlash = 0x20, kCastCueNone = 0, kCastCueSecondFlash = 0x80;
constexpr uint16_t kSoundArrival = 0x1e;
// exe 0x525970: the flashes and the cast's cues by the tick table 0x525a04 (ticks 0x28..0xb4); then, once the player
// is script driven again, the arrival's sound (stepPhase 1: played) and the next step
void stepFlashes(int32_t t) {
    ScriptState& s = block();
    if (s.subState == 0) {
        if (uint32_t(t - 0x28) > 0x8c) return;
        // the tick's case of the switch exe 0x5259f0 (its byte table, exe 0x525a04)
        const auto cue = [&s](uint8_t c) { (*s.actors[0]).params[0] = c; }; // the script's actor 0
        switch (flashTickCases[uint32_t(t - 0x28)]) {
        case 0:
            spawnScreenFlash();
            cue(kCastCueFlash);
            return;
        case 1: cue(kCastCueNone); return;
        case 2:
            spawnScreenFlash();
            cue(kCastCueSecondFlash);
            return;
        case 3: ++s.subState; return;
        default: return;
        }
    }
    if (s.subState == 1 && playerToScriptIfFree() == 0) {
        soundPlayFlatForced(kSoundArrival);
        s.stepPhase = 1;
        scriptSequenceSkip(1);
    }
}
// exe 0x525aa0: the player's stage animations 0x81 / 0x82; at 0x78 the next step
void stepLooks(int32_t t) {
    if (t == 10) playerScriptAnimId(0x81, 0, 1);
    else if (t == 0x5a) playerScriptAnimId(0x82, 0, 1);
    else if (t == 0x78) scriptSequenceSkip(1);
}
void runStep(const RoutineRef& routine) {
    const int32_t t = block().stepTick;
    switch (routine.id()) {
    case RoutineId::St11Script2aWalkInStep: stepWalkIn(t); return;
    case RoutineId::St11Script2aFlashesStep: stepFlashes(t); return;
    case RoutineId::St11Script2aLooksStep: stepLooks(t); return;
    case RoutineId::EmptyRoutine: return; // the empty step
    default: notPortedAt(kStoryPort, "script 0x2a step", routine.pointerValue());
    }
}

// ---- the states (exe table 0x8a4be0) --------------------------------------------------------------------------------
// exe 0x5256a0: the replay pad, the windows closed, the skip locked; once the player is script driven the scripted
// actors swept, the player at (0xfb1, -0x37b, 0x3a3) facing a half turn, the music faded, the camera script and the
// steps
void setUp() {
    ScriptState& s = block();
    if (s.subState == 0) {
        scriptReplayPadOn();
        windowCloseAll(WindowClose::Force);
        s.subState = 1;
        s.flags |= kScriptUnskippable;
    } else if (s.subState != 1) {
        return;
    }
    if (playerToScriptIfFree() != 0) return;
    actorsSweep(kSweepScriptActors, kSweepKeep);
    placePlayer(0xfb10000, int32_t(0xfc850000u), 0x3a30000, 0x800);
    soundFadeOut(kMusicFade, kMusicFadeStep, 0);
    scriptSequenceStart(stateAddress(TableId::St11Script2aCamera), stateAddress(TableId::St11Script2aSteps));
    scriptSequenceTick();
    s.state = uint8_t(CutsceneState::Steps);
    s.subState = 0;
    s.stepPhase = 0;
}
// exe 0x525760: the step's routine (called unchecked); the list's end or a skip (on a frame of buffer 1) -> the skip's
// wipe, the wrap-up; the sequence tick
void steps() {
    ScriptState& s = block();
    const ScriptStep step = scriptStepAt(s.steps.value());
    if (step.id != kScriptStepEnd) {
        if (const RoutineRef routine = stateRoutine(step.routine)) runStep(routine);
        if (scriptSkipRequested() == 0 || display().drawBuffer == 0) {
            scriptSequenceTick();
            return;
        }
        scriptFade(kScriptSkipWipe, 0);
    }
    s.enterStateKeepingPhases(uint8_t(CutsceneState::WrapUp));
    scriptSequenceTick();
}
// exe 0x5257b0: the voice faded; once it and the fade are idle (a skip fades 1) the scripted actors swept, the records
// 0x8a4a58 x 3 (the fight actor 0x4b3b10 + the exit lights), the arrival's sound (unless the step played it), the
// player back at (0x900, -0x2a0, 0x1600) facing a half turn, the block ended
void wrapUp() {
    ScriptState& s = block();
    switch (s.subState) {
    case 0:
        if (voiceClipPlaying()) voiceClipFadeOut(kVoiceFadeStep);
        ++s.subState;
        return;
    case 1:
        if (voiceClipIdle() && fadeState().idle()) ++s.subState;
        return;
    case 2: {
        if (s.flags & kScriptSkipTaken) scriptFade(1, 0);
        actorsSweep(kSweepScriptActors, kSweepKeep);
        spawnRecords(stateAddress(TableId::St11Script2aFightSpawns), 3);
        if (s.stepPhase == 0) soundPlayFlatForced(kSoundArrival);
        playerBackToPlay();
        scriptReplayPadOff();
        Player& p = player();
        p.airborne = 0;
        p.pos.x = 0x9000000;
        p.pos.y = int32_t(0xfd600000u);
        p.pos.z = 0x16000000;
        p.rotation.y = 0x800;
        scriptEndBlock();
        return;
    }
    default: return;
    }
}
void script2a() {
    switch (block().cutsceneState()) {
    case CutsceneState::SetUp: setUp(); return;
    case CutsceneState::Steps: steps(); return;
    case CutsceneState::WrapUp: wrapUp(); return;
    default: notPortedAt(kStoryPort, "script 0x2a state", block().state);
    }
}

// ==== the cast ====================================================================================================
// The class tails below run after a state that may have freed the actor (no model in the room bank: the exe goes on
// with the freed actor's animation); the port stops there instead (the actor is gone either way).

enum class ArrivalCastState : uint8_t { Bind = 0, Run = 1, Free = 2 };
enum class ArrivalCastPhase : uint8_t { Wait = 0, Watch = 1, Jump = 2, WalkOff = 3 };
enum class JumpPhase : uint8_t { TakeOff = 0, Rise = 1, Fall = 2, Land = 3 };
constexpr uint16_t kSoundStep = 0x16b, kSoundJump = 0x16c, kSoundLand = 0x170;
constexpr int16_t kJumpSpeedY = -0x140, kJumpSpeedZ = -0x200, kJumpGravity = 0x40, kWalkOffSpeed = -0x300;

// exe 0x4b5aa0: the box push-out (back to the previous position when blocked) and the ground: on the ground a drop of
// more than 0x10 returns 1 (falling, the height kept); airborne it lands on a floor / ceiling contact
int moveOnGround(const PlayerEnv& env, AnimatedActor& a) {
    uint32_t pushed = 0;
    if (int32_t(actorTestMove(env, a)) < 0) { // exe 0x40f570
        pushed = actorPushOut(env, a);        // exe 0x410960
        if (int32_t(pushed) < 0) a.pos = a.prevPos;
    }
    const int g = actorGround(env, a); // exe 0x40f190
    if (a.airborne != 0) {
        if (!(pushed & kWallOnY)) return 0;
        a.airborne = 0;
    } else if (g - int(a.pos.y.whole) > 0x10) {
        return 1;
    }
    a.pos.y.whole = int16_t(g);
    return 0;
}
// exe 0x4b5b20: the jump -- the take-off (animation 0xf, its sound, airborne, velocity (0, -0x140, -0x200) and gravity
// 0x40, the first move), animation 0x10 once falling, the landing (animation 0x11, its sound); 1 once the landing's
// animation ended. Rising and falling it moves by the velocity along the yaw and adds the gravity.
int jump(AnimatedActor& a, ArrivalCastFields& fields) {
    switch (JumpPhase(fields.jumpPhase)) {
    case JumpPhase::TakeOff:
        a.anim = 0xf;
        soundPlayAtPos(kSoundJump, a.pos);
        a.airborne = kAirborneFalling;
        ++fields.jumpPhase;
        a.velocity.x = 0;
        a.velocity.y = kJumpSpeedY;
        a.velocity.z = kJumpSpeedZ;
        a.acceleration.y = kJumpGravity;
        moveAlongYaw(a.pos, a.rotation.y, 0, kJumpSpeedY, kJumpSpeedZ); // exe 0x5e36f0
        return 0;
    case JumpPhase::Rise:
        if (a.velocity.y >= 1) {
            a.anim = 0x10;
            ++fields.jumpPhase;
        }
        break;
    case JumpPhase::Fall:
        if (a.airborne == 0) {
            a.anim = 0x11;
            soundPlayAtPos(kSoundLand, a.pos);
            ++fields.jumpPhase;
            return 0;
        }
        break;
    case JumpPhase::Land: return a.animEnded() ? 1 : 0;
    default: return 0;
    }
    moveAlongYaw(a.pos, a.rotation.y, 0, a.velocity.y, a.velocity.z);
    a.velocity.y += a.acceleration.y;
    return 0;
}
// the dust puff of the jump (global effect type 7, late loop)
void spawnJumpDust() {
    const uint32_t address = allocActorA0(0); // exe 0x5e13b0
    if (address == 0) return;
    EffectActor& e = effectActorAt(address);
    DustPuffFields& puff = classState<DustPuffFields>(e);
    e.flags = kActorLive;
    e.type = typeByte(GlobalEffectType::DustPuff);
    e.loopFlags |= kActorLateLoop;
    e.pos.x = 0xb50;
    e.pos.y = -0x480;
    e.pos.z = 0x2750;
    puff.size = 0x100;
    puff.ticks = 0x10;
    puff.airborne = 0;
}
// exe 0x4b58e0: the run
void runCast(const PlayerEnv& env, AnimatedActor& a, ArrivalCastFields& fields) {
    moveOnGround(env, a);
    const ScriptState& s = block();
    const int32_t t = s.stepTick;
    switch (ArrivalCastPhase(a.subState)) {
    case ArrivalCastPhase::Wait:
        if (s.stepSub == 3 && t == 0xa0) {
            a.anim = 0;
            ++a.subState;
        }
        return;
    case ArrivalCastPhase::Watch: {
        if (uint32_t(t - 0xbe) > 0x46) return;
        // the tick's case of the switch exe 0x4b5a44 (its byte table, exe 0x4b5a58)
        switch (castTickCases[uint32_t(t - 0xbe)]) {
        case 0: a.anim = 4; return;
        case 1: fields.animating = 0; return;
        case 2: fields.animating = 1; return;
        case 3:
            a.step = 0;
            ++a.subState;
            return;
        default: return;
        }
    }
    case ArrivalCastPhase::Jump:
        if (t == 0x10e) fields.shadow = 1;
        if (t == 0x11d) spawnJumpDust();
        if (jump(a, fields)) {
            a.anim = 0x20;
            a.velocity.z = kWalkOffSpeed;
            ++a.subState;
        }
        return;
    case ArrivalCastPhase::WalkOff:
        moveAlongYaw(a.pos, a.rotation.y, 0, 0, a.velocity.z);
        if (a.animKey.events != 0) soundPlayAtPos(kSoundStep, a.pos);
        return;
    default: return;
    }
}
void arrivalCast(const PlayerEnv& env, AnimatedActor& a) {
    ArrivalCastFields& fields = classState<ArrivalCastFields>(a);
    a.prevAnim = a.anim;
    a.tint = kTintNeutral;
    a.savePreviousPose();
    a.savePreviousContact();
    switch (ArrivalCastState(a.state)) {
    case ArrivalCastState::Bind:
        if (!bindActorModel(a, 0)) {
            freeActor(a);
            return;
        }
        a.anim = 1;
        a.flags |= kActorDrawn;
        a.prevAnim = kNoAnimShown;
        a.velocity.setXY(0, 0);
        a.velocity.setZSpare(0, 0);
        a.rotation.z = 0;
        a.rotation.x = 0;
        a.collisionBox.pointAt(TableId::St11Script2aArrivalCastBox);
        a.airborne = 0;
        a.onPlatform = 0;
        a.platform = nullptr;
        a.actorHit = 0;
        fields.animating = 1;
        fields.shadow = 0;
        a.enterState(uint8_t(ArrivalCastState::Run));
        break;
    case ArrivalCastState::Run: runCast(env, a, fields); break;
    case ArrivalCastState::Free: freeActor(a); return; // exe 0x468fa0
    default: notPortedAt(kStoryPort, "0x4b57b0 state (table 0x89464c has 3)", a.state);
    }
    if (a.flags & kActorLive) {
        if (a.anim != a.prevAnim) animSetPose(a, a.anim, 0);
        else if (fields.animating != 0) animStepPose(a);
    }
    // drawn with fields.shadow: the shadow list entry (exe 0x5f20f0, render output)
}

} // namespace

bool st11Script2aPorted(uint8_t id) { return storyScriptHandler(id).id() == RoutineId::St11Script2a; }

bool st11Script2aTick(const PlayerEnv&, uint8_t id) {
    if (storyScriptHandler(id).id() != RoutineId::St11Script2a) return false;
    script2a();
    return true;
}

void script2aArrivalCastUpdate(const PlayerEnv& env, uint32_t actor) { arrivalCast(env, animatedActorAt(actor)); }

} // namespace od::port
