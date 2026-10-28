// The gate of the Manda ruin: script 0x14 and its guardian (see st24_ruin_gate.hpp). The script's states are its exe
// table's ([0x8a1f68]: 0x51d160 / the talk scripts' runner exe 0x52f420 / 0x51d270); its steps (list 0x8a1f38) are
// dispatched by the named routine. The guardian's lean / bob / collision / head turn are the Manda guardians'
// routines (exe 0x4866a0 / 0x488170 / 0x486610 / 0x487bf0 / 0x486910; content/manda/manda_guardian has its own
// transcription).
#include "content/manda/st24_ruin_gate.hpp"
#include "content/manda/st24_ruin_gate_state.hpp"

#include "engine/data_source.hpp"
#include "game/actor_animation.hpp"
#include "game/actor_model.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actor_stats.hpp"
#include "game/actor_sweeps.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/cutscene_actor.hpp"
#include "game/fade_state.hpp"
#include "game/global_class_types.hpp"
#include "game/player/player_script_control.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/script_engine.hpp"
#include "game/script_state.hpp"
#include "game/skid_sparks.hpp"
#include "game/stage_state.hpp"
#include "game/story.hpp"
#include "game/transition.hpp"
#include "game/transition_state.hpp"

namespace od::port {

using namespace st24_ruin_gate_state;

namespace {

constexpr const char* kGateArea = "island story port"; // the area of the script's port-gap errors

// ---- script 0x14 -------------------------------------------------------------------------------------------------------
constexpr uint32_t kVoiceGuardian = 5;
constexpr uint32_t kVoiceFadeStep = 8;
constexpr uint32_t kFadeToRuin = 0x12;
constexpr uint8_t kSt12 = 0x12;


ScriptState& block() { return scriptState(); }
void lockSkip(bool on) {
    if (on) block().flags |= kScriptUnskippable;
    else block().flags &= uint8_t(~kScriptUnskippable);
}

// the step routines (list 0x8a1f38): 0 exe 0x51d380 (the player idle once), 1..3 the empty one, 4 exe 0x51d3b0 (the
// fade to the ruin at 0x130, the skip locked)
void step14(const RoutineRef& routine) {
    ScriptState& s = block();
    switch (routine.id()) {
    case RoutineId::St24GateStep1:
        if (s.stepPhase != 0) return;
        playerScriptAnim(PlayerAnim::Idle, 0, true);
        ++s.stepPhase;
        return;
    case RoutineId::St24GateStep2:
        if (s.stepTick != 0x130) return;
        lockSkip(true);
        scriptFade(kFadeToRuin, 0);
        return;
    case RoutineId::EmptyRoutine: return; // the empty routine
    default: notPortedAt(kGateArea, "script 0x14 step", routine.pointerValue());
    }
}
// exe 0x51d160: the actors swept, the sequence on the replay pad, the player hidden at the gate, the script's bytes
// cleared; once it is script driven the player idle, the voice; state 1 once it plays
void gateSetUp() {
    ScriptState& s = block();
    switch (s.subState) {
    case 0: {
        actorsSweep(kSweepActors, kSweepFree);
        scriptSequenceStart(stateAddress(TableId::St24RuinGateCamera), scriptStepList<TableId::St24RuinGateSteps>);
        scriptReplayPadOn(); // exe 0x5117f0
        lockSkip(true);
        Player& p = player();
        p.setDrawn(false);
        p.clearDirectionOffsetWords();
        p.idleFaces[0] = 0;
        p.idleFaces[1] = 0;
        p.pos.x = int32_t(0xf6700000u);
        p.pos.y = int32_t(0xf7ff0000u);
        p.pos.z = 0x0ac00000;
        p.rotation.y = 0;
        s.clearScriptByteRun();
        scriptSequenceTick();
        ++s.subState;
    }
        [[fallthrough]];
    case 1:
        if (playerToScriptIfFree() == 0) {
            playerScriptAnim(PlayerAnim::Idle, 0, true);
            ++s.subState;
        }
        return;
    case 2:
        voiceClipPlay(kVoiceGuardian);
        ++s.subState;
        return;
    case 3:
        if (!voiceClipPlaying()) return;
        scriptState().part<ScriptCastStart>().started = 1;
        lockSkip(false);
        s.enterState(CutsceneState::Steps);
        return;
    default: return;
    }
}
// exe 0x51d270: the skip locked, the voice faded; once it is idle the request to ST12 sub 0 (the fade out unless
// skipped); once taken and the fade done the player back, shown, the block's end
void gateWrapUp() {
    ScriptState& s = block();
    switch (s.subState) {
    case 0:
        lockSkip(true);
        if (voiceClipPlaying()) voiceClipFadeOut(kVoiceFadeStep);
        ++s.subState;
        [[fallthrough]];
    case 1:
        if (!voiceClipPlaying()) ++s.subState;
        return;
    case 2:
        requestTransition(kTransitionAccepted, kSt12, 0, 0, -0x110, -0xc00, kHalfTurn, 2,
                          (s.flags & kScriptSkipTaken) ? kNoFade : uint8_t(kFadeToRuin));
        ++s.subState;
        return;
    case 3:
        if (transitionRequest().state != kTransitionNone || !fadeState().idle()) return;
        playerBackToPlay();   // exe 0x5723c0
        scriptReplayPadOff(); // exe 0x511890
        player().airborne = 0;
        player().setDrawn(true);
        scriptEndBlock();
        return;
    default: return;
    }
}
void script14() {
    switch (block().cutsceneState()) { // [0x8a1f68]
    case CutsceneState::SetUp: gateSetUp(); return;
    case CutsceneState::Steps: scriptStepsStateNoFaces(&step14); return;
    case CutsceneState::WrapUp: gateWrapUp(); return;
    default: notPortedAt(kGateArea, "script 0x14 state", block().state);
    }
}

constexpr uint8_t kReflect = 0x01, kNoLean = 0x02, kFading = 0x04, kNoHeadTurn = 0x10;
constexpr uint8_t kGuardianFlags = 0x4e; // kNoLean | kFading | 0x08 | 0x40 (their readers are the guardians')
constexpr uint8_t kHitWall = 0x01, kHitReflected = 0x02, kHitTestedOut = 0x08, kHitFloor = 0x10;
// the fade's thresholds by bone (exe data 0x8922e0: s8 x 13)
const DataTable<ScalarTable<int8_t, 13>> fadeThresholds{TableId::St24RuinGateFadeThresholds};
constexpr uint32_t kFadeBones = 13;
constexpr int8_t kFadeTicks = 0x1a;
const DataTable<CollisionBoxRecord> guardianBox{TableId::St24RuinGateGuardianBox};
constexpr uint8_t kGuardianDustVariant = 0xf;
// the part shown / hidden at the start
constexpr size_t kShownPart = 5, kHiddenPart = 13;

// the colour of bone k (exe 0x5e1810: a bone past the actor's blocks reads through null in the exe)
BoneColour boneColour(AnimatedActor& a, uint32_t k) {
    BoneBlock* b = boneBlockOf(a, int(k >> 2));
    if (b == nullptr) notPorted(kStoryPort, "0x5e1810: bone past the bone-block mask");
    return b->colour(k & 3);
}
// exe 0x4866a0: the pitch and the roll leaned toward the turn to `angle` (their speeds eased, capped at +-0x60)
void lean(AnimatedActor& a, GateGuardianFields& f, uint32_t angle) {
    const int roll = a.rotation.z, pitch = a.rotation.x;
    int pitchSpeed = a.spin.x, rollSpeed = f.rollSpeed;
    const int d = angleDiff(uint32_t(int32_t(a.rotation.y)), angle);
    int e = angleDiff(uint32_t(pitch), (uint32_t(d + 0x400) < 0x800u ? 0x60u : uint32_t(-0x60)));
    if (e > 0 && (pitchSpeed += 4) > 0x180) pitchSpeed = 0x180;
    if (e < 0 && (pitchSpeed -= 4) < -0x180) pitchSpeed = -0x180;
    int newPitch = pitch + (pitchSpeed >> 4);
    if (newPitch >= 0x61) newPitch = 0x60;
    else if (newPitch < -0x60) newPitch = -0x60;
    e = angleDiff(uint32_t(roll), d < 1 ? 0x60u : uint32_t(-0x60));
    if (e > 0 && (rollSpeed += 4) > 0x180) rollSpeed = 0x180;
    if (e < 0 && (rollSpeed -= 4) < -0x180) rollSpeed = -0x180;
    int newRoll = roll + (rollSpeed >> 4);
    if (newRoll >= 0x61) newRoll = 0x60;
    else if (newRoll < -0x60) newRoll = -0x60;
    a.rotation.x = int16_t(newPitch);
    a.spin.x = int16_t(pitchSpeed);
    f.rollSpeed = int16_t(rollSpeed);
    a.rotation.z = int16_t(newRoll);
}
// exe 0x488170: the pitch and the roll back to 0 by 8
void unlean(AnimatedActor& a) {
    uint32_t p = uint32_t(int32_t(a.rotation.x)), r = uint32_t(int32_t(a.rotation.z));
    int d = angleDiff(p, 0);
    if (d > 0) p += 8;
    if (d < 0) p -= 8;
    if ((p + 8) * 0x10 < 0x100) p = 0;
    d = angleDiff(r, 0);
    if (d > 0) r += 8;
    if (d < 0) r -= 8;
    a.rotation.x = int16_t(p);
    a.rotation.z = int16_t((r + 8) * 0x10 < 0x100 ? 0u : r);
}
// exe 0x486610: the hover's bob (its vertical speed swings between -0x10 and 0x10)
void bob(AnimatedActor& a, GateGuardianFields& f) {
    if (f.bobs != 1) return;
    a.velocity.y += uint16_t(a.acceleration.y);
    if (a.velocity.y > 0x10) {
        a.velocity.y = 0x10;
        a.acceleration.y = -1;
    }
    if (a.velocity.y < -0x10) {
        a.velocity.y = -0x10;
        a.acceleration.y = 1;
    }
    a.pos.y = int32_t(uint32_t(a.pos.y) + uint32_t(int32_t(a.velocity.y) * 0x1000));
}
// exe 0x487bf0: the room collision (pushed out; back to the previous position when stuck), its results
void collide(const PlayerEnv& env, AnimatedActor& a, GateGuardianFields& f) {
    f.hits = 0;
    uint32_t r = actorTestMove(env, a); // exe 0x40f570
    if (r & 0x20000000) f.hits |= kHitTestedOut;
    if (int32_t(r) >= 0) return;
    r = actorPushOut(env, a); // exe 0x410960
    if (int32_t(r) < 0) a.pos = a.prevPos;
    if (r & 0xf00) f.hits |= kHitWall;
    if (r & 0x3000) f.hits |= kHitFloor;
    if ((f.flags & kReflect) && reflectRotationOffWalls(a.rotation, r) != 0) f.hits |= kHitReflected;
}
// exe 0x486910: the head turned toward the player's bone 1 (from its bone 1), at most 0x80 a tick, within -0x80..0x200
void turnHead(AnimatedActor& a, GateGuardianFields& f) {
    const int s = f.headTurn;
    uint32_t u = 0;
    if (!(f.flags & kNoHeadTurn)) {
        const ShortVec3& target = player().bonePoints[1];
        const int16_t to[3] = {target.x, target.y, target.z};
        const ShortVec4& head = bonePoint(a, 1, kStoryPort); // exe 0x5e1810
        const int16_t from[3] = {head.x, head.y, head.z};
        u = uint32_t(pitchOf(from, to)) + 0x800u;
    }
    int d = angleDiff(uint32_t(s), u);
    if (uint32_t(d + 0x80) > 0xff) u = uint32_t(s + (d < 1 ? -0x80 : 0x80));
    d = angleDiff(0, u);
    if (d >= 0x201) d = 0x200;
    else if (d < -0x80) d = -0x80;
    f.headTurn = int16_t(d);
}
// the dust's ticks of the fade (bit 31 of 0x8212c0 << counter)
bool dustTick(const GateGuardianFields& f) { return int32_t(0x8212c0u << (uint8_t(f.fadeCounter) & 0x1f)) < 0; }
// exe 0x488a10: four sparks of dust around the guardian (the global skid sparks, at its position << the stage shift)
void dustRing(AnimatedActor& a, uint16_t yaw, uint8_t variant) {
    const uint32_t shift = stage().coordinateShift & 0x1f;
    for (int k = 3; k >= 0; --k) {
        const uint32_t slot = allocActorA0(0); // exe 0x5e13b0
        if (!slot) continue;
        EffectActor& e = effectActorAt(slot);
        e.flags = kActorLiveDrawn;
        e.type = typeByte(GlobalEffectType::SkidSparks);
        e.variant = variant;
        e.pos.x = int16_t(uint32_t(uint16_t(a.pos.x.whole)) << shift);
        e.pos.y = int16_t(uint32_t(uint16_t(a.pos.y.whole)) << shift);
        e.pos.z = int16_t(uint32_t(uint16_t(a.pos.z.whole)) << shift);
        auto&& spark = classState<SkidSparkFields>(e);
        spark.yaw = uint16_t(uint32_t(k << 10) + yaw);
    }
}
void dustStep(AnimatedActor& a, GateGuardianFields& f) {
    if (!dustTick(f)) return;
    dustRing(a, uint16_t(uint32_t(f.dustPhase) << 8), kGuardianDustVariant);
    ++f.dustPhase;
}
// exe 0x4881f0: the fade started (drawn, semi-transparent per part)
void fadeStart(AnimatedActor& a, GateGuardianFields& f) {
    a.flags |= kActorDrawn;
    f.fadeCounter = kFadeTicks;
    a.semiTransparency = ((1 & 1) << 4 | (1 & 3)) << 1; // exe 0x5e1a00(actor, 1, 1)
    f.flags |= kFading;
}
// exe 0x488240: a tick of the fade out (each bone from its threshold); at its end hidden
bool fadeOut(AnimatedActor& a, GateGuardianFields& f) {
    const int c = f.fadeCounter;
    a.tint = 0;
    for (uint32_t k = 0; k < kFadeBones; ++k) {
        BoneColour colour = boneColour(a, k);
        const int t = fadeThresholds[k];
        if (t < c) {
            colour = kTintNeutral;
        } else {
            int v = (c - t) + 0x10;
            if (v < 0) v = 0;
            colour = uint16_t(v << 10 | v << 5 | v);
        }
    }
    f.fadeCounter = int8_t(c - 1);
    if (c - 1 < 0) {
        a.flags &= uint8_t(~kActorDrawn);
        return true;
    }
    return false;
}
// exe 0x4882c0: a tick of the fade in; at its end opaque
bool fadeIn(AnimatedActor& a, GateGuardianFields& f) {
    const int c = f.fadeCounter;
    a.tint = 0;
    for (uint32_t k = 0; k < kFadeBones; ++k) {
        BoneColour colour = boneColour(a, k);
        const int t = fadeThresholds[k];
        if (t < c) {
            colour = 0;
        } else {
            int v = t - c;
            if (v > 0x10) v = 0x10;
            colour = uint16_t(v << 10 | v << 5 | v);
        }
    }
    f.fadeCounter = int8_t(c - 1);
    if (c - 1 < 0) {
        a.semiTransparency = ((0 & 1) << 4 | (1 & 3)) << 1; // exe 0x5e1a00(actor, 0, 1)
        return true;
    }
    return false;
}

// exe 0x487e00: the model, still, its hit points from the stage's stat row, its box, hidden
void guardianInit(AnimatedActor& a) {
    if (!bindActorModel(a, 0)) { // exe 0x5e0ba0
        freeActor(a);
        return;
    }
    a.anim = 0;
    a.prevAnim = kNoAnimShown;
    const uint32_t row = statTableRow(actorStatTables().animated, a.type);
    const uint16_t hp = row ? uint16_t(uint32_t(animatedStatsAt(row).hitPoints) << 4) : 0;
    a.velocity.clearWords();
    a.spin.setXY(0, 0);
    a.rotation.z = 0;
    a.rotation.x = 0;
    a.interactionBox = nullptr;
    a.interaction = 0;
    a.airborne = 0;
    a.onPlatform = 0;
    a.platform = nullptr;
    a.actorHit = 0;
    a.maxHitPoints = int16_t(hp);
    a.hitPoints = int16_t(hp);
    a.collisionBox.pointAt(guardianBox.stateAddress());
    a.clearClassBlock();
    a.acceleration.y = -1;
    a.flags &= uint8_t(~kActorDrawn);
    a.parts[kShownPart].flags &= uint8_t(~kPartHidden);
    a.parts[kHiddenPart].flags |= kPartHidden;
    a.enterState(1);
}
// the actions ([0x8922d0 + sub * 4]), keyed on the script's live step sub and tick
// 0 exe 0x487f30: it fades in with dust, then action 1
void guardianFadesIn(AnimatedActor& a, GateGuardianFields& f) {
    if (a.step == 0) {
        f.bobs = 1;
        f.flags = kGuardianFlags;
        fadeStart(a, f); // exe 0x488220 = 0x4881f0 + the vertical speed 0
        a.velocity.y = 0;
        f.dustPhase = 0;
        ++a.step;
    } else if (a.step != 1) {
        return;
    }
    dustStep(a, f);
    if (!fadeIn(a, f)) return;
    a.subState = 1;
    a.enterStep(0);
}
// 1 exe 0x487fb0: in the step sub 2 its animations 1..5 by their ends and the ticks 0x5a / 0x6f
void guardianPoses(AnimatedActor& a, GateGuardianFields& f) {
    const ScriptState& s = block();
    if (s.stepSub != 2) return;
    switch (a.step) {
    case 0:
        f.bobs = 0;
        f.flags = kGuardianFlags;
        a.anim = 1;
        ++a.step;
        [[fallthrough]];
    case 1:
        if (!a.animEnded()) return;
        a.anim = 2;
        ++a.step;
        return;
    case 2:
        if (s.stepTick != 0x5a) return;
        a.anim = 3;
        a.step = 3;
        return;
    case 3:
        if (s.stepTick != 0x6f) return;
        a.anim = 4;
        a.step = 4;
        return;
    case 4:
        if (!a.animEnded()) return;
        a.anim = 5;
        a.subState = 2;
        a.enterStep(0);
        return;
    default: return;
    }
}
// 2 exe 0x488070: hidden from the step sub 3 until 4
void guardianHides(AnimatedActor& a) {
    const uint8_t sub = block().stepSub;
    if (a.step != 0) {
        a.flags &= uint8_t(~kActorDrawn);
        if (sub != 4) return;
        a.subState = 3;
        a.step = 0;
        a.stepCounter = 0;
        return;
    }
    if (sub == 3) a.step = 1;
}
// 3 exe 0x4880b0: shown, animation 6 at 200, from 0xea it fades out with dust
void guardianFadesOut(AnimatedActor& a, GateGuardianFields& f) {
    const int t = block().stepTick;
    switch (a.step) {
    case 0:
        f.bobs = 1;
        a.flags |= kActorDrawn;
        ++a.step;
        [[fallthrough]];
    case 1:
        if (t != 200) return;
        a.anim = 6;
        ++a.step;
        return;
    case 2:
        if (t != 0xea) return;
        fadeStart(a, f); // exe 0x4881f0
        f.dustPhase = 0;
        ++a.step;
        [[fallthrough]];
    case 3:
        dustStep(a, f);
        if (fadeOut(a, f)) ++a.step;
        return;
    default: return;
    }
}
void gateGuardian(const PlayerEnv& env, AnimatedActor& a) {
    GateGuardianFields& f = classState<GateGuardianFields>(a);
    a.prevAnim = a.anim;
    a.tint = kTintNeutral;
    a.savePreviousPose();
    a.savePreviousContact();
    switch (a.state) {
    case 0: guardianInit(a); break;
    case 1: // exe 0x487ed0
        switch (a.subState) { // [0x8922d0 + +9 * 4]
        case 0: guardianFadesIn(a, f); break;
        case 1: guardianPoses(a, f); break;
        case 2: guardianHides(a); break;
        case 3: guardianFadesOut(a, f); break;
        default: notPortedAt(kStoryPort, "0x487d50 action (table 0x8922d0)", a.subState);
        }
        if (f.flags & kNoLean) unlean(a);
        else lean(a, f, uint32_t(int32_t(f.leanTarget)));
        bob(a, f);
        collide(env, a, f);
        turnHead(a, f);
        break;
    default: notPortedAt(kStoryPort, "0x487d50 state (table 0x8922c8)", a.state);
    }
    if (!(a.flags & kActorLive)) return;
    animateAsked(a, 0);
    // exe 0x5f20f0(actor, 0x60, 0x100, 0) while drawn: the shadow (render output)
}

} // namespace

bool st24GateScriptPorted(RoutineId handler) { return handler == RoutineId::St24GateScript; }
bool st24GateScriptTick(const PlayerEnv&, RoutineId handler) {
    if (handler != RoutineId::St24GateScript) return false;
    script14();
    return true;
}

void st24GateGuardianUpdate(const PlayerEnv& env, uint32_t actor) { gateGuardian(env, animatedActorAt(actor)); }

} // namespace od::port
