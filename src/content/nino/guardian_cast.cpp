// The cutscene casts of the ruin under Nino's guardian (ST35), one routine per exe routine; the names are hypotheses
// from the observed behaviour. Their scripts (content/nino/st35_scripts) cue them through their sub-state.
//   0x4b2a00  pool 0x20 type 0x3a sub 1 (script 0x15; states [0x894530]): the cues 1..7 -> its animations
//   0x4b2ba0  type 0x3a sub 2 (script 0x16; [0x894538]): the cues 1 / 2 its animations, 3 it backs away 0x80 ticks
//   0x4b2d00  type 0x3a sub 3 (script 0x17; [0x894554]): it falls in, lands (a shake, the pad, 16 dust sparks), its
//             landing animations on the cues
//   0x4b2f50  type 0x3a sub 4 (script 0x19; [0x89457c]): sparks at its bones, then on the cues its animations, a white
//             star at its head, its bones dissolving
//   0x488350  pool 0x20 type 0x24 sub 1 (script 0x19; [0x892300]): hidden, its bones fading in, its animations on the
//             cues, its bones dissolving
#include "content/nino/guardian_cast.hpp"
#include "content/nino/guardian_cast_state.hpp"
#include "engine/data_source.hpp"
#include "game/actor_state.hpp"
#include "game/stage_smoke.hpp"

#include <algorithm>

#include "game/actor_animation.hpp"
#include "game/actor_model.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/angles.hpp"
#include "game/camera.hpp"
#include "game/draw_commands.hpp"
#include "game/vectors.hpp"
#include "game/global_class_types.hpp"
#include "game/player/player_input.hpp"
#include "game/port_gap.hpp"
#include "game/projection.hpp"
#include "game/random.hpp"
#include "game/skid_sparks.hpp"
#include "game/stage_state.hpp"
#include "game/story.hpp"

namespace od::port {

using namespace guardian_cast_state;

namespace {

int16_t t16(int64_t v) { return int16_t(uint16_t(uint64_t(v))); }

enum class CastState : uint8_t { Init = 0, Cue = 1, Act = 2 };
constexpr uint8_t kSparkleVariant = 0xf, kDustVariant = 0x1a, kBoneSparkVariant = 0x12;
constexpr uint8_t kBlendOn = ((1u << 4) | 1u) << 1, kBlendOff = (0u | 1u) << 1; // exe 0x5e1a00(1, 1) / (0, 1)
const DataTable<ScalarTable<int8_t, 15>> dissolveOrder{TableId::GuardianCastDissolveOrder}; // the bones' thresholds of the fade
const DataTable<ScalarTable<int8_t, 13>> fadingOrder{TableId::GuardianCastFadingOrder};
constexpr uint32_t kSparkleMask = 0x8212c0; // the bit pattern of the fade's ticks that sends sparkles

// the head of the cast classes: the animation of the last tick, the previous pose
void castHead(AnimatedActor& cast) {
    cast.prevAnim = cast.anim;
    cast.savePreviousPose();
}
// the tail: a new animation started at its key's frame byte, else stepped
void castTail(AnimatedActor& cast) {
    if (!(cast.flags & kActorLive)) return;
    if (cast.anim != cast.prevAnim) animSetPose(cast, cast.anim, cast.animKey.frame); // exe 0x5e1c30
    else animStepPose(cast);                                                         // exe 0x5e1e80
}
// the common state 0 (exe 0x4b2aa0 / 0x4b2c40 / 0x4b2da0 / 0x4b3010): model variant 0 (none: freed), the box (0:
// kept), the rotation's x / z and the velocity cleared, the animation, the cues
bool castInit(AnimatedActor& cast, uint8_t anim, uint32_t box) {
    if (!bindActorModel(cast, 0)) {
        freeActor(cast);
        return false;
    }
    if (box) cast.collisionBox.pointAt(box);
    cast.rotation.x = 0;
    cast.rotation.z = 0;
    cast.velocity.clearWords();
    cast.anim = anim;
    cast.prevAnim = 0xff;
    cast.animKey.frame = 0;
    cast.enterStateKeepingStep(uint8_t(CastState::Cue));
    return true;
}
// a cue's animation (its key's frame byte cleared, the cue taken)
void cueAnim(AnimatedActor& cast, uint8_t anim) {
    cast.anim = anim;
    cast.animKey.frame = 0;
    cast.subState = 0;
}
// the colour word of a bone's point (exe 0x5e1810's point: a null block is the exe's crash)
BoneColour boneColour(const AnimatedActor& cast, uint32_t bone) {
    BoneBlock* block = boneBlockOf(cast, int(bone >> 2));
    if (!block) notPortedAt(kStoryPort, "0x5e1810 returned null, bone", bone);
    return block->colour(bone & 3);
}

// exe 0x4b2a00: the cues 1..7 -> its animations
void guardianWakes(AnimatedActor& cast) {
    castHead(cast);
    switch (CastState(cast.state)) {
    case CastState::Init: castInit(cast, 0x12, 0); break; // exe 0x4b2aa0
    case CastState::Cue: {                                 // exe 0x4b2af0
        static constexpr uint8_t kAnim[7] = {0x14, 0x15, 0x13, 0x16, 0x17, 0x18, 0x19};
        const uint8_t c = cast.subState;
        if (c >= 1 && c <= 7) cueAnim(cast, kAnim[c - 1]);
        else cast.subState = 0;
        break;
    }
    default: notPortedAt(kStoryPort, "0x4b2a00 state", cast.state);
    }
    castTail(cast);
}
// exe 0x4b2ba0: the cues 1 / 2 its animations, 3 it backs away (8 a tick, 0x80 ticks)
void guardianBeaten(AnimatedActor& cast) {
    castHead(cast);
    CastFields& fields = classState<CastFields>(cast);
    switch (CastState(cast.state)) {
    case CastState::Init: castInit(cast, 0x1c, 0); break; // exe 0x4b2c40
    case CastState::Cue:                                   // exe 0x4b2c90
        switch (cast.subState) {
        case 1: cueAnim(cast, 0x1a); break;
        case 2: cueAnim(cast, 0x1b); break;
        case 3:
            fields.ticks = 0x80;
            cast.enterStateKeepingStep(uint8_t(CastState::Act));
            break;
        default: break;
        }
        break;
    case CastState::Act: // exe 0x4b2ce0
        fields.ticks = int16_t(uint16_t(fields.ticks) - 1u);
        if (fields.ticks == 0) cast.enterStateKeepingStep(uint8_t(CastState::Cue));
        else cast.pos.z = int32_t(uint32_t(cast.pos.z) + 0xfff80000u);
        break;
    default: notPortedAt(kStoryPort, "0x4b2ba0 state", cast.state);
    }
    castTail(cast);
}
// exe 0x4b2d00: it falls in (0x80, + 0x30 a tick to 0x300), lands (a shake, the pad, 16 dust sparks), then its landing
// animations on the cues
void guardianDropsIn(const PlayerEnv& env, AnimatedActor& cast) {
    castHead(cast);
    switch (CastState(cast.state)) {
    case CastState::Init: castInit(cast, 0x1d, stateAddress(TableId::GuardianCastGuardianBox)); break; // exe 0x4b2da0
    case CastState::Cue: {                                        // exe 0x4b2df0
        if (cast.subState == 0) {
            cast.subState = 1;
            cast.velocity.y = 0x80;
        } else if (cast.subState == 1) {
            cast.velocity.y = int16_t(uint16_t(cast.velocity.y) + 0x30u);
            if (cast.velocity.y > 0x300) cast.velocity.y = 0x300;
        }
        integrateVelocity(cast, 0, cast.velocity.y, 0);
        if (int32_t(actorTestMove(env, cast)) >= 0) break; // exe 0x40f570
        actorPushOut(env, cast);                            // exe 0x410960
        cameraShake(true, 0x800, 0x40);
        padVibrate(0x601, 0x618e0);
        for (uint32_t i = 0; i < 0x10; ++i) {
            const uint32_t e = allocActorA0(0); // exe 0x5e13b0: the dust (the global effect type 3)
            if (!e) continue;
            EffectActor& dust = effectActorAt(e);
            SkidSparkFields& spark = classState<SkidSparkFields>(effectActorAt(e));
            dust.flags = kActorLive;
            dust.setTypeWord(uint32_t(typeByte(GlobalEffectType::SkidSparks)) | uint32_t(kDustVariant) << 16);
            dust.loopFlags |= kActorLateLoop;
            dust.pos.x = cast.pos.x.whole;
            dust.pos.y = cast.pos.y.whole;
            dust.pos.z = cast.pos.z.whole;
            spark.spareWord = 0;
            spark.yaw = uint16_t((i << 8) + (rngStep() & 0xff));
        }
        cast.anim = 0x1e;
        cast.prevAnim = 0xff;
        cast.animKey.frame = 0;
        cast.enterStateKeepingStep(uint8_t(CastState::Act));
        break;
    }
    case CastState::Act: // exe 0x4b2f00
        if (cast.subState == 1) {
            cast.anim = 0x1f;
            cast.animKey.frame = 0;
            cast.subState = 2;
        } else if (cast.subState == 2 && cast.animKey.flags == 0xff) {
            cueAnim(cast, 0x20);
        }
        break;
    default: notPortedAt(kStoryPort, "0x4b2d00 state", cast.state);
    }
    castTail(cast);
}
// exe 0x488ee0(actor, base, variant): 4 sparkles (the global effect type 3) around the actor (0x40 up, << the stage
// shift)
void sparkles(const AnimatedActor& cast, uint32_t base, uint8_t variant) {
    for (uint32_t i = 0; i < 4; ++i) {
        const uint32_t e = allocActorA0(0);
        if (!e) continue;
        const int sh = stage().coordinateShift & 0x1f;
        EffectActor& sparkle = effectActorAt(e);
        sparkle.flags = kActorLiveDrawn;
        sparkle.type = typeByte(GlobalEffectType::SkidSparks);
        sparkle.variant = variant;
        sparkle.pos.x = int16_t(uint16_t(uint32_t(uint16_t(cast.pos.x.whole)) << sh));
        sparkle.pos.y = int16_t(uint16_t((uint32_t(uint16_t(cast.pos.y.whole)) + 0x40u) << sh));
        sparkle.pos.z = int16_t(uint16_t(uint32_t(uint16_t(cast.pos.z.whole)) << sh));
        classState<SkidSparkFields>(effectActorAt(e)).yaw = uint16_t((i << 10) + base);
    }
}
// exe 0x488580: shown, the fade's 0x1a ticks, the blend on
void fadeStart(AnimatedActor& cast, CastFields& fields) {
    cast.flags |= kActorDrawn;
    fields.ticks = 0x1a;
    fields.sparkles = 0;
    cast.semiTransparency = kBlendOn;
}
// the sparkles on the ticks of the fade's bit pattern
void fadeSparkles(const AnimatedActor& cast, CastFields& fields) {
    if (int32_t(kSparkleMask << (uint8_t(fields.ticks) & 31)) < 0) {
        sparkles(cast, uint32_t(int32_t(fields.sparkles) << 8), kSparkleVariant);
        fields.sparkles = int16_t(uint16_t(fields.sparkles) + 1u);
    }
}
// exe 0x488640 / 0x4b3210(actor, block, thresholds, bones): the bones dissolve one after the other (their colour
// words); at the end the actor hidden, 1
template <size_t N>
int dissolve(AnimatedActor& cast, CastFields& fields, const DataTable<ScalarTable<int8_t, N>>& order, uint32_t bones) {
    int c = fields.ticks;
    cast.tint = 0;
    for (uint32_t i = 0; i < bones; ++i) {
        const int thr = order[i];
        BoneColour colour = boneColour(cast, i);
        if (c > thr) {
            colour = kTintNeutral;
        } else {
            int k = c - thr + 0x10;
            if (k < 0) k = 0;
            colour = uint16_t((((k << 5) | k) << 5) | k);
        }
    }
    if (--c < 0) {
        cast.flags &= uint8_t(~kActorDrawn);
        return 1;
    }
    fields.ticks = int16_t(c);
    return 0;
}
// exe 0x4885b0: the bones appear one after the other; at the end the blend off, 1
int fadeIn(AnimatedActor& cast, CastFields& fields) {
    int c = fields.ticks;
    cast.tint = 0;
    for (uint32_t i = 0; i < fadingOrder.size(); ++i) {
        const int thr = fadingOrder[i];
        BoneColour colour = boneColour(cast, i);
        if (c <= thr) {
            int k = thr - c;
            if (k > 0x10) k = 0x10;
            colour = uint16_t((((k << 5) | k) << 5) | k);
        } else {
            colour = 0;
        }
    }
    if (--c < 0) {
        cast.semiTransparency = kBlendOff;
        return 1;
    }
    fields.ticks = int16_t(c);
    return 0;
}
// exe 0x4b32a0: a spark emitter (the stage's static type 3 variant 0x12) at a random point around the bone
void boneSpark(const AnimatedActor& cast, CastFields& fields) {
    BoneBlock* block = boneBlockOf(cast, int(fields.sparkBone >> 2)); // exe 0x5e1780
    if (!block) notPorted(kStoryPort, "0x5e1780: no such bone block");
    const ShortVec4& point = block->points[uint8_t(fields.sparkBone) & 3u];
    const uint32_t e = allocActor60(kActorStageClass);
    if (!e) {
        fields.sparkBone = 0;
        return;
    }
    StaticActor& emitter = staticActorAt(e);
    SmokeParams params(actorHeaderAt(e));
    emitter.flags = kActorLive;
    emitter.loopFlags |= kActorLateLoop;
    emitter.setTypeWord(uint32_t(kSmokeType) | uint32_t(kBoneSparkVariant) << 16);
    emitter.pos.x = int32_t(uint32_t(int32_t(rngStep() & 0x7f) + point.x - 0x40) << 16);
    emitter.pos.y = int32_t(uint32_t(int32_t(rngStep() & 0x7f) + point.y - 0x40) << 16);
    emitter.pos.z = int32_t(uint32_t(int32_t(rngStep() & 0x7f) + point.z - 0x40) << 16);
    params.driftX = fields.sparkDrift;
    params.driftZ = fields.sparkCount;
    params.lifeBytes[0] = 0xe0;
    params.lifeBytes[1] = 0;
    fields.sparkBone = 0;
}
// exe 0x4b3390: a white 4-spike star on the screen at the head (OT: DR_MODE + POLY_G4 x 4). Port: dc::otQuadG4 with the
// corners as the head point + camera-plane offsets (p pixels = p * (0x20 << shift) / w camera-input units)
void headStar(const AnimatedActor& cast) {
    BoneBlock* block = boneBlockOf(cast, 0);
    if (!block) notPorted(kStoryPort, "0x5e1780: no such bone block");
    const ShortVec4& head = block->points[2];
    const int16_t pt[3] = {head.x, t16(head.y - 0x58), head.z};
    int16_t sxy[2];
    projectOnScreen(pt, sxy); // exe 0x5f2dd0
    int16_t depth = 0;
    const int16_t bp[3] = {head.x, head.y, head.z};
    const int w = projectedWidth(bp, 0x20, &depth); // exe 0x5f2f20
    const int h = (w * 3) >> 3;
    const int sh = stage().coordinateShift & 0x1f;
    const float scale = w != 0 ? float(0x20 << sh) / float(w) : 0.0f;
    int16_t cp[3];
    for (int k = 0; k < 3; ++k) cp[k] = t16(pt[k] << sh);
    // the offsets in pixels from the projected point S; C = S + (4, 0), Q starts at C + (0, -h)
    float qx = 4.0f, qy = float(-h);
    int ang = 0x200;
    for (int i = 0; i < 4; ++i) {
        const int sa = (int(rngStep() & 0x1f) + ang + 0x70) & 0xfff;
        ang += 0x100;
        const float sx = 4.0f + float((fixedSin(int(uint32_t(sa))) * w) >> 12);
        const float sy = float(-((fixedCos(int(uint32_t(sa))) * w) >> 12));
        float nx, ny;
        if (i == 3) {
            nx = 4.0f;
            ny = float(h);
        } else {
            nx = 4.0f + float((fixedSin(int(uint32_t(ang))) * w * 3) >> 14);
            ny = float(-((fixedCos(int(uint32_t(ang))) * w * 3) >> 14));
        }
        if (w != 0) {
            const uint32_t c[4] = {0x38ffffffu, 0x38ffffffu, 0x38ffffffu, 0x38ffffffu};
            int16_t v[4][3];
            for (auto& vv : v) std::copy(cp, cp + 3, vv);
            const float off[4][2] = {{qx * scale, qy * scale}, {sx * scale, sy * scale}, {4.0f * scale, 0.0f},
                                     {nx * scale, ny * scale}};
            dc::otQuadG4(cast.at, c, 0, v, off);
        }
        qx = nx;
        qy = ny;
    }
}
// exe 0x4b2f50: sparks at its bones on the ticks 0 / 10 / 20 / 30, then the cues: animations, the star, the dissolve
void guardianDissolves(AnimatedActor& cast) {
    castHead(cast);
    CastFields& fields = classState<CastFields>(cast);
    switch (CastState(cast.state)) {
    case CastState::Init: // exe 0x4b3010
        if (castInit(cast, 0x20, stateAddress(TableId::GuardianCastGuardianDissolvingBox))) {
            fields.ticks = 0;
            fields.sparkles = 0;
            fields.sparkBone = 0;
        }
        break;
    case CastState::Cue: { // exe 0x4b3070
        const int16_t t = fields.ticks;
        fields.ticks = int16_t(t + 1);
        switch (t) {
        case 0:
            fields.sparkDrift = 1;
            fields.sparkCount = 4;
            fields.sparkBone = 0xb;
            break;
        case 0xa:
            fields.sparkDrift = 0xff;
            fields.sparkCount = 4;
            fields.sparkBone = 0xe;
            break;
        case 0x14:
            fields.sparkDrift = 1;
            fields.sparkCount = 4;
            fields.sparkBone = 9;
            break;
        case 0x1e:
            fields.sparkDrift = 0xff;
            fields.sparkCount = 4;
            fields.sparkBone = 8;
            cast.enterStateKeepingStep(uint8_t(CastState::Act));
            break;
        default: break;
        }
        break;
    }
    case CastState::Act: // exe 0x4b3110: the script's cues
        switch (cast.subState) {
        case 1:
            cast.anim = 0x21;
            cast.animKey.frame = 0;
            cast.subState = 2;
            break;
        case 2:
            if (cast.animKey.flags != 0xff) break;
            headStar(cast);
            cast.subState = 0;
            break;
        case 3:
            cast.anim = 0x22;
            cast.animKey.frame = 0;
            cast.subState = 4;
            break;
        case 4:
            if (cast.animKey.flags == 0xff) cueAnim(cast, 0x23);
            break;
        case 5:
            fadeStart(cast, fields);
            cast.subState = 6;
            [[fallthrough]];
        case 6:
            fadeSparkles(cast, fields);
            if (dissolve(cast, fields, dissolveOrder, 0xf)) cast.subState = 0;
            break;
        default: break;
        }
        break;
    default: notPortedAt(kStoryPort, "0x4b2f50 state", cast.state);
    }
    castTail(cast);
    if ((cast.flags & kActorLive) && (cast.flags & kActorDrawn) && uint16_t(fields.sparkBone) != 0)
        boneSpark(cast, fields);
}
// exe 0x488350: hidden until its fade-in, then its animations on the cues and its dissolve
void fadingCast(AnimatedActor& cast) {
    castHead(cast);
    CastFields& fields = classState<CastFields>(cast);
    switch (CastState(cast.state)) {
    case CastState::Init: // exe 0x4883f0
        if (!bindActorModel(cast, 0)) {
            freeActor(cast);
            break;
        }
        cast.prevAnim = 0xff;
        cast.enterStateKeepingStep(uint8_t(CastState::Cue));
        cast.flags &= uint8_t(~kActorDrawn);
        cast.rotation.x = 0;
        cast.rotation.z = 0;
        cast.velocity.clearWords();
        cast.anim = 0;
        cast.animKey.frame = 0;
        break;
    case CastState::Cue: // exe 0x488440: the fade-in
        if (cast.subState == 0) {
            fadeStart(cast, fields);
            cast.subState = 1;
        } else if (cast.subState != 1) {
            break;
        }
        fadeSparkles(cast, fields);
        if (fadeIn(cast, fields)) cast.enterStateKeepingStep(uint8_t(CastState::Act));
        break;
    case CastState::Act: // exe 0x4884b0: the script's cues
        switch (cast.subState) {
        case 1:
            cast.anim = 1;
            cast.animKey.frame = 0;
            cast.subState = 2;
            break;
        case 2:
            if (cast.animKey.flags == 0xff) cueAnim(cast, 0);
            break;
        case 3: cueAnim(cast, 2); break;
        case 4:
            fadeStart(cast, fields);
            cast.subState = 5;
            [[fallthrough]];
        case 5:
            fadeSparkles(cast, fields);
            if (dissolve(cast, fields, fadingOrder, 0xd)) cast.subState = 0;
            break;
        default: break;
        }
        break;
    default: notPortedAt(kStoryPort, "0x488350 state", cast.state);
    }
    castTail(cast);
}

} // namespace

void guardianWakesUpdate(const PlayerEnv&, uint32_t actor) { guardianWakes(animatedActorAt(actor)); }
void guardianBeatenUpdate(const PlayerEnv&, uint32_t actor) { guardianBeaten(animatedActorAt(actor)); }
void guardianDropsInUpdate(const PlayerEnv& env, uint32_t actor) { guardianDropsIn(env, animatedActorAt(actor)); }
void guardianDissolvesUpdate(const PlayerEnv&, uint32_t actor) { guardianDissolves(animatedActorAt(actor)); }
void fadingCastUpdate(const PlayerEnv&, uint32_t actor) { fadingCast(animatedActorAt(actor)); }

} // namespace od::port
