// ST53 at story 0xf: the sub 0 handler, scripts 0x60 / 0x61 and their cast. See content/late_game/st53_story.hpp.
#include "content/late_game/st53_story.hpp"
#include "content/late_game/st53_story_state.hpp"

#include <optional>

#include "engine/data_source.hpp"
#include "game/actor_animation.hpp"
#include "game/actor_model.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actor_sweeps.hpp"
#include "game/audio_logic.hpp"
#include "game/camera.hpp"
#include "game/cutscene_actor.hpp"
#include "game/fade_state.hpp"
#include "game/global_class_types.hpp"
#include "game/keyframe_tracks.hpp"
#include "game/player/player_input.hpp"
#include "game/player/player_script_control.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/room_scripts.hpp"
#include "game/script_engine.hpp"
#include "game/script_state.hpp"
#include "game/skid_sparks.hpp"
#include "game/stage_files.hpp"
#include "game/stage_state.hpp"
#include "game/story.hpp"
#include "game/story_flags.hpp"
#include "game/transition.hpp"
#include "game/transition_state.hpp"
#include "game/dat_names.hpp"

using namespace od::port::st53_story_state;

namespace od::port {

using namespace st53_story_state;

namespace {

constexpr RoutineId kStepIdle = RoutineId::EmptyRoutine;
constexpr uint32_t kSweepAll = 0xffe;
constexpr uint8_t kSweepFree = 0x80;
constexpr DatFile kNextSceneDat{"ST5303.DAT"};

St53ScriptFields& fields() {
    return scriptState().part<St53ScriptFields>();
}
int32_t tick() { return scriptState().stepTick; }
void nextPhase() { scriptState().subState += 1u; }

// the in-cutscene room swap of the steps: request state 0xff to ST53 sub `sub` (0, 0, 0, yaw 0xc00, fades 0xff / 0xff)
void swapTo(uint8_t sub) { requestTransition(kTransitionWarp, 0x53, sub, 0, 0, 0, 0xc00, kNoFade, kNoFade); }

// the bones' fade thresholds of the fading figure (exe data)
const DataTable<ScalarTable<int8_t, 13>> boneFadeThresholds{TableId::St53StoryBoneFadeThresholds};
constexpr uint32_t kFadingBones = 13;
constexpr uint8_t kSparkRingVariant = 0xf;

uint16_t grey15(int v) { return uint16_t(((v << 5 | v) << 5) | v); }
// exe 0x5e1810: the colour of bone n (the exe writes through it unchecked)
BoneColour boneColour(const AnimatedActor& actor, uint32_t n) {
    BoneBlock* block = boneBlockOf(actor, int(n >> 2));
    if (!block) notPortedAt(kStoryPort, "st53 0x488a90: exe 0x5e1810 null, bone", n);
    return block->colour(n & 3);
}
// exe 0x488d90: the fade started -- drawn, the counter 0x1a, no bursts, drawn per part additive
void fadeStart(AnimatedActor& actor, St53CastFields& cast) {
    actor.setDrawn(true);
    cast.fadeCounter = 0x1a;
    cast.bursts = 0;
    actor.semiTransparency = semiTransparencyBits(1, 1); // exe 0x5e1a00(actor, 1, 1)
}
// exe 0x488ee0(actor, yaw, v): a ring of 4 skid sparks (global effect type 3, variant v) at the actor (y + 0x40), the
// stage shift applied to the position's whole parts, thrown at the yaws i * 0x400 + yaw
void sparkRing(const AnimatedActor& actor, uint32_t yaw, uint8_t variant) {
    for (uint32_t i = 0; i < 4; ++i) {
        const uint32_t slot = allocActorA0(0); // exe 0x5e13b0
        if (slot == 0) continue;
        const uint32_t sh = stage().coordinateShift & 0x1f;
        EffectActor& spark = effectActorAt(slot);
        spark.flags = kActorLiveDrawn;
        spark.type = typeByte(GlobalEffectType::SkidSparks);
        spark.variant = variant;
        spark.pos.x = int16_t(uint32_t(uint16_t(actor.pos.x.whole)) << sh);
        spark.pos.y = int16_t(uint32_t(uint16_t(uint16_t(actor.pos.y.whole) + 0x40)) << sh);
        spark.pos.z = int16_t(uint32_t(uint16_t(actor.pos.z.whole)) << sh);
        classState<SkidSparkFields>(effectActorAt(slot)).yaw = uint16_t((i << 10) + yaw);
    }
}
// exe 0x488dc0: the bones fade in -- each bone's colour from its threshold against the counter (grey min(th - c,
// 0x10), black below); counter < 1: drawn whole again, done
bool fadeInBones(AnimatedActor& actor, St53CastFields& cast) {
    const int c = cast.fadeCounter;
    actor.tint = 0;
    for (uint32_t n = 0; n < kFadingBones; ++n) {
        const int th = boneFadeThresholds[n];
        BoneColour colour = boneColour(actor, n);
        if (c <= th) {
            int v = th - c;
            if (v > 0x10) v = 0x10;
            colour = grey15(v);
        } else {
            colour = 0;
        }
    }
    if (c - 1 < 0) {
        actor.semiTransparency = semiTransparencyBits(0, 1); // exe 0x5e1a00(actor, 0, 1)
        return true;
    }
    cast.fadeCounter = int16_t(c - 1);
    return false;
}
// exe 0x488e50: the reverse (grey max(c - th + 0x10, 0), neutral below); counter < 1: hidden, done
bool fadeOutBones(AnimatedActor& actor, St53CastFields& cast) {
    const int c = cast.fadeCounter;
    actor.tint = 0; // (the exe's two byte stores)
    for (uint32_t n = 0; n < kFadingBones; ++n) {
        const int th = boneFadeThresholds[n];
        BoneColour colour = boneColour(actor, n);
        if (c <= th) {
            int v = c - th + 0x10;
            if (v < 0) v = 0;
            colour = grey15(v);
        } else {
            colour = kTintNeutral;
        }
    }
    if (c - 1 < 0) {
        actor.setDrawn(false);
        return true;
    }
    cast.fadeCounter = int16_t(c - 1);
    return false;
}
// the fading tail of 0x488bf0 / 0x488d10 (fading: sub-state 1): spark rings at counter 25 / 13 / 4, the fade step
template <class F> void fadeTail(AnimatedActor& actor, St53CastFields& cast, F fade) {
    if (actor.subState != 1) return;
    if ((0x8040040u << (cast.fadeCounterLow & 0x1f)) & 0x80000000u) {
        sparkRing(actor, uint32_t(int32_t(cast.bursts)) << 8, kSparkRingVariant);
        cast.bursts = int16_t(uint16_t(cast.bursts) + 1u);
    }
    if (fade(actor, cast)) ++actor.subState;
}
// the tail of 0x4b3770: the yaw ramp (yaw += the turn speed until 0xd80 (sub-state 1) / down to 0xc00 (2)), the
// forward walk (exe 0x5e3690(actor, 0, 0, -speed))
void yawRampWalk(AnimatedActor& actor) {
    const uint8_t mode = actor.subState;
    if ((mode == 1 || mode == 2) && actor.spin.y != 0) {
        actor.rotation.y = int16_t(uint16_t(actor.rotation.y) + uint16_t(actor.spin.y));
        const int y = actor.rotation.y;
        if (mode == 1 ? y >= 0xd80 : y <= 0xc00) {
            actor.rotation.y = int16_t(mode == 1 ? 0xd80 : 0xc00);
            actor.spin.y = 0;
            actor.subState = 0;
        }
    }
    integrateVelocity(actor, 0, 0, -actor.velocity.z); // exe 0x5e3690
}
void say(uint8_t v) { fields().talkCue = v; } // the talking cue

// The cast's step routines [steps + step sub * 4](actor, faces, block), keyed on the cast's copy of the tick k. The
// face helpers: exe 0x5e33d0 / 0x5e33f0 (the eyes' / mouth's sequence) and exe 0x5e3630 / 0x5e3660 (the eyes' /
// mouth's texture cell, the track stopped).
void castStep(const RoutineRef& routine, AnimatedActor& a, St53CastFields& cast) {
    const int32_t k = scriptState().part<CastTimelineTick>().stepTick;
    const auto anim = [&](uint8_t n) { a.anim = n; };
    const auto eyes = [&](uint8_t seq) { cast.faces.first.select(seq, 0); };
    const auto mouth = [&](uint8_t seq) { cast.faces.second.select(seq, 0); };
    const auto eyesCell = [&](uint32_t c) { textureTrackStop(a, cast.faces, FaceTrack::Eyes, c); };
    const auto mouthCell = [&](uint32_t c) { textureTrackStop(a, cast.faces, FaceTrack::Mouth, c); };
    switch (routine.id()) {
    case kStepIdle: return; // ret
    // ---- 0x45c840 (9 / 0): steps 6 / 8 of script 0x60
    case RoutineId::St53CastStep1:
        switch (k) {
        case 0: anim(0); return;
        case 0x50: case 0x8c: case 0x1e0: case 600: eyes(0); return;
        case 0xf6: eyes(0); anim(1); return;
        case 0x172: eyes(0); anim(2); return;
        case 0x33e:
            anim(3);
            eyes(1);
            mouth(0);
            say(5);
            return;
        case 0x366: eyes(1); mouth(1); return;
        case 0x46a: eyesCell(6); return;
        case 0x4c6: mouthCell(0); say(0); return;
        case 0x4fe: eyes(2); anim(4); return;
        default: return;
        }
    case RoutineId::St53CastStep2:
        switch (k) {
        case 0: anim(0); return;
        case 0x14: case 0x78: eyes(0); return;
        case 0xd0: eyesCell(0); mouth(2); say(5); return;
        case 0xf3: mouthCell(0); return;
        case 0xfa: mouth(2); return;
        case 0x10b: mouthCell(0); say(0); return;
        default: return;
        }
    // ---- 0x45cbf0 (9 / 1): steps 0 / 2 of script 0x61
    case RoutineId::St53CastStep3:
        switch (k) {
        case 0: anim(0); return;
        case 0x78: case 0xfa: case 0x17c: case 500: eyes(0); return;
        case 0x28c: eyes(0); anim(5); return;
        case 0x296: mouth(0); say(5); return;
        case 0x2af: mouthCell(0); return;
        case 0x2b7: mouth(0); return;
        case 0x2cd: mouthCell(0); say(0); return;
        case 0x2d2: anim(6); return;
        default: return;
        }
    case RoutineId::St53CastStep4:
        switch (k) {
        case 0: anim(0); return;
        case 0x64: case 0x14a: case 0x1f4: eyes(0); return;
        case 0xe2: anim(7); mouth(1); say(5); return;
        case 0xf1: case 0x168: case 0x19b: case 0x1c2: mouthCell(0); return;
        case 0x101: case 0x177: case 0x1a0: case 0x1ce: mouth(1); return;
        case 0x140: case 0x1e7: mouthCell(0); say(0); return;
        case 0x15d: mouth(1); say(5); return;
        case 0x1fb: say(4); return;
        case 0x239: say(0); return;
        default: return;
        }
    // ---- 0x488a90 (0x24 / 0): steps 2..4 of script 0x60 (fades in, talks, fades out; spark rings)
    case RoutineId::St53CastStep5:
        if (k == 0) {
            a.subState = 0;
            anim(1);
            a.setDrawn(false);
        } else if (k == 0x8f) {
            fadeStart(a, cast);
            a.subState = 1;
        } else if (k == 0xdf) {
            say(4);
        } else if (k == 0x15f) {
            say(0);
        }
        fadeTail(a, cast, fadeInBones);
        return;
    case RoutineId::St53CastStep6:
        switch (k) {
        case 0:
            a.subState = 0;
            anim(1);
            a.spin.y = 0;
            return;
        case 0xa: say(4); return;
        case 0xa0: anim(2); return;
        case 0xec: case 0x129: say(0); return;
        case 0x100: say(4); anim(3); return;
        case 0x11e: anim(1); return;
        default: return;
        }
    case RoutineId::St53CastStep7:
        if (k == 0) {
            a.subState = 0;
            anim(1);
        } else if (k == 0x14) {
            fadeStart(a, cast);
            a.subState = 1;
        }
        fadeTail(a, cast, fadeOutBones);
        return;
    // ---- 0x4991a0 (0x2d / 0): step 1 of script 0x61
    case RoutineId::St53CastStep8:
        if (k == 0) anim(1);
        return;
    // ---- 0x4b35d0 (0x3a / 0): steps 0..5 of script 0x60 (walks forward at its speed, turns by its turn speed)
    case RoutineId::St53CastStep9:
        if (k == 0) {
            anim(1);
            a.velocity.z = 0x39;
        }
        integrateVelocity(a, 0, 0, -a.velocity.z);
        return;
    case RoutineId::St53CastStep10:
        switch (k) {
        case 0:
            anim(1);
            a.prevAnim = kNoAnimShown;
            a.velocity.z = 0x39;
            a.spin.y = 0;
            a.subState = 0;
            break;
        case 0x18:
            say(3);
            a.velocity.z = 0x28;
            a.subState = 1;
            break;
        case 0x30:
            anim(2);
            a.velocity.z = 0x1d;
            break;
        case 0x3d:
            a.velocity.z = 0;
            a.spin.y = 9;
            break;
        case 0x62: case 0x181: case 0x26c: case 0x2cb: case 0x35a: say(0); break;
        case 0x66: anim(3); break;
        case 0x7a: say(3); break;
        case 0x195: case 0x280: case 0x2e7: say(2); break;
        case 0x249:
            anim(4);
            a.spin.y = int16_t(0xfff2);
            a.subState = 2;
            break;
        case 0x265: anim(5); break;
        default: break;
        }
        yawRampWalk(a);
        return;
    case RoutineId::St53CastStep11:
        switch (k) {
        case 0:
            anim(5);
            a.prevAnim = kNoAnimShown;
            a.spin.y = 0;
            return;
        case 0x30: anim(6); say(1); return;
        case 0x96: say(0); return;
        case 0xe0: anim(7); return;
        default: return;
        }
    case RoutineId::St53CastStep12:
        if (k == 0) anim(7);
        else if (k == 0x32) {
            anim(8);
            say(1);
        } else if (k == 0x75) {
            say(0);
        }
        return;
    case RoutineId::St53CastStep13:
        if (k == 0xa) say(1);
        else if (k == 0x19) anim(9);
        else if (k == 0x69) say(0);
        return;
    // ---- 0x4f0ed0 (0x69 / 0): step 8 of script 0x60
    case RoutineId::St53CastStep14:
        switch (k) {
        case 0: anim(0); return;
        case 0xa: eyesCell(0xb); mouth(0); say(2); return;
        case 0x22: case 0xce: mouthCell(8); say(0); return;
        case 0x37: say(4); return;
        case 0x76: say(0); return;
        case 0x8a: eyes(0); mouth(0); say(2); return;
        case 0xa0: mouthCell(8); return;
        case 0xaa: mouth(0); return;
        default: return;
        }
    // ---- 0x4f1220 (0x69 / 1): step 1 of script 0x61
    case RoutineId::St53CastStep15:
        switch (k) {
        case 0: anim(0); return;
        case 0x14: eyesCell(0xc); mouth(0); say(2); return;
        case 0x28: anim(1); return;
        case 0x43: mouthCell(8); say(0); return;
        case 0x57:
            cameraShake(0, 0x600, 0x100); // exe 0x40ea80(0, 0x600, 0x100)
            anim(2);
            return;
        case 0x7c: eyesCell(0xf); mouth(1); say(2); return;
        case 0xc4: say(0); return;
        default: return;
        }
    // ---- 0x4f57d0 (0x72 / 0): steps 6 / 8 of script 0x60
    case RoutineId::St53CastStep16:
        switch (k) {
        case 0: anim(0); return;
        case 0xa: case 0x6e: case 0xac: say(1); return;
        case 0x14: anim(1); return;
        case 0x5a: case 0x98: case 0xdd: case 0x1b9: case 0x265: case 0x56f: say(0); return;
        case 0xf1: eyesCell(0); mouth(0); anim(2); say(3); return;
        case 0xfa: case 0x116: case 0x131: mouthCell(0); return;
        case 0x100: case 0x12b: case 0x13c: mouth(0); return;
        case 0x157: mouthCell(0); say(0); return;
        case 0x16d: anim(3); say(2); return;
        case 0x1cd: eyesCell(3); mouth(1); anim(4); say(3); return;
        case 0x1d3: mouthCell(3); return;
        case 0x1e0: mouth(1); return;
        case 0x204: mouthCell(3); say(0); return;
        case 0x216: say(2); return;
        case 0x279: eyesCell(3); mouth(2); anim(5); say(3); return;
        case 0x292: anim(6); return;
        case 0x2a0: mouthCell(6); return;
        case 0x2b8: mouth(2); return;
        case 0x31e: mouthCell(6); say(0); return;
        case 0x4da: say(3); return;
        case 0x4fc: anim(7); return;
        default: return;
        }
    case RoutineId::St53CastStep17:
        switch (k) {
        case 0: anim(0); return;
        case 0xa: eyesCell(6); mouth(3); say(3); return;
        case 0x1e: case 0x84: mouthCell(6); return;
        case 0x2b: case 0x95: mouth(3); return;
        case 0x4e: case 0xc1: mouthCell(6); say(0); return;
        case 0x6b: mouth(3); say(3); return;
        case 0xad: anim(8); return;
        default: return;
        }
    // ---- 0x4f5f70 (0x72 / 1): steps 0 / 2 of script 0x61
    case RoutineId::St53CastStep18:
        switch (k) {
        case 0: anim(0); return;
        case 0x1e: anim(9); eyesCell(1); mouth(0); say(3); return;
        case 0x54: case 0x154: case 0x254: mouthCell(0); return;
        case 0x82: case 0x162: case 0x264: mouth(0); return;
        case 0xa2: case 0x190: case 0x27c: mouthCell(0); say(0); return;
        case 0xbf: say(2); return;
        case 0x11d: case 0x1de: case 0x234: say(0); return;
        case 0x131: eyesCell(1); mouth(0); anim(0xa); say(3); return;
        case 0x198: case 0x1f2: say(4); return;
        case 0x248: eyesCell(1); mouth(0); anim(0xb); say(3); return;
        default: return;
        }
    case RoutineId::St53CastStep19:
        switch (k) {
        case 0: case 0x92: anim(0xc); return;
        case 0x52: eyesCell(7); mouth(1); anim(0xd); say(3); return;
        case 0x97: mouth(2); return;
        case 0xca: mouthCell(7); say(0); return;
        default: return;
        }
    default: notPortedAt(kStoryPort, "st53 cast step", routine.pointerValue());
    }
}

// The class shape (all eleven): the previous-pose copies, state 0 (bind the model of its variant -- none: freed --,
// the class block cleared, its box, anim 0 (none shown), on the ground, the class extras, state 1 and its first tick),
// state 1 = [steps + step sub * 4](actor) while the cast's step id is not the end; the tail (live): the texture tracks
// (exe 0x5e34f0) for the textured ones, the animation (its key / flags, the fading figure's after its fade bytes).
struct Cast {
    TableId states, box, steps;
    std::optional<TableId> t0, t1; // the texture track tables (none: untextured class, no tracks, no exe 0x5e34f0)
    uint8_t c0, c1;    // the initial cells
    bool clearInteraction; // the interaction byte cleared
    bool clearMotion;  // the pitch / roll and the velocities cleared
    bool fader;        // the fading figure (its animation key / flags after its fade bytes)
};
// a class's step routines by the step sub (exe data)
RoutineRef castStepRoutine(const Cast& c, uint8_t sub) { return stateRoutine(dataScalar<uint32_t>(stateAddress(c.steps) + uint32_t(sub) * 4)); }
void castRun(AnimatedActor& actor, const Cast& c) {
    St53CastFields& cast = classState<St53CastFields>(actor);
    castSavePose(actor);
    switch (actor.state) {
    case 0:
        if (!bindActorModel(actor, actor.variant)) { // exe 0x5e0bc0(actor, actor + 6)
            freeActor(actor);                        // exe 0x5e1490
            break;
        }
        actor.clearClassBlock(); // exe 0x40dc40
        actor.collisionBox.pointAt(c.box);
        if (c.clearInteraction) actor.interaction = 0;
        actor.anim = 0;
        actor.prevAnim = kNoAnimShown;
        actor.airborne = 0;
        if (c.clearMotion) {
            actor.rotation.x = 0;
            actor.rotation.z = 0;
            actor.velocity.clearWords();
        }
        if (c.t0) {
            cast.faces.init(stateAddress(*c.t0), stateAddress(*c.t1)); // exe 0x5e33a0
            textureTrackStop(actor, cast.faces, FaceTrack::Eyes, c.c0);
            textureTrackStop(actor, cast.faces, FaceTrack::Mouth, c.c1);
        }
        actor.enterState(1);
        [[fallthrough]];
    case 1:
        if (scriptState().part<CastTimelineStep>().stepId != kScriptStepEnd) {
            if (const RoutineRef routine = castStepRoutine(c, scriptState().part<CastTimelineStep>().stepSub)) castStep(routine, actor, cast);
        }
        break;
    default: notPortedAt(kStoryPort, "st53 cast state, class states", stateAddress(c.states));
    }
    if (!(actor.flags & kActorLive)) return;
    if (c.t0) textureTracksTick(actor, cast.faces);
    if (c.fader) castAnimate(actor, cast.faderKey, cast.faderFlags, kCastHoldPose);
    else castAnimate(actor, cast.animKey, cast.castFlags, kCastHoldPose);
}

constexpr Cast kFacedTalker60{TableId::St53StoryFacedTalker60States, TableId::St53StoryFacedTalker60Box, TableId::St53StoryFacedTalker60Steps, TableId::St53StoryFacedTalker60FirstTracks, TableId::St53StoryFacedTalker60SecondTracks, 0, 0, false, false, false};
constexpr Cast kFacedTalker61{TableId::St53StoryFacedTalker61States, TableId::St53StoryFacedTalker61Box, TableId::St53StoryFacedTalker61Steps, TableId::St53StoryFacedTalker61FirstTracks, TableId::St53StoryFacedTalker61SecondTracks, 0, 0, true, false, false};
constexpr Cast kFadingFigure{TableId::St53StoryFadingFigureStates, TableId::St53StoryFadingFigureBox, TableId::St53StoryFadingFigureSteps, {}, {}, 0, 0, false, true, true};
constexpr Cast kStandingFigure{TableId::St53StoryStandingFigureStates, TableId::St53StoryStandingFigureBox, TableId::St53StoryStandingFigureSteps, {}, {}, 0, 0, true, false, false};
constexpr Cast kWalker{TableId::St53StoryWalkerStates, TableId::St53StoryWalkerBox, TableId::St53StoryWalkerSteps, {}, {}, 0, 0, false, true, false};
constexpr Cast kMaskedTalker60{TableId::St53StoryMaskedTalker60States, TableId::St53StoryMaskedTalker60Box, TableId::St53StoryMaskedTalker60Steps, TableId::St53StoryMaskedTalker60FirstTracks, TableId::St53StoryMaskedTalker60SecondTracks, 8, 8, true, false, false};
constexpr Cast kMaskedTalker61{TableId::St53StoryMaskedTalker61States, TableId::St53StoryMaskedTalker61Box, TableId::St53StoryMaskedTalker61Steps, TableId::St53StoryMaskedTalker61FirstTracks, TableId::St53StoryMaskedTalker61SecondTracks, 8, 8, true, false, false};
constexpr Cast kLeadTalker60{TableId::St53StoryLeadTalker60States, TableId::St53StoryLeadTalker60Box, TableId::St53StoryLeadTalker60Steps, TableId::St53StoryLeadTalker60FirstTracks, TableId::St53StoryLeadTalker60SecondTracks, 0, 0, false, false, false};
constexpr Cast kLeadTalker61{TableId::St53StoryLeadTalker61States, TableId::St53StoryLeadTalker61Box, TableId::St53StoryLeadTalker61Steps, TableId::St53StoryLeadTalker61FirstTracks, TableId::St53StoryLeadTalker61SecondTracks, 0, 0, true, false, false};
constexpr Cast kExtraA{TableId::St53StoryExtraAStates, TableId::St53StoryExtraABox, TableId::St53StoryExtraASteps, {}, {}, 0, 0, false, false, false};
constexpr Cast kExtraB{TableId::St53StoryExtraBStates, TableId::St53StoryExtraBBox, TableId::St53StoryExtraBSteps, {}, {}, 0, 0, false, false, false};

// ---- the scripts' shared parts -------------------------------------------------------------------------------------
// the opening of 0x60 / 0x61 (0x537280 / 0x5376d0): sweep, the camera sequence, the player scripted and posed, the
// voice; the steps when the voice started (0x60 also clears its "DAT loaded")
void scriptOpen(uint32_t camera, uint32_t steps, uint32_t voice, bool clearDatLoaded) {
    ScriptState& s = scriptState();
    switch (s.subState) {
    case 0:
        actorsSweep(kSweepAll, kSweepFree);     // exe 0x5e05d0
        scriptSequenceStart(camera, steps);     // exe 0x511390
        scriptReplayPadOn();                    // exe 0x5117f0
        player().setDrawn(false);
        s.flags |= kScriptUnskippable;
        scriptSequenceTick();                   // exe 0x5114d0
        nextPhase();
        [[fallthrough]];
    case 1: {
        if (playerToScriptIfFree() != 0) return; // exe 0x572300
        Player& p = player();
        p.clearDirectionOffsetWords();
        p.idleFaces[0] = 0;
        p.idleFaces[1] = 0;
        p.pos.x = 0;
        p.pos.y = 0;
        p.pos.z = 0;
        p.rotation.y = 0xc00;
        playerScriptAnimId(0, 0, 1); // exe 0x5723d0(0, 0, 1)
        voiceClipPlay(voice);
        nextPhase();
        return;
    }
    case 2:
        if (!voiceClipPlaying()) return; // exe 0x601240
        if (clearDatLoaded) fields().datLoaded = 0;
        s.enterState(CutsceneState::Steps);
        s.flags &= uint8_t(~kScriptUnskippable);
        return;
    default: return;
    }
}
// the shared end: the request to ST0E sub 2 (0, 0, 0, yaw 0xc00, fades 2 / 0x12), then the player back
void requestSt0e() { requestTransition(kTransitionAccepted, 0xe, 2, 0, 0, 0, 0xc00, 2, 0x12); }
void scriptClose() {
    playerBackToPlay();   // exe 0x5723c0
    scriptReplayPadOff(); // exe 0x511890
    Player& p = player();
    p.airborne = 0;
    p.animPaused = 0;
    p.setDrawn(true);
    scriptEndBlock(); // exe 0x5116a0
}

// ---- script 0x60 (0x537260, states 0x8ac2c8 = 0x537280 / 0x537c50 / 0x537360; camera 0x8ac044, steps 0x8ac278:
// 0..4 camera only, 5 = 0x5374b0, 6 = 0x5375d0, 7 = 0x537640, 8 camera only): ST53 sub 0 at story 0xf (handler
// 0x58c0c0, flag 0x5a0). Voice 9 over the opening; step 5 waits tick 0xb4, fades the voice, loads DAT 0x144, plays
// voice 10 and swaps to ST53 sub 1; step 6 swaps to sub 2 at tick 0x579, step 7 back to sub 1 at 0xda. End: the
// DAT load (unless step 5 did it), the request to ST0E sub 2 (fades 2 / 0x12).
const DataTable<RoutineTable<3>> snowTrackStates{TableId::St53StorySnowTrackStates};
enum class LoadPhase : uint8_t { Wait = 0, FadeVoice = 1, RequestDat = 2, WaitDat = 3, WaitVoice = 4 };
void snowTrackStep(const RoutineRef& routine) {
    ScriptState& s = scriptState();
    const int32_t t = tick();
    switch (routine.id()) {
    case kStepIdle: return;
    case RoutineId::St53SnowTrackStep1:
        if (t == 0) s.subState = uint8_t(LoadPhase::Wait);
        else if (t == 0xb4) s.subState = uint8_t(LoadPhase::FadeVoice);
        switch (LoadPhase(s.subState)) {
        case LoadPhase::FadeVoice:
            s.flags |= kScriptUnskippable;
            if (voiceClipPlaying()) voiceClipFadeOut(8); // exe 0x601220
            nextPhase();
            [[fallthrough]];
        case LoadPhase::RequestDat:
            if (!voiceClipIdle()) return;
            datLoadRequest(kNextSceneDat); // exe 0x40efe0
            nextPhase();
            return;
        case LoadPhase::WaitDat:
            if (fileLoads().loading & kFileLoading) return;
            fields().datLoaded = 1;
            voiceClipPlay(10); // exe 0x601050
            nextPhase();
            return;
        case LoadPhase::WaitVoice:
            if (!voiceClipPlaying()) return;
            swapTo(1);
            scriptSequenceSkip(1); // exe 0x5115c0(1)
            s.subState = 0;
            s.flags &= uint8_t(~kScriptUnskippable);
            return;
        default: return;
        }
    case RoutineId::St53SnowTrackStep2:
        if (t != 0x579) return;
        swapTo(2);
        scriptSequenceSkip(1);
        return;
    case RoutineId::St53SnowTrackStep3:
        if (t != 0xda) return;
        swapTo(1);
        scriptSequenceSkip(1);
        return;
    default: notPortedAt(kStoryPort, "script 0x60 step", routine.pointerValue());
    }
}
void snowTrackScript() { // 0x537260
    ScriptState& s = scriptState();
    switch (snowTrackStates->idAt(s.state)) {
    case RoutineId::St53SnowTrackOpening: scriptOpen(stateAddress(TableId::St53StorySnowTrackCamera), stateAddress(TableId::St53StorySnowTrackSteps), 9, true); return;
    case RoutineId::ScriptCastStepsState: scriptStepsStateForCast(snowTrackStep); return; // game/script_engine
    case RoutineId::St53SnowTrackEnd:
        switch (s.subState) {
        case 0:
            padVibrate(0, 0x1000000); // exe 0x55cd10
            if (voiceClipPlaying()) voiceClipFadeOut(8);
            nextPhase();
            return;
        case 1:
            if (!voiceClipIdle()) return;
            if (fields().datLoaded != 0) {
                s.subState = 3;
                return;
            }
            datLoadRequest(kNextSceneDat);
            s.subState = 2;
            return;
        case 2:
            if (fileLoads().loading & kFileLoading) return;
            fields().datLoaded = 1;
            nextPhase();
            return;
        case 3:
            if (fadeState().busy != 0) return;
            requestSt0e();
            nextPhase();
            return;
        case 4:
            if (transitionRequest().state != kTransitionNone) return;
            scriptClose();
            return;
        default: return;
        }
    default: notPortedAt(kStoryPort, "script 0x60 state", s.state);
    }
}

// ---- script 0x61 (0x5376b0, states 0x8ac448 = 0x5376d0 / 0x537c50 / 0x5377b0; camera 0x8ac328, steps 0x8ac428 =
// 0x537890 / 0x537900 / 0x5379e0): ST53 sub 1 at story 0xf (handler 0x58c100, flag 0x5a1; content/late_game/ch4_story).
// Voice 0xb; step 0 swaps to ST53 sub 2 at tick 0x311; step 1 fades 0x12 at tick 0xbb, waits the fade + 0x1f ticks,
// swaps back to sub 1 (fade 2); step 2 (0x24d ticks) plays the engine sound 0x89 fading (attenuation 0x16..0x29),
// 0x8b at 0x30. End: the request to ST0E sub 2.
const DataTable<RoutineTable<3>> rideStates{TableId::St53StoryRideStates};
void rideStep(const RoutineRef& routine) {
    ScriptState& s = scriptState();
    const int32_t t = tick();
    switch (routine.id()) {
    case RoutineId::St53RideStep1:
        if (t != 0x311) return;
        swapTo(2);
        scriptSequenceSkip(1);
        return;
    case RoutineId::St53RideStep2:
        if (t == 0) {
            s.subState = 0;
            fields().waitTicks = 0;
        } else if (t == 0xbb) {
            scriptFade(0x12, 0);
            nextPhase();
        }
        switch (s.subState) {
        case 1:
            if (fadeState().busy != 0) return;
            s.subState = 2;
            [[fallthrough]];
        case 2: {
            const int32_t c = fields().waitTicks;
            fields().waitTicks = c + 1;
            if (c <= 0x1e) return;
            nextPhase();
            [[fallthrough]];
        }
        case 3:
            swapTo(1);
            scriptFade(2, 0);
            scriptSequenceSkip(1);
            nextPhase();
            return;
        default: return;
        }
    case RoutineId::St53RideStep3: {
        // exe 0x5d5340(id, 0, attenuation) by tick (jump table 0x537abc / 0x537aec)
        struct Cue {
            int32_t t;
            uint16_t id;
            int16_t att;
        };
        static constexpr Cue kCues[] = {{5, 0x89, 0x16},   {9, 0x89, 0x18},   {0xc, 0x89, 0x1a}, {0x10, 0x89, 0x1c},
                                        {0x14, 0x89, 0x1e}, {0x19, 0x89, 0x20}, {0x1e, 0x89, 0x23}, {0x23, 0x89, 0x25},
                                        {0x2a, 0x89, 0x29}, {0x30, 0x8b, 0x2c}};
        if (t == 0) {
            s.subState = 0;
            fields().waitTicks = 0;
            return;
        }
        for (const Cue& q : kCues)
            if (q.t == t) soundPlayPanned(q.id, 0, q.att);
        return;
    }
    default: notPortedAt(kStoryPort, "script 0x61 step", routine.pointerValue());
    }
}
void rideScript() { // 0x5376b0
    ScriptState& s = scriptState();
    switch (rideStates->idAt(s.state)) {
    case RoutineId::St53RideOpening: scriptOpen(stateAddress(TableId::St53StoryRideCamera), stateAddress(TableId::St53StoryRideSteps), 0xb, false); return;
    case RoutineId::ScriptCastStepsState: scriptStepsStateForCast(rideStep); return; // game/script_engine
    case RoutineId::St53RideEnd:
        switch (s.subState) {
        case 0:
            padVibrate(0, 0x1000000);
            if (voiceClipPlaying()) voiceClipFadeOut(8);
            nextPhase();
            [[fallthrough]];
        case 1:
            if (!voiceClipIdle() || fadeState().busy != 0) return;
            requestSt0e();
            nextPhase();
            return;
        case 2:
            if (transitionRequest().state != kTransitionNone) return;
            scriptClose();
            return;
        default: return;
        }
    default: notPortedAt(kStoryPort, "script 0x61 state", s.state);
    }
}

void runCast(uint32_t actor, const Cast& c) { castRun(animatedActorAt(actor), c); }

} // namespace

bool st53RoomHandler(RoutineId fn, bool run) {
    if (fn != RoutineId::St53StoryFRoomHandler) return false;
    // 0x58c0c0 (ST53 sub 0 at story 0xf, table 0x8cd800): step 0: without flag 0x5a0 script 0x60 + that flag; the next
    // step either way
    RoomScriptState& room = roomScriptState();
    if (!run || room.handlerStep != 0) return true;
    if (!storyFlag(0x5a0)) {
        scriptRun(0x60); // exe 0x511270
        setStoryFlag(0x5a0);
    }
    ++room.handlerStep;
    return true;
}

bool st53ScriptPorted(uint8_t id) {
    const RoutineId h = storyScriptHandler(id).id();
    return h == RoutineId::St53SnowTrackScript || h == RoutineId::St53RideScript;
}
bool st53ScriptTick(const PlayerEnv&, uint8_t id) {
    switch (storyScriptHandler(id).id()) {
    case RoutineId::St53SnowTrackScript: snowTrackScript(); return true;
    case RoutineId::St53RideScript: rideScript(); return true;
    default: return false;
    }
}

void script60FacedTalkerUpdate(const PlayerEnv&, uint32_t actor) { runCast(actor, kFacedTalker60); }
void script61FacedTalkerUpdate(const PlayerEnv&, uint32_t actor) { runCast(actor, kFacedTalker61); }
void script60FadingFigureUpdate(const PlayerEnv&, uint32_t actor) { runCast(actor, kFadingFigure); }
void script61StandingFigureUpdate(const PlayerEnv&, uint32_t actor) { runCast(actor, kStandingFigure); }
void script60WalkerUpdate(const PlayerEnv&, uint32_t actor) { runCast(actor, kWalker); }
void script60MaskedTalkerUpdate(const PlayerEnv&, uint32_t actor) { runCast(actor, kMaskedTalker60); }
void script61MaskedTalkerUpdate(const PlayerEnv&, uint32_t actor) { runCast(actor, kMaskedTalker61); }
void script60LeadTalkerUpdate(const PlayerEnv&, uint32_t actor) { runCast(actor, kLeadTalker60); }
void script61LeadTalkerUpdate(const PlayerEnv&, uint32_t actor) { runCast(actor, kLeadTalker61); }
void script60ExtraAUpdate(const PlayerEnv&, uint32_t actor) { runCast(actor, kExtraA); }
void script60ExtraBUpdate(const PlayerEnv&, uint32_t actor) { runCast(actor, kExtraB); }

} // namespace od::port
