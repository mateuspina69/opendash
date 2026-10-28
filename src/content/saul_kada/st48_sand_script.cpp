// Script 0x58 of ST48 (exe 0x533820, its steps [0x8aa398] by its state; ST48 sub 0, the first visit; hypothesis
// names): the room's tracked actor 0 (record 0x8cd038) rises out of the sand with a voice clip and a camera sequence;
// the end frees it, sweeps the transition's classes, sets flag 0x7e0 and closes the sand's cell again.
#include "content/saul_kada/st48_sand_script.hpp"
#include "content/saul_kada/st48_sand_script_state.hpp"

#include "engine/routine_ids.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actor_sweeps.hpp"
#include "game/audio_logic.hpp"
#include "game/breakable_walls.hpp"
#include "game/display.hpp"
#include "game/fade_state.hpp"
#include "game/message.hpp"
#include "game/player/player_script_control.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/room_scripts.hpp"
#include "game/script_engine.hpp"
#include "game/script_state.hpp"
#include "game/stage_files.hpp"
#include "game/story.hpp"
#include "game/story_flags.hpp"
#include "game/transition.hpp"

using namespace od::port::st48_sand_script_state;

namespace od::port {

namespace {

enum class SandState : uint8_t { SetUp = 0, Rise = 1, End = 2 };
enum class SetUpStep : uint8_t { Start = 0, WaitFiles = 1, Voice = 2, WaitVoice = 3 };
enum class RiseStep : uint8_t { Emerge = 0, Slow = 1, Settle = 2, Hold = 3, WaitFade = 4 };
SandScriptFields& fields() {
    return scriptState().part<SandScriptFields>();
}
// The rising actor's parameters (its class's; hypothesis names)
struct RiserParams {
    explicit RiserParams(const Actor& actor) : RiserParams(const_cast<Actor&>(actor)) {}
    explicit RiserParams(Actor& actor) : mode(actor.params[0]), level(actor.params[1]) {}
    uint8_t& mode;
    uint8_t& level;
};
constexpr uint32_t kSandCellColumn = 0x45, kSandCellRow = 0x3c;
constexpr uint32_t kFlagSandSeen = 0x7e0;
constexpr uint32_t kSandVoice = 0xf;
constexpr uint8_t kFadeIn = 2, kHoldFade = 0x12, kSkipFade = 0x22;
constexpr uint32_t kVoiceFade = 8;

void sandScript() { // exe 0x533820
    ScriptState& s = scriptState();
    SandScriptFields& f = fields();
    switch (SandState(s.state)) {
    case SandState::SetUp: // exe 0x533840
        switch (SetUpStep(s.subState)) {
        case SetUpStep::Start: {
            scriptReplayPadOn(); // exe 0x5117f0
            player().setDrawn(false);
            const uint32_t riser = roomScriptState().actors[0];
            pointAtWord(s.actors[0], riser);
            Actor& a = actorAt(riser);
            f.restHeight = a.pos.y.whole;
            a.pos.y.whole = int16_t(uint16_t(a.pos.y.whole) + 0xfc00u);
            a.velocity.y = 0x100;
            a.acceleration.y = -0x10;
            RiserParams(actorAt(riser)).mode = 1;
            RiserParams(actorAt(riser)).level = 0;
            s.subState += 1u;
            [[fallthrough]];
        }
        case SetUpStep::WaitFiles:
            if (!(fileLoads().loading & kFileLoading)) s.subState += 1u;
            return;
        case SetUpStep::Voice:
            if (fileLoads().loading & kStreamBusy) return;
            voiceClipPlay(kSandVoice); // exe 0x601050
            s.subState += 1u;
            return;
        case SetUpStep::WaitVoice:
            if (!voiceClipPlaying()) return;
            scriptSequenceStart(stateAddress(TableId::St48SandScriptCameraScript), scriptStepList<TableId::St48SandScriptSteps>); // exe 0x511390
            scriptSequenceTick();                       // exe 0x5114d0
            scriptFade(kFadeIn, 0);                     // exe 0x53f6b0
            s.enterState(uint8_t(SandState::Rise));
            return;
        default: return;
        }
    case SandState::Rise: { // exe 0x533910: the actor rises (slowing to 0x20) to its rest height, 30 ticks, a fade
        Actor& a = actorLayoutOf(*s.actors[0]);
        switch (RiseStep(s.subState)) {
        case RiseStep::Emerge:
            if (a.step == 0 && a.pos.y.whole >= f.restHeight - 0x200) {
                RiserParams(a).level = 0x40;
                a.step = 1;
            }
            if (a.pos.y.whole >= f.restHeight - 0x100) s.subState += 1u;
            break;
        case RiseStep::Slow:
            a.velocity.y = int16_t(uint16_t(uint16_t(a.velocity.y) + uint16_t(a.acceleration.y)));
            if (a.velocity.y <= 0x20) {
                a.velocity.y = 0x20;
                a.acceleration.y = 0;
                s.subState += 1u;
            }
            break;
        case RiseStep::Settle:
            if (a.pos.y.whole >= f.restHeight) {
                a.pos.y.whole = f.restHeight;
                a.velocity.y = 0;
                a.acceleration.y = 0;
                s.subState += 1u;
            }
            break;
        case RiseStep::Hold:
            if (f.holdTicks == 0x1e) {
                scriptFade(kHoldFade, 0);
                s.flags |= kScriptUnskippable;
                s.subState += 1u;
            } else {
                f.holdTicks += 1u;
            }
            break;
        case RiseStep::WaitFade:
            if (fadeState().busy == 0) s.enterState(uint8_t(SandState::End));
            break;
        default: break;
        }
        integrateVelocity(a, 0, a.velocity.y, 0);                 // exe 0x5e3690
        if (scriptSkipRequested() && display().drawBuffer != 0) { // exe 0x511790
            scriptFade(kSkipFade, 0);
            s.enterState(uint8_t(SandState::End));
        }
        scriptSequenceTick();
        return;
    }
    case SandState::End: // exe 0x533a50
        if (s.subState == 0) {
            if (voiceClipPlaying()) voiceClipFadeOut(kVoiceFade); // exe 0x601220
            s.subState += 1u;
        }
        if (s.subState != 1 || !voiceClipIdle() || fadeState().busy != 0) return;
        freeActor(*s.actors[0]); // exe 0x5e1490
        actorsSweep(kSweepTransition, kSweepFree);
        windowCloseAll(WindowClose::Gentle); // exe 0x5577d0
        setStoryFlag(kFlagSandSeen);
        switchGridCellVariant(kSandCellColumn, kSandCellRow, 0); // exe 0x58d5f0
        scriptFade(kFadeIn, 0);
        scriptReplayPadOff(); // exe 0x511890
        player().setDrawn(true);
        scriptEndBlock(); // exe 0x5116a0
        return;
    default: notPortedAt(kStoryPort, "script 0x58 state", s.state);
    }
}

} // namespace

bool st48SandScriptPorted(RoutineId handler) { return handler == RoutineId::St48SandScript; }
bool st48SandScriptTick(const PlayerEnv&, RoutineId handler) {
    if (!st48SandScriptPorted(handler)) return false;
    sandScript();
    return true;
}

} // namespace od::port
