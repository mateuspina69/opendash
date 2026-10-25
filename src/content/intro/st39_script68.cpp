// ST39's script 0x68 and its cast (see st39_script68.hpp). The room case of ST39 (content/intro/opening_script) sets the
// revisit's flag and runs it on story 0x11 room 1.
//   state 0 (exe 0x5397f0): the replay pad, the player scripted at (0x80, 0, 0x40) in its stage animation 0, the DAT
//           loader idle, voice clip 0x1d; once it plays the camera script and its steps, the fade in, state 1
//   state 1 (exe 0x5398e0): 0x15e ticks on the script's own clock, then the fade out and the wait for it; start
//           skips (the skip's wipe)
//   state 2 (exe 0x539970): the vibration off, the actors swept, the windows closed, the voice faded; once it is idle
//           the request to ST1F room 0 at (0x40, -0x231, -0x900) facing a quarter turn; then the player back, the
//           block ended
// The cast stops its tail once a state freed it (no model in the room's bank), where the exe goes on with the freed
// actor's animation. The shadow list entries of the cast (exe 0x5f20f0) are render output; their ground
// probe is not run (an approximation). The animation numbers are the classes' model-bank animations.
#include "content/intro/st39_script68.hpp"
#include "content/intro/opening_cast_state.hpp"
#include "content/intro/st39_script68_state.hpp"

#include "engine/data_source.hpp"
#include "game/actor_animation.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actor_sweeps.hpp"
#include "game/audio_logic.hpp"
#include "game/cutscene_actor.hpp"
#include "game/display.hpp"
#include "game/fade.hpp"
#include "game/fade_state.hpp"
#include "game/keyframe_tracks.hpp"
#include "game/message.hpp"
#include "game/player/player_input.hpp"
#include "game/player/player_script_control.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/script_engine.hpp"
#include "game/script_state.hpp"
#include "game/stage_files.hpp"
#include "game/story.hpp"
#include "game/transition.hpp"
#include "game/transition_state.hpp"

using namespace od::port::st39_script68_state;

namespace od::port {

using namespace st39_script68_state;

namespace {

// the states (exe table 0x8acd60: a routine per state; the named routines of engine/routines.csv)
const DataTable<RoutineTable<3>> script68States{TableId::St39Script68States};

// the running script's bytes as script 0x68's
Script68Bytes& script68Bytes() { return scriptState().part<Script68Bytes>(); }
constexpr int32_t kFadeOutTick = 0x15e;
constexpr int32_t kCastAnimTick = 0x81; // the cast member's second animation

enum class SetUpSub : uint8_t { Start = 0, TakeOver = 1, WaitFile = 2, WaitStream = 3, WaitVoice = 4 };
enum class TimerSub : uint8_t { Start = 0, Running = 1, FadingOut = 2 };
enum class WrapUpSub : uint8_t { Stop = 0, WaitVoice = 1, WaitRequest = 2 };

constexpr uint32_t kRevisitVoice = 0x1d;
constexpr uint32_t kVoiceFade = 8;
constexpr uint32_t kVibrationStop = 0x1000000;
constexpr uint8_t kStageAnim0 = 0x80; // the player's stage animation 0 (the stage bank's)
constexpr int32_t kPlayerX = 0x800000, kPlayerZ = 0x400000;
constexpr uint8_t kFadeIn = kFadeInRequest | 2, kFadeOut = kFadeOutRequest | 2;
constexpr uint8_t kST1F = 0x1f;

// exe 0x5397f0
void script68SetUp(ScriptState& s) {
    switch (SetUpSub(s.subState)) {
    case SetUpSub::Start:
        scriptReplayPadOn(); // exe 0x5117f0
        ++s.subState;
        [[fallthrough]];
    case SetUpSub::TakeOver: {
        if (playerToScriptIfFree() != 0) return; // exe 0x572300
        Player& p = player();
        p.pos.x = kPlayerX;
        p.pos.z = kPlayerZ;
        p.idleFaces[0] = 0;
        p.idleFaces[1] = 0;
        p.pos.y = 0;
        p.clearDirectionOffsetWords();
        playerScriptAnimId(kStageAnim0, 0, true); // exe 0x5723d0
        ++s.subState;
        return;
    }
    case SetUpSub::WaitFile:
        if (fileLoads().loading & kFileLoading) return;
        ++s.subState;
        return;
    case SetUpSub::WaitStream:
        if (fileLoads().loading & kStreamBusy) return;
        voiceClipPlay(kRevisitVoice); // exe 0x601050
        ++s.subState;
        return;
    case SetUpSub::WaitVoice:
        if (!voiceClipPlaying()) return;
        scriptSequenceStart(stateAddress(TableId::St39Script68CameraScript), stateAddress(TableId::St39Script68Steps)); // exe 0x511390
        scriptFade(kFadeIn, 0);                     // exe 0x53f6b0
        s.enterState(CutsceneState::Steps);
        return;
    default: return;
    }
}

// exe 0x5398e0
void script68Timer(ScriptState& s) {
    Script68Bytes& bytes = script68Bytes();
    const TimerSub sub = TimerSub(s.subState);
    bool toEnd = false;
    if (sub == TimerSub::Start || sub == TimerSub::Running) {
        if (sub == TimerSub::Start) {
            bytes.clock = 0;
            s.subState = uint8_t(TimerSub::Running);
        }
        if (bytes.clock == kFadeOutTick) {
            ++s.subState;
            scriptFade(kFadeOut, 0);
            s.flags |= kScriptUnskippable;
        }
        if (scriptSkipRequested() != 0 && display().drawBuffer != 0) { // exe 0x511790
            scriptFade(kScriptSkipWipe, 0);
            toEnd = true;
        }
    } else if (sub == TimerSub::FadingOut && fadeState().idle()) {
        toEnd = true;
    }
    if (toEnd) s.enterState(CutsceneState::WrapUp);
    scriptSequenceTick(); // exe 0x5114d0
    bytes.clock += 1u;
}

// exe 0x539970
void script68WrapUp(ScriptState& s) {
    switch (WrapUpSub(s.subState)) {
    case WrapUpSub::Stop:
        padVibrate(0, kVibrationStop);   // exe 0x55cd10
        playerScriptAnimId(0, 0, true);
        actorsSweep(kSweepActors, kSweepFree); // exe 0x5e05d0
        windowCloseAll(WindowClose::Gentle);   // exe 0x5577d0
        if (voiceClipPlaying()) voiceClipFadeOut(kVoiceFade); // exe 0x601220
        ++s.subState;
        [[fallthrough]];
    case WrapUpSub::WaitVoice:
        if (!voiceClipIdle()) return; // exe 0x601260
        ++s.subState;
        requestTransition(kTransitionAccepted, kST1F, 0, 0x40, -0x231, -0x900, kQuarterTurn, kFadeIn, kNoFade);
        return;
    case WrapUpSub::WaitRequest:
        if (transitionRequest().state != kTransitionNone) return;
        playerBackToPlay();   // exe 0x5723c0
        scriptReplayPadOff(); // exe 0x511890
        player().setDrawn(true);
        scriptEndBlock(); // exe 0x5116a0
        return;
    default: return;
    }
}

// exe 0x5397d0: the state's routine
void script68() {
    ScriptState& s = scriptState();
    switch (script68States->idAt(s.state)) {
    case RoutineId::St39SetUpState: script68SetUp(s); return;
    case RoutineId::St39TimerState: script68Timer(s); return;
    case RoutineId::St39WrapUpState: script68WrapUp(s); return;
    default: notPortedAt(kStoryPort, "script 0x68 state", s.state);
    }
}

// ---- the cast member (exe 0x44b940, ST39 pool 0x20 type 2 sub 0; the camera script's record 0x8accd0)
// ----------------------
// one variant (exe table 0x88e668: 0x44ba00). State 0 binds it (box 0x88e670, animation 0, the motion cleared), its
// texture tracks on their tables with both cells stopped at 0; states 0 / 1 ask animation 1 once the script's clock
// reaches 0x81. Its tail: the texture tracks, the animation (held by kCastHoldPose | kCastShadow, as the exe tests).
const DataTable<RoutineTable<1>> castVariants{TableId::St39Script68CastVariants};
constexpr uint8_t kCastHold = kCastHoldPose | kCastShadow;
enum class CastState : uint8_t { Init = 0, Wait = 1 };

// exe 0x44ba00: false once the state 0 freed it
bool castMember(AnimatedActor& a, CastFields& c) {
    if (CastState(a.state) == CastState::Init) {
        if (!castBind(a, stateAddress(TableId::St39Script68CastBox), 0, true)) return false; // exe 0x5e0ba0
        c.animKey = 0;
        c.faceTracks.init(stateAddress(TableId::St39Script68CastEyes), stateAddress(TableId::St39Script68CastMouth));                     // exe 0x5e33a0
        textureTrackStop(a, c.faceTracks, FaceTrack::Eyes, 0);        // exe 0x5e3630
        textureTrackStop(a, c.faceTracks, FaceTrack::Mouth, 0);       // exe 0x5e3660
    } else if (CastState(a.state) != CastState::Wait) {
        return true;
    }
    if (script68Bytes().clock == kCastAnimTick) requestAnim(a, c.animKey, 1);
    return true;
}

constexpr int16_t kCrewShadowWidth = 0x20, kCrewShadowLength = 0x100;
// +8: the variant's state 0 (the bind) only; after it, nothing
enum class CrewState : uint8_t { Init = 0 };

} // namespace

// whether script `id`'s handler is script 0x68's
bool st39Script68Ported(uint8_t id) { return storyScriptHandler(id).id() == RoutineId::St39Script68; }

// script 0x68 ticked (false: another script's handler)
bool st39Script68Tick(const PlayerEnv&, uint8_t id) {
    if (storyScriptHandler(id).id() != RoutineId::St39Script68) return false;
    script68();
    return true;
}

// exe 0x44b940: the variant (one), then the texture tracks and the animation
void script68CastUpdate(const PlayerEnv&, uint32_t address) {
    AnimatedActor& a = animatedActorAt(address);
    CastFields& c = classState<CastFields>(a);
    castSavePose(a);
    if (castVariants->idAt(a.variant) != RoutineId::St39CastMember)
        notPortedAt(kStoryPort, "0x44b940 variant (table 0x88e668 has 1)", a.variant);
    if (!castMember(a, c)) return;
    textureTracksTick(a, c.faceTracks); // exe 0x5e34f0
    castAnimate(a, c.animKey, c.castFlags, kCastHold);
}

// exe 0x4ea120: the crew's variant 2 (its state 0 only)
void script68Crew(AnimatedActor& a) {
    if (CrewState(a.state) != CrewState::Init) return;
    if (!castBind(a, stateAddress(TableId::OpeningCastCrewBox), 0, true)) return;
    CastFields& c = classState<CastFields>(a);
    opening_cast_state::CrewFields& shadow = classState<opening_cast_state::CrewFields>(a);
    c.animKey = 0;
    c.castFlags |= kCastShadow;
    shadow.shadowWidth = kCrewShadowWidth;
    shadow.shadowLength = kCrewShadowLength;
    c.faceTracks.init(stateAddress(TableId::OpeningCastCrewTracks), 0);
    c.faceTracks.first.select(0, 0); // exe 0x5e33d0
}

} // namespace od::port
