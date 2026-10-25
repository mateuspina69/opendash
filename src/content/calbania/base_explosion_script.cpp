// Script 0x26 (exe 0x523e90, states 0x8a46ac: 0x523ed0 / 0x524050 / 0x524090; camera 0x8a459c (flag 0x3e5: the escape
// made it) or 0x8a45d0 (the player hidden), steps 0x8a45f8 = one step 0x524250): ST1F stories 9..0x10 with flag 0x23
// (room handler 0x581230, which clears 0x23): Glyde's base blows up, seen from the square. State 0: the actors 0xefe
// swept, the replay pad, the sequence, the player at (0x300, -0x1b0, -0xdd8) yaw 0x400; once script driven animation
// 1; voice 0x10 (0x3e5) / 0x11; state 1 once it plays (with 0x3e5 the player walks -0xf0 on the ground meanwhile). The
// step 0x524250: the player walks; at 0x3c six death explosions (0x8a4608), from 0x5a the blasts (0x524470(k): 0x8a4608
// + k * 8, pad vibration) with sparks (stage static type 3 variant 0x13 at 0x8a4680, 0x5244e0(k)) and camera shakes; at
// 0x96 without 0x3e5 fade 0x1b (the white screen asked for). The runner 0x524050 (skip: state 2, no fade). State 2:
// fade 0x1b (unless asked already), the voice faded; the actors 0xefe swept, animation 0; without 0x3e5 fade 0x12; then
// the player back, the block ended and: 0x3e5 -> flag 0x7d3 cleared, request 2 -> ST20 sub 0 (-0x51c, -0x280, 0x720)
// yaw 0 (fades 2 / 0x12); else the game over (the main task's mode ToGameOver, the task switch to 0x542ff0). Every
// tick after the state: the white screen asked for -> shown once no fade runs; shown = the white screen exe 0x5f30e0
// (the host draws it: baseBlownWhiteScreen).
#include "content/calbania/base_explosion_script_state.hpp"
#include "content/calbania/ch3_scripts.hpp"
#include "content/calbania/ch3_story.hpp"

#include "engine/data_source.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actor_sweeps.hpp"
#include "game/audio_logic.hpp"
#include "game/camera.hpp"
#include "game/display.hpp"
#include "game/explosions.hpp"
#include "game/fade_state.hpp"
#include "game/global_class_types.hpp"
#include "game/player/player_input.hpp"
#include "game/port_gap.hpp"
#include "game/script_state.hpp"
#include "game/spark_emitter.hpp"
#include "game/story.hpp"
#include "game/story_flags.hpp"
#include "game/table_records.hpp"
#include "game/title.hpp"
#include "game/transition.hpp"
#include "game/transition_state.hpp"

using namespace od::port::base_explosion_script_state;

namespace od::port {

namespace {

constexpr uint8_t kWhiteNone = 0, kWhiteAsked = 1, kWhiteShown = 0xff;
constexpr uint8_t kBaseBlownScript = 0x26;

enum class SetUpSub : uint8_t { Start = 0, TakeOver = 1, Voice = 2, WaitVoice = 3 };
enum class WrapUpSub : uint8_t { Fade = 0, WaitIdle = 1, FadeOut = 2, End = 3 };
constexpr uint32_t kFlagEscaped = 0x3e5, kFlagSquareWaits = 0x7d3;
constexpr RoutineId kStepBlowUp = RoutineId::BaseExplosionScriptStepBlowUp;
constexpr uint32_t kSweepBase = 0xefe, kSweepKeep = 0x80;
constexpr uint32_t kVoiceFade = 8;
constexpr uint32_t kWhiteFade = 0x1b;

// the points of the blow-up: the explosions' / blasts' {x, y, z, blast size} and the sparks' {x, y, z}
struct BlowUpPoint : DataRecord {
    ShortPoint point;
    uint16_t size = 0;
    static BlowUpPoint load(const TableReader& r) {
        BlowUpPoint rec;
        rec.point = ShortPoint::load(r.sub(0x0, 6));
        rec.size = r.u16(0x6);
        return rec;
    }
};
struct SparkPoint : DataRecord {
    ShortPoint point;
    static SparkPoint load(const TableReader& r) {
        SparkPoint rec;
        rec.point = ShortPoint::load(r.sub(0x0, 6));
        return rec;
    }
};
const DataTable<RecordTable<BlowUpPoint, 0xf, 8>> blowUpPoints{TableId::BaseExplosionScriptBlowUpPoints};
const DataTable<RecordTable<SparkPoint, 7, 6>> sparkPoints{TableId::BaseExplosionScriptSparkPoints};
constexpr uint32_t kExplosions = 6, kSparks = 7;
constexpr uint16_t kExplosionSpread = 0x2bc, kExplosionFlashes = 0x1f4;
constexpr uint8_t kSparkVariant = 0x13;
constexpr uint32_t kBlastRumbleA = 0x201, kBlastRumbleB = 0x208b0;
constexpr uint32_t kShakeAmount = 0x800;
constexpr uint16_t kShakeBig = 0x80, kShakeSmall = 0x40;

// the player walks on along the ground
void walkOn() {
    Player& p = player();
    integrateVelocity(p, 0, 0, -0xf0);                    // exe 0x5e3690
    p.pos.y.whole = int16_t(actorGround(scriptEnv(), p)); // exe 0x40f190
}
// exe 0x524470(k): a blast at point k, the pad's rumble
void blastAt(uint32_t k) {
    if (const uint32_t address = allocActor60(0)) { // exe 0x5e1300
        const BlowUpPoint at = blowUpPoints[k];
        StaticActor& e = staticActorAt(address);
        e.flags = kActorLive;
        e.loopFlags |= kActorSweepGroup1;
        e.type = uint8_t(GlobalStaticType::Blast);
        e.variant = 0;
        e.pos.x.whole = at.point.x;
        e.pos.y.whole = at.point.y;
        e.pos.z.whole = at.point.z;
        BlastParams params(actorHeaderAt(address));
        params.size = at.size;
        params.attack = 0;
    }
    padVibrate(kBlastRumbleA, kBlastRumbleB); // exe 0x55cd10
}
// exe 0x5244e0(k): a spark emitter at spark point k
void sparksAt(uint32_t k) {
    if (const uint32_t address = allocActor60(kActorStageClass)) { // exe 0x5e12f0
        const SparkPoint at = sparkPoints[k];
        StaticActor& e = staticActorAt(address);
        e.flags = kActorLive;
        e.loopFlags |= kActorSweepGroup2;
        e.type = kSparkEmitterStageType;
        e.variant = kSparkVariant;
        e.pos.x.whole = at.point.x;
        e.pos.y.whole = at.point.y;
        e.pos.z.whole = at.point.z;
        e.setParamsWord(0);
    }
}
void blastsWithShake(uint32_t a, uint32_t b) {
    blastAt(a);
    blastAt(b);
    cameraShake(true, kShakeAmount, kShakeSmall);
}
// exe 0x524250
void blowUpStep(const RoutineRef& routine) {
    if (!routine.is(kStepBlowUp)) notPortedAt(kStoryPort, "script 0x26 step", routine.pointerValue());
    ScriptState& s = scriptState();
    walkOn();
    switch (s.stepTick) { // switch (t - 0x3c) over the byte table 0x524404
    case 0x3c:
        for (uint32_t i = 0; i < kExplosions; ++i) {
            if (const uint32_t address = allocActor60(0)) { // exe 0x5e1300
                const BlowUpPoint at = blowUpPoints[i];
                StaticActor& e = staticActorAt(address);
                e.flags = kActorLive;
                e.type = uint8_t(GlobalStaticType::Explosion);
                e.variant = 0;
                e.loopFlags |= kActorSweepGroup2;
                e.pos.x.whole = at.point.x;
                e.pos.y.whole = at.point.y;
                e.pos.z.whole = at.point.z;
                ExplosionParams params(actorHeaderAt(address));
                params.spread = kExplosionSpread;
                params.flashes = kExplosionFlashes;
            }
        }
        return;
    case 0x5a:
        blastAt(0);
        for (uint32_t k = 0; k < kSparks; ++k) sparksAt(k);
        cameraShake(true, kShakeAmount, kShakeBig);
        return;
    case 0x6e: blastsWithShake(1, 7); return;
    case 0x78: blastsWithShake(2, 8); return;
    case 0x7d: blastsWithShake(3, 9); return;
    case 0x82: blastsWithShake(4, 0xa); return;
    case 0x87: blastsWithShake(5, 0xb); return;
    case 0x96:
        if (!storyFlag(kFlagEscaped)) {
            scriptFade(kWhiteFade, 0);
            scriptState().part<BaseBlownBytes>().whiteScreen = kWhiteAsked;
        }
        blastAt(0xc);
        cameraShake(true, kShakeAmount, kShakeBig);
        return;
    case 0xa0:
        blastAt(0xd);
        cameraShake(true, kShakeAmount, kShakeBig);
        return;
    case 0xa5:
        blastAt(0xe);
        cameraShake(true, kShakeAmount, kShakeBig);
        return;
    default: return;
    }
}

void baseExplosionState(ScriptState& s) {
    switch (s.cutsceneState()) {
    case CutsceneState::SetUp: // exe 0x523ed0
        switch (SetUpSub(s.subState)) {
        case SetUpSub::Start:
            s.subState = uint8_t(SetUpSub::TakeOver);
            s.flags |= kScriptUnskippable;
            actorsSweep(kSweepBase, kSweepKeep);
            scriptReplayPadOn(); // exe 0x5117f0
            if (storyFlag(kFlagEscaped)) {
                scriptSequenceStart(stateAddress(TableId::BaseExplosionScriptCameraEscaped), stateAddress(TableId::BaseExplosionScriptSteps));
            } else {
                scriptSequenceStart(stateAddress(TableId::BaseExplosionScriptCameraNotEscaped), stateAddress(TableId::BaseExplosionScriptSteps));
                player().flags &= uint8_t(~kActorLiveDrawn);
            }
            scriptSequenceTick();
            clearPlayerAnglesAndFaces();
            placePlayer(0x3000000, 0xfe500000u, 0xf2280000u, 0x400);
            [[fallthrough]];
        case SetUpSub::TakeOver:
            if (playerToScriptIfFree() != 0) return; // exe 0x572300
            ++s.subState;
            playerAnim(1);
            return;
        case SetUpSub::Voice:
            ++s.subState;
            voiceClipPlay(storyFlag(kFlagEscaped) ? 0x10 : 0x11);
            [[fallthrough]];
        case SetUpSub::WaitVoice:
            if (voiceClipPlaying()) {
                s.enterState(CutsceneState::Steps);
                s.flags &= uint8_t(~kScriptUnskippable);
            }
            if (storyFlag(kFlagEscaped)) walkOn();
            return;
        default: return;
        }
    case CutsceneState::Steps: // exe 0x524050: the step, the skip (state 2 without a fade)
        if (s.steps->id != kScriptStepEnd) {
            blowUpStep(stateRoutine(s.steps->routine));
            if (scriptSkipRequested() != 0 && display().drawBuffer != 0) s.enterState(CutsceneState::WrapUp);
        } else {
            s.enterState(CutsceneState::WrapUp);
        }
        scriptSequenceTick();
        return;
    case CutsceneState::WrapUp: // exe 0x524090
        switch (WrapUpSub(s.subState)) {
        case WrapUpSub::Fade: {
            ++s.subState;
            BaseBlownBytes& bytes = scriptState().part<BaseBlownBytes>();
            if (fadeState().busy == 0 && bytes.whiteScreen == kWhiteNone) {
                scriptFade(kWhiteFade, 1);
                bytes.whiteScreen = kWhiteAsked;
            }
            if (voiceClipPlaying()) voiceClipFadeOut(kVoiceFade);
            [[fallthrough]];
        }
        case WrapUpSub::WaitIdle:
            if (voiceClipIdle() && fadeState().busy == 0) {
                ++s.subState;
                actorsSweep(kSweepBase, kSweepKeep);
                playerAnim(0);
                return;
            }
            walkOn();
            scriptSequenceTick();
            return;
        case WrapUpSub::FadeOut:
            if (!storyFlag(kFlagEscaped)) {
                if (fadeState().busy != 0) return;
                ++s.subState;
                scriptFade(0x12, 1);
                return;
            }
            ++s.subState;
            return;
        case WrapUpSub::End:
            if (fadeState().busy != 0) return;
            playerBackToPlay();   // exe 0x5723c0
            scriptReplayPadOff(); // exe 0x511890
            player().flags |= kActorLiveDrawn;
            scriptEndBlock();
            if (!storyFlag(kFlagEscaped)) { // the game over: the main task entry 0x542ff0 (exe 0x604b06)
                mainTask().enterMode(MainMode::ToGameOver, 1);
                return;
            }
            clearStoryFlag(kFlagSquareWaits);
            requestTransition(kTransitionAccepted, 0x20, 0, int16_t(0xfae4), int16_t(0xfd80), 0x720, 0, 2, 0x12);
            return;
        default: return;
        }
    default: notPortedAt(kStoryPort, "script 0x26 state", s.state);
    }
}

} // namespace

void baseExplosionScript() {
    ScriptState& s = scriptState();
    baseExplosionState(s);
    BaseBlownBytes& bytes = scriptState().part<BaseBlownBytes>();
    if (bytes.whiteScreen == kWhiteAsked && fadeState().busy == 0) bytes.whiteScreen = kWhiteShown;
}

bool baseBlownWhiteScreen() {
    const ScriptState& s = scriptState();
    return s.id == kBaseBlownScript && scriptState().part<BaseBlownBytes>().whiteScreen == kWhiteShown;
}

} // namespace od::port
