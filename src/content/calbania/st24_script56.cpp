// ST24's script 0x56 (Pokte's first visit). Transcribed from dash2.exe one function per exe function.
//
// Scripts (id, handler, stage, trigger):
//   0x56 (0x533220)  ST24 story 3 sub 0 (case 0x5818f0, first visit, flag 0x580): the stand-on solid of the room's
//                    slot 0 (class 0x49d110) rises 0x400 into place, voice 0xf; flag 0x7a8, cell (0x4c, 0x4a) to
//                    variant 0
#include "content/calbania/st24_script56.hpp"
#include "content/calbania/st24_script56_state.hpp"

#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actor_sweeps.hpp"
#include "game/address_forms.hpp"
#include "game/audio_logic.hpp"
#include "game/breakable_walls.hpp"
#include "game/display.hpp"
#include "game/fade_state.hpp"
#include "game/message.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/room_scripts.hpp"
#include "game/script_engine.hpp"
#include "game/script_state.hpp"
#include "game/stage_files.hpp"
#include "game/story.hpp"
#include "game/story_flags.hpp"
#include "game/transition.hpp"

using namespace od::port::st24_script56_state;

namespace od::port {

namespace {

// ==== script 0x56 (0x533220, states 0x8aa2f8 = 0x533240 / 0x533310 / 0x533450; camera 0x8aa2b8, steps 0x8aa2e8 (one
// empty step, not run: state 1 has its own body)) ================================================================
// ST24 story 3 sub 0 (case 0x5818f0), the first visit: the stand-on solid of the room's slot 0 (record 0x8c8e80, class
// 0x49d110) is lowered by 0x400 and rises (vy 0x100, -0x10 a tick; at 0x200 below its rest its cue byte = 0x40, its
// step = 1; from 0x100 below vy settles at 0x20; at rest it stops); 0x1e ticks later fade 0x12. State 0 also waits for
// the DAT loader and starts voice 0xf; once it plays the camera, fade 2. End: the voice faded; once idle the solid
// freed, actors 0x6dc swept, windows closed, flag 0x7a8, cell (0x4c, 0x4a) to segment variant 0, fade 2, the player
// back (drawn).

// the risen solid's parameter bytes the script sets (its class does not read them)
struct RisingSolidFields {
    explicit RisingSolidFields(const AnimatedActor& actor) : RisingSolidFields(const_cast<AnimatedActor&>(actor)) {}
    explicit RisingSolidFields(AnimatedActor& actor) : raised(actor.params[0]), cue(actor.params[1]) {}
    uint8_t& raised;
    uint8_t& cue;
};

enum class SetUpSub : uint8_t { Lower = 0, WaitFiles = 1, Voice = 2, WaitVoice = 3 };
enum class RiseSub : uint8_t { Rise = 0, Slow = 1, Settle = 2, Rest = 3, WaitFade = 4 };
enum class WrapUpSub : uint8_t { FadeVoice = 0, End = 1 };

constexpr size_t kSolidSlot = 0;          // the room's actor and the script's actor of the solid
constexpr int16_t kLowered = 0x400;       // how far below its rest it starts
constexpr int16_t kRiseSpeed = 0x100, kRiseBrake = -0x10, kRiseSlowest = 0x20;
constexpr int kCueHeight = 0x200, kSlowHeight = 0x100;
constexpr uint8_t kSolidCue = 0x40;
constexpr int32_t kRestTicks = 0x1e;
constexpr uint32_t kVoiceClip = 0xf, kVoiceFade = 8;
constexpr uint32_t kFadeIn = 2, kFadeOut = 0x12, kSkipFade = 0x22;
constexpr uint32_t kSweepMask = 0x6dc, kSweepKeep = 0x80;
constexpr uint32_t kFlagSolidRisen = 0x7a8;
constexpr uint32_t kCellColumn = 0x4c, kCellRow = 0x4a;

// the voice-clip tail of the end states: the clip faded out (exe 0x601220(8)) while it plays
void voiceFadeIfPlaying() {
    if (voiceClipPlaying()) voiceClipFadeOut(kVoiceFade);
}

// exe 0x533240: the solid lowered, the voice started, the camera
void riseSetUp(ScriptState& s) {
    switch (SetUpSub(s.subState)) {
    case SetUpSub::Lower: {
        scriptReplayPadOn();
        const uint32_t solidAddress = roomScriptState().actors[kSolidSlot];
        AnimatedActor& solid = animatedActorAt(solidAddress);
        RisingSolidFields f(solid);
        player().setDrawn(false);
        pointAtWord(s.actors[kSolidSlot], solidAddress);
        scriptState().part<RiseBytes>().restY = solid.pos.y.whole;
        solid.pos.y.whole = int16_t(uint16_t(solid.pos.y.whole) - uint32_t(kLowered));
        solid.velocity.y = kRiseSpeed;
        solid.acceleration.y = kRiseBrake;
        f.raised = 1;
        f.cue = 0;
        ++s.subState;
        [[fallthrough]];
    }
    case SetUpSub::WaitFiles:
        if (fileLoads().loading & kFileLoading) return;
        ++s.subState;
        return;
    case SetUpSub::Voice:
        if (fileLoads().loading & kStreamBusy) return;
        voiceClipPlay(kVoiceClip);
        ++s.subState;
        return;
    case SetUpSub::WaitVoice:
        if (!voiceClipPlaying()) return;
        scriptSequenceStart(stateAddress(TableId::St24Script56CameraScript), stateAddress(TableId::St24Script56Steps));
        scriptSequenceTick();
        scriptFade(kFadeIn, 0);
        s.enterState(CutsceneState::Steps);
        return;
    default: return;
    }
}
// exe 0x533310: the solid risen into place
void riseIntoPlace(ScriptState& s) {
    AnimatedActor& solid = asAnimated(*s.actors[kSolidSlot]);
    RisingSolidFields f(solid);
    RiseBytes& bytes = scriptState().part<RiseBytes>();
    switch (RiseSub(s.subState)) {
    case RiseSub::Rise:
        if (solid.step == 0 && int(bytes.restY) - kCueHeight <= int(solid.pos.y.whole)) {
            f.cue = kSolidCue;
            solid.step = 1;
        }
        if (int(bytes.restY) - kSlowHeight <= int(solid.pos.y.whole)) ++s.subState;
        break;
    case RiseSub::Slow:
        solid.velocity.y += uint16_t(solid.acceleration.y);
        if (solid.velocity.y <= kRiseSlowest) {
            solid.velocity.y = kRiseSlowest;
            solid.acceleration.y = 0;
            ++s.subState;
        }
        break;
    case RiseSub::Settle:
        if (bytes.restY <= solid.pos.y.whole) {
            solid.pos.y.whole = bytes.restY;
            solid.velocity.y = 0;
            solid.acceleration.y = 0;
            ++s.subState;
        }
        break;
    case RiseSub::Rest:
        if (bytes.restTicks != kRestTicks) {
            ++bytes.restTicks;
        } else {
            scriptFade(kFadeOut, 0);
            s.flags |= kScriptUnskippable;
            ++s.subState;
        }
        break;
    case RiseSub::WaitFade:
        if (fadeState().busy == 0) s.enterState(CutsceneState::WrapUp);
        break;
    default: break;
    }
    integrateVelocity(solid, 0, solid.velocity.y, 0); // exe 0x5e3690
    if (scriptSkipRequested() != 0 && display().drawBuffer != 0) {
        scriptFade(kSkipFade, 0);
        s.enterState(CutsceneState::WrapUp);
    }
    scriptSequenceTick();
}
// exe 0x533450: the voice faded; the solid freed, the cell switched, the player back
void riseWrapUp(ScriptState& s) {
    switch (WrapUpSub(s.subState)) {
    case WrapUpSub::FadeVoice:
        voiceFadeIfPlaying();
        ++s.subState;
        [[fallthrough]];
    case WrapUpSub::End:
        if (!voiceClipIdle() || fadeState().busy != 0) return;
        freeActor(*s.actors[kSolidSlot]); // exe 0x5e1490
        actorsSweep(kSweepMask, kSweepKeep);
        windowCloseAll(WindowClose::Gentle);
        setStoryFlag(kFlagSolidRisen);
        switchGridCellVariant(kCellColumn, kCellRow, 0); // exe 0x58d5f0
        scriptFade(kFadeIn, 0);
        scriptReplayPadOff();
        player().setDrawn(true);
        scriptEndBlock();
        return;
    default: return;
    }
}
void riseScript() {
    ScriptState& s = scriptState();
    switch (s.cutsceneState()) {
    case CutsceneState::SetUp: riseSetUp(s); return;
    case CutsceneState::Steps: riseIntoPlace(s); return;
    case CutsceneState::WrapUp: riseWrapUp(s); return;
    default: notPortedAt(kStoryPort, "script 0x56 state", s.state);
    }
}

} // namespace

// (its handler: game/story's table of the scripts' handlers)
bool st24Script56Ported(uint8_t id) { return storyScriptHandler(id).is(RoutineId::St24Script56); }

bool st24Script56Tick(const PlayerEnv& env, uint8_t id) {
    address_forms::E = &env;
    if (!storyScriptHandler(id).is(RoutineId::St24Script56)) return false;
    riseScript();
    return true;
}

} // namespace od::port
