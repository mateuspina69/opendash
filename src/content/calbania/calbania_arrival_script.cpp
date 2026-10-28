// Script 0x57 (exe 0x533520, states 0x8aa348: 0x533540 / 0x533610 / 0x533750; camera 0x8aa308, steps 0x8aa338 = one
// entry without a routine): ST23 sub 0 at story 8, the arrival on Calbania (room handler 0x581770 the first time, flag
// 0x5b0). The Flutter (record 0x8c8c80 = the room's slot 0) comes down from 0x400 above its place: vy 0x100, ay -0x10;
// 0x200 above it its cue 0x40 (its step 1), 0x100 above it the brake to vy 0x20, then it lands; 0x1e ticks later fade
// 0x12, state 2 once the fade is over. State 2: the voice faded, the Flutter freed, the actors of classes 0x6dc swept,
// the windows closed, flag 0x7d0, cell (0x40, 0x49) to variant 0, fade 2, the player back.
#include "content/calbania/calbania_arrival_script_state.hpp"
#include "content/calbania/ch3_scripts.hpp"

#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actor_sweeps.hpp"
#include "game/audio_logic.hpp"
#include "game/breakable_walls.hpp"
#include "game/display.hpp"
#include "game/fade_state.hpp"
#include "game/message.hpp"
#include "game/port_gap.hpp"
#include "game/room_scripts.hpp"
#include "game/script_state.hpp"
#include "game/stage_files.hpp"
#include "game/story.hpp"
#include "game/story_flags.hpp"
#include "game/transition.hpp"

using namespace od::port::calbania_arrival_script_state;

namespace od::port {

namespace {

// the Flutter's parameter bytes the script sets
struct FlutterFields {
    explicit FlutterFields(const AnimatedActor& actor) : FlutterFields(const_cast<AnimatedActor&>(actor)) {}
    explicit FlutterFields(AnimatedActor& actor) : lowered(actor.params[0]), cue(actor.params[1]) {}
    uint8_t& lowered;
    uint8_t& cue;
};
enum class SetUpSub : uint8_t { Lower = 0, WaitFiles = 1, Voice = 2, WaitVoice = 3 };
enum class LandSub : uint8_t { Descend = 0, Brake = 1, Settle = 2, Rest = 3, WaitFade = 4 };
enum class WrapUpSub : uint8_t { FadeVoice = 0, End = 1 };

constexpr size_t kFlutterSlot = 0;
constexpr int16_t kLowered = 0x400, kDescentSpeed = 0x100, kBrake = -0x10, kSlowest = 0x20;
constexpr int kCueHeight = 0x200, kBrakeHeight = 0x100;
constexpr uint8_t kFlutterCue = 0x40;
constexpr int32_t kRestTicks = 0x1e;
constexpr uint32_t kVoiceClip = 0xf, kVoiceFade = 8;
constexpr uint32_t kFadeIn = 2, kFadeOut = 0x12, kSkipFade = 0x22;
constexpr uint32_t kSweepMask = 0x6dc, kSweepKeep = 0x80;
constexpr uint32_t kFlagArrived = 0x7d0;
constexpr uint32_t kCellColumn = 0x40, kCellRow = 0x49;

// exe 0x533540: the Flutter lowered, the voice, the camera
void arrivalSetUp(ScriptState& s) {
    switch (SetUpSub(s.subState)) {
    case SetUpSub::Lower: {
        scriptReplayPadOn(); // exe 0x5117f0
        const uint32_t address = roomScriptState().actors[kFlutterSlot];
        AnimatedActor& flutter = animatedActorAt(address);
        FlutterFields f(flutter);
        player().setDrawn(false);
        pointAtWord(s.actors[kFlutterSlot], address);
        scriptState().part<ArrivalBytes>().restY = flutter.pos.y.whole;
        flutter.pos.y.whole = int16_t(flutter.pos.y.whole - kLowered);
        flutter.velocity.y = kDescentSpeed;
        flutter.acceleration.y = kBrake;
        f.lowered = 1;
        f.cue = 0;
        ++s.subState;
        [[fallthrough]];
    }
    case SetUpSub::WaitFiles:
        if (!(fileLoads().loading & kFileLoading)) ++s.subState;
        return;
    case SetUpSub::Voice:
        if (!(fileLoads().loading & kStreamBusy)) {
            voiceClipPlay(kVoiceClip);
            ++s.subState;
        }
        return;
    case SetUpSub::WaitVoice:
        if (voiceClipPlaying()) {
            scriptSequenceStart(stateAddress(TableId::CalbaniaArrivalScriptCameraScript), scriptStepList<TableId::CalbaniaArrivalScriptSteps>);
            scriptSequenceTick();
            scriptFade(kFadeIn, 0);
            s.enterState(CutsceneState::Steps);
        }
        return;
    default: return;
    }
}
// exe 0x533610: the Flutter lands
void arrivalLanding(ScriptState& s) {
    AnimatedActor& flutter = asAnimated(*s.actors[kFlutterSlot]);
    FlutterFields f(flutter);
    ArrivalBytes& bytes = scriptState().part<ArrivalBytes>();
    switch (LandSub(s.subState)) {
    case LandSub::Descend:
        if (flutter.step == 0 && bytes.restY - kCueHeight <= flutter.pos.y.whole) {
            f.cue = kFlutterCue;
            flutter.step = 1;
        }
        if (bytes.restY - kBrakeHeight <= flutter.pos.y.whole) ++s.subState;
        break;
    case LandSub::Brake:
        flutter.velocity.y = int16_t(flutter.velocity.y + flutter.acceleration.y);
        if (flutter.velocity.y < 0x21) {
            flutter.velocity.y = kSlowest;
            flutter.acceleration.y = 0;
            ++s.subState;
        }
        break;
    case LandSub::Settle:
        if (bytes.restY <= flutter.pos.y.whole) {
            flutter.pos.y.whole = bytes.restY;
            flutter.velocity.y = 0;
            flutter.acceleration.y = 0;
            ++s.subState;
        }
        break;
    case LandSub::Rest:
        if (bytes.restTicks == kRestTicks) {
            scriptFade(kFadeOut, 0);
            s.flags |= kScriptUnskippable;
            ++s.subState;
        } else {
            ++bytes.restTicks;
        }
        break;
    case LandSub::WaitFade:
        if (fadeState().busy == 0) s.enterState(CutsceneState::WrapUp);
        break;
    default: break;
    }
    integrateVelocity(flutter, 0, flutter.velocity.y, 0);
    if (scriptSkipRequested() != 0 && display().drawBuffer != 0) {
        scriptFade(kSkipFade, 0);
        s.enterState(CutsceneState::WrapUp);
    }
    scriptSequenceTick();
}
// exe 0x533750: the voice faded; the Flutter freed, the cell switched, the player back
void arrivalWrapUp(ScriptState& s) {
    if (WrapUpSub(s.subState) == WrapUpSub::FadeVoice) {
        if (voiceClipPlaying()) voiceClipFadeOut(kVoiceFade);
        ++s.subState;
    } else if (WrapUpSub(s.subState) != WrapUpSub::End) {
        return;
    }
    if (voiceClipIdle() && fadeState().busy == 0) {
        freeActor(*s.actors[kFlutterSlot]);   // exe 0x5e1490
        actorsSweep(kSweepMask, kSweepKeep); // exe 0x5e05d0
        windowCloseAll(WindowClose::Gentle); // exe 0x5577d0
        setStoryFlag(kFlagArrived);
        switchGridCellVariant(kCellColumn, kCellRow, 0);
        scriptFade(kFadeIn, 0);
        scriptReplayPadOff(); // exe 0x511890
        player().setDrawn(true);
        scriptEndBlock();
    }
}

} // namespace

void calbaniaArrivalScript() {
    ScriptState& s = scriptState();
    switch (s.cutsceneState()) {
    case CutsceneState::SetUp: arrivalSetUp(s); return;
    case CutsceneState::Steps: arrivalLanding(s); return;
    case CutsceneState::WrapUp: arrivalWrapUp(s); return;
    default: notPortedAt(kStoryPort, "script 0x57 state", s.state);
    }
}

} // namespace od::port
