// Script 0x69 (exe 0x539a60, states 0x8acf20: 0x539a80 / 0x52f420 / 0x539b50; camera 0x8acdac, steps 0x8acee8): ST1F
// sub 2 at stories 9..0x10 (room handler 0x581270) once flag 0x690 is set (ST1F message 17, the "yes" of the question
// 16 / 18) without flag 0x150 -- the departure from Calbania. State 0: the replay pad, the player script driven, voice
// 0x15 once the file loads are over, then the sequence. Steps: 0x539c70 (the actors 0x6dc swept, records 0x8acd70 /
// 0x8acd84, the player placed), 0x52ef00 (nothing), 0x539cf0 (yaw 0x800), 0x539d10 (the player and the first cast
// member placed; the music faded at 0x1c2), 0x539d90 (record 0x8acd70 freed, the second cast member rises and shrinks,
// fade 0x13 at 0xea, the in-stage warp to ST1F sub 1), 0x539f10 (record 0x8acd98, fade 3, the sky's scroll, the third
// cast member flies off, fade 0x13 at 0x314). The runner 0x52f420 (no face tracks). State 2: request 2 -> ST40 sub 1
// (0x80, -0x41, 0) yaw 0x400, then the story + 1.
#include "content/calbania/calbania_departure_script_state.hpp"
#include "content/calbania/ch3_scripts.hpp"

#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actor_sweeps.hpp"
#include "game/audio_logic.hpp"
#include "game/fade_state.hpp"
#include "game/game_state.hpp"
#include "game/message.hpp"
#include "game/player/player_input.hpp"
#include "game/port_gap.hpp"
#include "game/script_state.hpp"
#include "game/sky_layers.hpp"
#include "game/stage_files.hpp"
#include "game/stage_state.hpp"
#include "game/story.hpp"
#include "game/transition.hpp"
#include "game/transition_state.hpp"

using namespace od::port::calbania_departure_script_state;

namespace od::port {

namespace {

// the second cast member's parameter byte 1: it rises
struct RisingCastFields {
    explicit RisingCastFields(const Actor& actor) : RisingCastFields(const_cast<Actor&>(actor)) {}
    explicit RisingCastFields(Actor& actor) : rises(actor.params[1]) {}
    uint8_t& rises;
};
enum class SetUpSub : uint8_t { Start = 0, TakeOver = 1, WaitFiles = 2, Voice = 3, WaitVoice = 4 };
enum class WrapUpSub : uint8_t { Stop = 0, WaitSound = 1, End = 2 };
enum class RisePhase : uint8_t { Hide = 0, Wait = 1, Rise = 2, Shrink = 3, Warp = 4 };
enum class FlyPhase : uint8_t { Spawn = 0, Wait = 1, Climb = 2, FlyOff = 3 };

constexpr RoutineId kStepPlace = RoutineId::CalbaniaDepartureScriptStepPlace, kStepNothing = RoutineId::EmptyRoutine,
                    kStepTurn = RoutineId::CalbaniaDepartureScriptStepTurn,
                    kStepBoard = RoutineId::CalbaniaDepartureScriptStepBoard,
                   kStepRise = RoutineId::CalbaniaDepartureScriptStepRise,
                   kStepFlyOff = RoutineId::CalbaniaDepartureScriptStepFlyOff;
constexpr size_t kBoardCast = 0, kRisingCast = 1, kFlyingCast = 2;
constexpr uint32_t kVoice = 0x15, kVoiceFade = 8;
constexpr uint32_t kVibrationStop = 0x1000000;
constexpr uint32_t kSweepCast = 0x6dc, kSweepAll = 0xffe, kSweepKeep = 0x80;
constexpr uint32_t kLeaveFade = 0x13;
constexpr int16_t kShrinkLimit = 0x20;
constexpr uint32_t kSkyStartY = 0xd00000, kSkySlow = 0x58bf, kSkyFast = 0x8000, kSkyLate = 0x4000;

void departureStep(const RoutineRef& routine) {
    ScriptState& s = scriptState();
    const int32_t t = s.stepTick;
    Player& p = player();
    switch (routine.id()) {
    case kStepPlace:
        if (s.stepPhase != 0) return;
        actorsSweep(kSweepCast, kSweepKeep); // exe 0x5e05d0
        scriptActorSpawnRecord(stateAddress(TableId::CalbaniaDepartureScriptRecordFigure)); // exe 0x511740
        scriptActorSpawnRecord(stateAddress(TableId::CalbaniaDepartureScriptRecordShip));
        placePlayer(0xf9c00000u, 0xfdaf0000u, 0xfa400000u, 0x600);
        playerAnim(0x80);
        ++s.stepPhase;
        return;
    case kStepNothing: return;
    case kStepTurn:
        if (t == 0) {
            s.clearStepPhase();
            p.rotation.y = 0x800;
        }
        return;
    case kStepBoard:
        if (t == 0) {
            s.clearStepPhase();
            stage().gridByte = 0x18;
            placePlayer(0xfa400000u, 0xfd930000u, 0xfc800000u, 0x600);
            Actor& c = actorLayoutOf(*s.actors[kBoardCast]);
            c.pos.x = int32_t(0xf9000000u);
            c.pos.y = int32_t(0xfd6f0000u);
            c.pos.z = int32_t(0xfc000000u);
            c.rotation.y = 0x600;
            return;
        }
        if (t == 0x1c2) {
            soundFadeOut(kMusicFades, 0x222, 0); // exe 0x5d5780
            scriptState().part<DepartureBytes>().musicFaded = 1;
        }
        return;
    case kStepRise:
        if (t == 0) s.clearStepPhase();
        switch (RisePhase(s.stepPhase)) {
        case RisePhase::Hide:
            p.setDrawn(false);
            scriptActorFreeRecord(stateAddress(TableId::CalbaniaDepartureScriptRecordFigure)); // exe 0x511760
            RisingCastFields(actorLayoutOf(*s.actors[kRisingCast])).rises = 1;
            ++s.stepPhase;
            return;
        case RisePhase::Wait:
            if (t == 0x1e) s.stepPhase = uint8_t(RisePhase::Rise);
            return;
        case RisePhase::Rise: {
            Actor& c = actorLayoutOf(*s.actors[kRisingCast]);
            c.pos.y.whole -= 4u;
            if (s.stepTick != 0xaa) return;
            c.pos.y = int32_t(0xf8500000u);
            ++s.stepPhase;
            return;
        }
        case RisePhase::Shrink: {
            AnimatedActor& c = asAnimated(*s.actors[kRisingCast]);
            const int16_t sc = c.scale.x;
            c.pos.y.whole += 0xff40u;
            if (sc > kShrinkLimit) {
                c.scale.x = int16_t(sc - kShrinkLimit);
                c.scale.y -= uint32_t(kShrinkLimit);
                c.scale.z -= uint32_t(kShrinkLimit);
            }
            if (s.stepTick != 0xea) return;
            scriptFade(kLeaveFade, 0); // exe 0x53f6b0
            s.flags |= kScriptUnskippable;
            ++s.stepPhase;
            return;
        }
        case RisePhase::Warp:
            if (fadeState().busy != 0) return;
            requestTransition(kTransitionWarp, 0x1f, 1, 0, 0, 0, 0, kNoFade, kNoFade); // the in-stage warp to ST1F sub 1
            scriptNextStep(); // exe 0x5115c0(1)
            return;
        default: return;
        }
    case kStepFlyOff: {
        SkyLayer& sky = skyLayers()[0];
        switch (FlyPhase(s.stepPhase)) {
        case FlyPhase::Spawn:
            actorsSweep(kSweepAll, kSweepKeep);
            scriptActorSpawnRecord(stateAddress(TableId::CalbaniaDepartureScriptRecordFlight));
            ++s.stepPhase;
            return;
        case FlyPhase::Wait:
            if (t != 0x3c) return;
            scriptFade(3, 0);
            sky.scrollX = 0;
            sky.scrollY = int32_t(kSkyStartY);
            s.flags &= uint8_t(~kScriptUnskippable);
            ++s.stepPhase;
            return;
        case FlyPhase::Climb: {
            Actor& c = actorLayoutOf(*s.actors[kFlyingCast]);
            c.pos.z.whole += 1u;
            if (t == 0x294) ++s.stepPhase;
            const uint32_t d = t >= 0x274 ? kSkyFast : kSkySlow;
            sky.scrollX += d;
            sky.scrollY += d;
            return;
        }
        case FlyPhase::FlyOff: {
            AnimatedActor& c = asAnimated(*s.actors[kFlyingCast]);
            if ((s.stepTick & 1) && c.scale.x > kShrinkLimit) {
                const uint16_t sc = uint16_t(c.scale.x);
                c.scale.y -= 1u;
                c.scale.z -= 1u;
                c.scale.x = int16_t(sc - 1u);
            }
            if (s.stepTick > 0x2ee) {
                c.pos.z.whole += 6u;
                c.rotation.y -= 0xau;
            } else {
                c.pos.z.whole += 0x10u;
            }
            if (s.stepTick <= 0x2f4) {
                sky.scrollX += kSkyLate;
                sky.scrollY += kSkyLate;
            }
            if (s.stepTick == 0x314) {
                scriptFade(kLeaveFade, 0);
                s.flags |= kScriptUnskippable;
                ++s.stepSubPhase;
            }
            if (s.stepSubPhase != 0 && fadeState().busy == 0) scriptNextStep();
            return;
        }
        default: return;
        }
    }
    default: notPortedAt(kStoryPort, "script 0x69 step", routine.pointerValue());
    }
}

} // namespace

void calbaniaDepartureScript() {
    ScriptState& s = scriptState();
    switch (s.cutsceneState()) {
    case CutsceneState::SetUp: // exe 0x539a80
        switch (SetUpSub(s.subState)) {
        case SetUpSub::Start:
            scriptReplayPadOn(); // exe 0x5117f0
            scriptState().part<DepartureBytes>().musicFaded = 0;
            ++s.subState;
            [[fallthrough]];
        case SetUpSub::TakeOver:
            if (playerToScriptIfFree() != 0) return; // exe 0x572300
            clearPlayerAnglesAndFaces();
            ++s.subState;
            return;
        case SetUpSub::WaitFiles:
            if (fileLoads().loading & kFileLoading) return;
            s.subState = uint8_t(SetUpSub::Voice);
            return;
        case SetUpSub::Voice:
            if (fileLoads().loading & kStreamBusy) return;
            voiceClipPlay(kVoice); // exe 0x601050
            ++s.subState;
            return;
        case SetUpSub::WaitVoice:
            if (!voiceClipPlaying()) return;     // exe 0x601240
            scriptSequenceStart(stateAddress(TableId::CalbaniaDepartureScriptCamera), scriptStepList<TableId::CalbaniaDepartureScriptSteps>); // exe 0x511390
            s.enterState(CutsceneState::Steps);
            return;
        default: return;
        }
    case CutsceneState::Steps: scriptStepsStateNoFaces(departureStep); return; // exe 0x52f420
    case CutsceneState::WrapUp:                                                 // exe 0x539b50
        switch (WrapUpSub(s.subState)) {
        case WrapUpSub::Stop:
            padVibrate(0, kVibrationStop); // exe 0x55cd10
            playerAnim(0);
            actorsSweep(kSweepAll, kSweepKeep);  // exe 0x5e05d0
            windowCloseAll(WindowClose::Gentle); // exe 0x5577d0
            if (scriptState().part<DepartureBytes>().musicFaded == 0) soundFadeOut(kMusicFades, 0x7ff, 0);
            if (voiceClipPlaying()) voiceClipFadeOut(kVoiceFade); // exe 0x601220
            ++s.subState;
            [[fallthrough]];
        case WrapUpSub::WaitSound:
            if (!voiceClipIdle()) return;       // exe 0x601260
            if (!soundDone(kMusicFades)) return; // exe 0x5d5900
            ++s.subState;
            requestTransition(kTransitionAccepted, 0x40, 1, 0x80, int16_t(0xffbf), 0, 0x400, 2, kNoFade);
            return;
        case WrapUpSub::End:
            if (transitionRequest().state != 0) return;
            playerBackToPlay();   // exe 0x5723c0
            scriptReplayPadOff(); // exe 0x511890
            player().setDrawn(true);
            ++gameState().nextStory; // exe 0x512800
            scriptEndBlock();        // exe 0x5116a0
            return;
        default: return;
        }
    default: notPortedAt(kStoryPort, "script 0x69 state", s.state);
    }
}

} // namespace od::port
