// End-game cast of ST52 / ST5C, the bobbing prop (exe 0x50b750) and their effects. See content/late_game/end_cast.hpp.
//
// The classes (exe address -> routine): the cue casts of ST52 (variant tables [table + variant * 4](actor, 0xa725a0)):
// 0x44b0b0 (table 0x88e550) talkerACues; 0x4787d0 (0x890d88) talkerBCues; 0x47d8c0 (0x8912a0) mouthTalkerCues, its
// mouth 0x47dfe0 talkingMouth; 0x49b4c0 (0x8933b0) walkerCues; 0x4d5230 (0x895530) groupFirstCues / groupSecondCues
// / groupThirdCues; 0x4ef2a0 (0x896760) glowFirstCues / glowSecondCues, its glow ring 0x4efe10. The scene casts of
// ST5C (state tables): 0x49c270 (0x8937a8) finaleAScene2..7; 0x4f0050 (0x896850) finaleBScene2..7. The prop 0x50b750
// (0x8982e8 by its kind): 0x50b7d0 / 0x50b870 / 0x50b930. The ending effects 0x43d030 (0x88d970 by the variant):
// 0x43d050 / 0x43d970 / 0x43e260 / 0x43e730 / 0x43ec20 / 0x43ef80 / 0x43f260 / 0x43f980 / (8, 9:
// content/late_game/st4c_story 0x43fc90 / 0x43fe50) / 0x43ffa0 / 0x440670 / (12: content/late_game/st5c 0x440940,
// 13 / 14: content/late_game/st5c_fx2 0x440d40 / 0x4416a0); the stage dust effect 0x43ff80's variants 0 / 1 = 0x43ffa0
// / 0x440670 (= 0x43d030 variants 10 / 11: [0x88d998] = [0x88d970] + 10 * 4).
// The cue routines read the script block: the step tick and the scene (the step's sub). Their switch jump tables were
// decoded from the exe bytes (case value -> target) and transcribed as plain cases.
// GTE effects: projection-dependent logic (the ring counter of 0x43d050 only advances on screen) uses the camera
// read-only through cameraPoint (as game/global_fx / content/late_game/st40_fx); the OT polygons go through the
// presentation side channel (dc::otQuad* / dc::otLineG2) with camera-input corners. Deviation (render only): the exe's
// screen-space midpoints (0x6d0120 / 0x6d0124) are drawn at the 3D midpoints of the corners; NCLIP uses the truncated
// screen points. The GTE scratch the exe writes (0x6d00c0..) is written as the exe does. Names are hypotheses from the
// observed behaviour; shadows (exe 0x5f20f0 / 0x5f2220) are render output and left out.
#include "content/late_game/end_cast.hpp"
#include "content/late_game/end_cast_state.hpp"

#include <algorithm>

#include "content/late_game/st4c_story.hpp"
#include "content/late_game/st5c.hpp"
#include "content/late_game/st5c_fx2.hpp"

#include "game/actor_animation.hpp"
#include "game/actor_model.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/angles.hpp"
#include "game/cutscene_actor.hpp"
#include "game/distances.hpp"
#include "game/draw_commands.hpp"
#include "game/field_alias.hpp"
#include "game/game_state.hpp"
#include "game/geometry_scratch.hpp"
#include "game/glow_fan.hpp"
#include "game/keyframe_tracks.hpp"
#include "game/matrix.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/projection.hpp"
#include "game/projection_state.hpp"
#include "game/random.hpp"
#include "game/script_state.hpp"
#include "game/stage_state.hpp"
#include "game/story.hpp"
#include "game/vectors.hpp"

namespace od::port {

using namespace end_cast_state;

namespace {

int16_t t16(int64_t v) { return int16_t(uint16_t(uint64_t(v))); }
int32_t mul(int32_t a, int32_t b) { return int32_t(uint32_t(a) * uint32_t(b)); } // imul (32-bit wrap)
uint32_t sh() { return stage().coordinateShift & 0x1f; }                         // the stage shift
int16_t shl(int v) { return t16(int32_t(uint32_t(int32_t(t16(v))) << sh())); }    // (int2)v << shift, as int2
uint32_t tick() { return uint32_t(scriptState().stepTick); }
uint8_t scene() { return scriptState().stepSub; }

// the ending effects (pool 0xa0 stage type 0x21, exe 0x43d030) by variant
enum class EndingEffect : uint8_t {
    RingBand = 0, GlowDisc = 1, Swirl = 2, BoneSparkles = 3, Trail = 4, Puff = 5, GlowFan = 6, PulseGlow = 7,
    AnimatedSprite = 8, StillSprite = 9, LightPillar = 10, RisingStreak = 11, DriftingPuffs = 12, WarningRing = 13,
    LandingDust = 14,
};
constexpr uint8_t kEndingEffectType = 0x21;
// an ending / dust effect slot (pool 0xa0, the stage's class table: exe 0x5e13a0); 0 when the pool is full
uint32_t allocEndingEffect() { return allocActorA0(kActorStageClass); }

// ---- the pool-0x20 cast ---------------------------------------------------------------------------------------------
constexpr uint8_t kMouthTalk = 1, kMouthClose = 2; // the mouth talker's step
constexpr uint8_t kSparkleAlways = 0xff;           // the third group member's sparkle mask: every frame

// a cue routine's actor and its class state (the slot's: the cast's block and the fade timer), with the cue helpers: the animation asked (its key 0), the eyes' / mouth's
// sequence (exe 0x5e33d0 / 0x5e33f0(tracks, n, 0)), their texture cells (exe 0x5e3630 / 0x5e3660), the face tables
struct Cue {
    explicit Cue(const AnimatedActor& actor) : Cue(const_cast<AnimatedActor&>(actor)) {}
    explicit Cue(AnimatedActor& actor) : a(actor), extra(classState<EndCastExtra>(actor)), cast(extra) {}
    AnimatedActor& a;
    EndCastExtra& extra;
    CastFields& cast;

    void anim(uint8_t n) { requestAnim(a, cast.animKey, n); }
    void eyes(uint8_t n) { cast.faceTracks.first.select(n, 0); }
    void mouth(uint8_t n) { cast.faceTracks.second.select(n, 0); }
    void eyesCell(uint32_t n) { textureTrackStop(a, cast.faceTracks, FaceTrack::Eyes, n); }
    void mouthCell(uint32_t n) { textureTrackStop(a, cast.faceTracks, FaceTrack::Mouth, n); }
    void faces(uint32_t first, uint32_t second) { cast.faceTracks.init(first, second); } // exe 0x5e33a0
};
// the common state-0 bind of the cue casts (exe 0x5e0ba0 = model variant 0; none: exe 0x5e1490 freed): the box, the
// class block cleared, the state (1, or the sub-state 1 with the step bytes cleared for the scene classes), the
// animation `anim` (none shown), the velocities / accelerations cleared, the animation key 0
bool castStart(Cue& c, uint32_t box, uint8_t anim, bool sceneClass) {
    AnimatedActor& a = c.a;
    if (!bindActorModel(a, 0)) { // exe 0x5e0ba0
        freeActor(a);            // exe 0x5e1490
        return false;
    }
    a.collisionBox.pointAt(box);
    a.clearClassBlock(); // exe 0x40dc40
    if (sceneClass) {
        a.subState = 1;
        a.clearStepBytes();
    } else {
        a.enterState(1);
    }
    a.anim = anim;
    a.prevAnim = kNoAnimShown;
    clearVelocity(a);
    c.cast.animKey = 0;
    return true;
}
// the parts of the face models (the hidden bit of their part entries)
void hidePart(AnimatedActor& a, size_t p) { a.parts[p].flags |= kPartHidden; }
void showPart(AnimatedActor& a, size_t p) { a.parts[p].flags &= uint8_t(~kPartHidden); }

// the class tails (after the variant, while live): the planar velocities += their accelerations, exe 0x5e3690 (x, 0,
// z) when either moves; on the ground (its flag): its height = exe 0x40f190 (the ground); the texture tracks (exe
// 0x5e34f0) or the talking mouth; the animation (the hold bits `holdMask`); the shadow is render output
void talkingMouth(AnimatedActor& a);
void castTail(const PlayerEnv& env, Cue& c, uint8_t groundBit, bool mouth, uint8_t holdMask) {
    AnimatedActor& a = c.a;
    if (!(a.flags & kActorLive)) return;
    a.velocity.x = int16_t(a.velocity.x + a.acceleration.x);
    a.velocity.z = int16_t(a.velocity.z + a.acceleration.z);
    if (a.velocity.x != 0 || a.velocity.z != 0) integrateVelocity(a, a.velocity.x, 0, a.velocity.z); // 0x5e3690
    if (groundBit != 0 && (c.cast.castFlags & groundBit)) a.pos.y.whole = int16_t(actorGround(env, a)); // 0x40f190
    if (mouth) talkingMouth(a);
    else textureTracksTick(a, c.cast.faceTracks); // exe 0x5e34f0
    castAnimate(a, c.cast.animKey, c.cast.castFlags, holdMask);
}

// ---- 0x44b0b0 (type 2) variant 0 0x44b200: box 0x88e558, tracks 0x88e594 / 0x88e614, cells 0 / 0 ---------------------
void talkerACues(Cue& c) {
    AnimatedActor& a = c.a;
    if (a.state == 0) {
        if (!castStart(c, stateAddress(TableId::EndCastTalkerABox), 0, false)) return;
        c.faces(stateAddress(TableId::EndCastTalkerAFirstTracks), stateAddress(TableId::EndCastTalkerASecondTracks)); // exe 0x5e33a0
        c.eyesCell(0);
        c.mouthCell(0);
    }
    switch (tick()) {
    case 0x1f: c.anim(1); c.mouth(0); return;
    case 0x3d: c.eyes(0); return;
    case 0x74: c.anim(2); return;
    case 0x8b7: c.anim(3); return;
    case 0x8d3: c.anim(4); return;
    case 0x8f3: c.eyesCell(4); c.mouth(1); return;
    case 0x92f: case 0x957: case 0x9b1: case 0xa29: case 0xa51: case 0xaab: case 0xb4b: c.mouth(2); return;
    case 0x94d: case 0x993: case 0x9ed: case 0xa35: case 0xa97: case 0xadb: case 0xb7b: case 0xbe1: case 0xc15:
    case 0xc31: case 0xc63:
        c.mouthCell(3);
        return;
    case 0xb0f: c.eyes(2); c.mouth(3); return;
    case 0xba5: c.eyes(3); c.mouth(2); return;
    case 0xc09: c.anim(5); c.eyes(0); c.mouth(4); return;
    case 0xc1d: case 0xc45: c.mouth(4); return;
    default: return;
    }
}

// ---- 0x4787d0 (type 0x16) variant 0 0x478920: box 0x890d90, tracks 0x890db4 / 0x890ddc, cells 8 / 8 ------------------
void talkerBCues(Cue& c) {
    AnimatedActor& a = c.a;
    if (a.state == 0) {
        if (!castStart(c, stateAddress(TableId::EndCastTalkerBBox), 0, false)) return;
        c.faces(stateAddress(TableId::EndCastTalkerBFirstTracks), stateAddress(TableId::EndCastTalkerBSecondTracks));
        c.eyesCell(8);
        c.mouthCell(8);
    }
    switch (tick()) {
    case 0x71: c.anim(1); return;
    case 0x4b1: case 0x5dd: case 0x6e1: c.eyes(0); return;
    case 0x687: c.anim(2); c.mouth(0); return;
    case 0x6eb: case 0x727: c.mouthCell(8); return;
    case 0x6ff: c.mouth(0); return;
    case 0x745: c.anim(3); c.eyesCell(0xc); return;
    case 0x7bd: case 0x8ad: c.eyes(1); return;
    default: return;
    }
}

// ---- 0x47d8c0 (type 0x1a) variant 0 0x47da20: box 0x8912a8, no tracks; its step = the talking mouth (1 talk, 2 close) ---
void mouthTalkerCues(Cue& c) {
    AnimatedActor& a = c.a;
    if (a.state == 0 && !castStart(c, stateAddress(TableId::EndCastMouthTalkerBox), 0, false)) return;
    switch (tick()) {
    case 0x6f: c.anim(1); return;
    case 0x41b: c.anim(2); return;
    case 0x745: c.anim(3); return;
    case 0x425: case 0x45f: case 0x4b1: case 0x533: case 0x597: case 0x5d3: case 0x60f: case 0x759: case 0x80d:
    case 0x82b: case 0x867: case 0x8b7:
        a.step = kMouthTalk;
        return;
    case 0x441: case 0x49d: case 0x515: case 0x57b: case 0x5bf: case 0x5fb: case 0x669: case 0x7ef: case 0x817:
    case 0x853: case 0x8a3: case 0x8cb:
        a.step = kMouthClose;
        return;
    default: return;
    }
}
// 0x47dfe0: the talking mouth -- +0xa == 1: the mouth parts (+0xf3 / +0x117 / +0x11b bit 7 = hidden) flip every 4 ticks of
// the counter +0xb; +0xa == 2: closed (+0xf3 shown), +0xa = +0xb = 0
void talkingMouth(AnimatedActor& a) {
    if (a.step == kMouthTalk) {
        const uint8_t c = a.stepCounter;
        if (!(c & 4)) {
            showPart(a, 1);
            hidePart(a, 10);
            hidePart(a, 11);
        } else {
            hidePart(a, 1);
            hidePart(a, 10);
            showPart(a, 11);
        }
        a.stepCounter = uint8_t(c + 1u);
        return;
    }
    if (a.step != kMouthClose) return;
    showPart(a, 1);
    hidePart(a, 10);
    hidePart(a, 11);
    a.clearStepBytes();
}

// ---- 0x49b4c0 (type 0x2f) variant 0 0x49b5d0: box 0x8933b8, tracks 0x8933dc / 0x893418, cells 0 / 0 ------------------
void walkerCues(Cue& c) {
    AnimatedActor& a = c.a;
    if (a.state == 0) {
        if (!castStart(c, stateAddress(TableId::EndCastWalkerBox), 0, false)) return;
        c.faces(stateAddress(TableId::EndCastWalkerFirstTracks), stateAddress(TableId::EndCastWalkerSecondTracks));
        c.eyesCell(0);
        c.mouthCell(0);
    }
    switch (tick()) {
    case 0x6f: case 0xab: case 0x105: case 0x19b: case 0x1af: case 0x227: case 0x24f: case 0x2c7: case 0x30d:
    case 0x371:
        c.mouth(0);
        return;
    case 0x8d: case 0xf1: case 0x169: case 0x1a3: case 0x20c: case 0x245: case 0x295: case 0x2f9: case 0x349:
    case 0x399:
        c.mouthCell(0);
        return;
    case 0x97: case 0x191: case 0x213: case 0x35d: case 0x3b7: c.eyes(0); return;
    case 0x3d5: c.anim(1); c.eyes(1); c.mouth(1); return;
    case 0x3f6: // turns half around and walks
        a.rotation.y = int16_t(uint16_t(a.rotation.y) + 0x555u);
        c.anim(2);
        a.velocity.z = int16_t(0xffd2);
        return;
    default: return;
    }
}

// ---- 0x4d5230 (type 0x4c): box 0x895540, tracks 0x8955b0 / 0x895694, +0x15c = 6 (ground + shadow) --------------------
void groupFaces(Cue& c, uint32_t c0, uint32_t c1) {
    c.cast.castFlags |= uint8_t(kCastShadow | kCastOnGround);
    c.faces(stateAddress(TableId::EndCastGroupFirstTracks), stateAddress(TableId::EndCastGroupSecondTracks));
    c.eyesCell(c0);
    c.mouthCell(c1);
}
void groupFirstCues(Cue& c) { // variant 0
    AnimatedActor& a = c.a;
    if (a.state == 0) {
        if (!castStart(c, stateAddress(TableId::EndCastGroupBox), 0, false)) return;
        groupFaces(c, 0, 0);
        showPart(a, 4);
        hidePart(a, 11);
    }
    switch (tick()) {
    case 0x3d: c.anim(1); a.rotation.y = int16_t(0x600); return;
    case 0x52: case 0x97: c.anim(2); c.mouth(0); return;
    case 0x85: case 0xab: case 0xd3: case 0x117: case 0x7ef: case 0x82b: case 0x856: case 0x88f: case 0x8c1:
        c.mouthCell(0);
        return;
    case 0xbf: c.anim(3); c.eyes(1); return;
    case 0xc4: case 0xe7: case 0x80d: case 0x83f: case 0x871: case 0x8a3: c.mouth(0); return;
    case 0x14b: c.anim(4); c.eyes(2); c.mouth(1); return;
    case 0x187: case 0x1b9: case 0x2b2: case 0x2e5: case 0x35d: case 0x3df: case 0x409: case 0x439: case 0x49d:
    case 0x4e3: case 0x533: case 0x565: case 0x5a1: case 0x5e5: case 0x623: case 0x669: case 0x6a5: case 0x6ff:
    case 0x757: case 0x79f:
        c.mouthCell(3);
        return;
    case 0x1a5: case 0x2a9: case 0x2d1: case 0x317: case 0x380: case 0x425: case 0x44d: case 0x4bb: case 0x50b:
    case 0x551: case 0x583: case 0x5b5: case 0x605: case 0x641: case 0x687: case 0x6b9: case 0x727: case 0x777:
        c.mouth(1);
        return;
    case 0x1f5: a.rotation.y = int16_t(0x800); return;
    case 0x29f: // decode-only animation (+0x15c bit 3)
        c.anim(5);
        c.cast.castFlags |= kCastHoldPoseB;
        c.eyesCell(6);
        c.mouthCell(3);
        return;
    case 0x2f9: case 0x5f1: case 0x713: case 0x7b3: c.eyes(3); return;
    case 0x3fb: c.eyes(3); c.mouth(1); return;
    case 0x7d1:
        c.cast.castFlags &= uint8_t(~kCastHoldPoseB);
        c.eyes(0);
        c.mouth(0);
        return;
    default: return;
    }
}
void groupSecondCues(Cue& c) { // variant 1
    AnimatedActor& a = c.a;
    if (a.state == 0) {
        if (!castStart(c, stateAddress(TableId::EndCastGroupBox), 6, false)) return;
        groupFaces(c, 6, 7);
        a.tint = 0x318c;
        showPart(a, 4);
        hidePart(a, 11);
    }
    switch (tick()) {
    case 0x281: case 0x2c7: case 0x317: case 0x3b7: case 0x3e9: case 0x439: case 0x4cf: case 0x551: case 0x781:
    case 0x7f9: case 0x867: case 0x8ad: case 0x939:
        c.mouth(2);
        return;
    case 0x2a9: case 0x2f7: case 0x367: case 0x3c6: case 0x419: case 0x469: case 0x4ed: case 0x533: case 0x575:
    case 0x5f1: case 0x62d: case 0x69b: case 0x6f3: case 0x759: case 0x79f: case 0x7e1: case 0x821: case 0x885:
    case 0x8f0: case 0x945:
        c.mouthCell(7);
        return;
    case 0x2b3: case 0x385: case 0x49d: c.eyes(3); return;
    case 0x4bb: c.anim(7); c.eyesCell(1); return;
    case 0x4be: c.eyesCell(7); return;
    case 0x4c5: case 0x752: c.eyesCell(8); return;
    case 0x529: c.eyes(4); c.mouth(2); return;
    case 0x5c9: c.anim(8); c.eyes(5); c.mouth(3); return;
    case 0x605: case 0x687: case 0x6c3: c.mouth(3); return;
    case 0x713: c.anim(9); c.eyes(6); return;
    case 0x74f:
        c.anim(0xa);
        c.eyesCell(0xa);
        c.mouth(2);
        showPart(a, 11);
        hidePart(a, 4);
        return;
    case 0x7c7: c.anim(0xb); c.mouth(2); return;
    case 0x7ef:
        c.anim(0xc);
        showPart(a, 4);
        hidePart(a, 11);
        return;
    default: return;
    }
}
// 0x4d64a0 (variant 2): cues set +9 (a sparkle mask), +0xb = 1 starts the fade-out; tail: while +9 != 0 and (+9 ==
// 0xff or no bit of +9 in the frame byte 0x6d0006) two pool-0xa0 type-0x21 variant-3 sparkles at random bones (+0xc =
// this slot +2, +0xd = rng & 0xf); +0xb 1 -> 2 with +0x15e = 6, then +0xe6 = grey(+0x15e) counting down, at 0 freed
void groupThirdCues(Cue& c) {
    AnimatedActor& a = c.a;
    if (a.state == 0) {
        if (!castStart(c, stateAddress(TableId::EndCastGroupBox), 0xc, false)) return;
        c.faces(stateAddress(TableId::EndCastGroupFirstTracks), stateAddress(TableId::EndCastGroupSecondTracks));
        c.eyesCell(0xc);
        c.mouthCell(0xb);
        showPart(a, 4);
        hidePart(a, 11);
    }
    switch (tick()) {
    case 0xc9: a.subState = 0xf; c.anim(0xd); break;
    case 0xd3: c.eyesCell(0xb); break;
    case 0xf1: a.subState = 7; c.eyesCell(0xc); c.mouth(4); break;
    case 0xfb: case 0x1a5: case 0x245: case 0x28b: case 0x2b8: case 0x335: case 0x399: case 0x3f3: case 0x439:
        c.mouthCell(0xb);
        break;
    case 0x10f: case 0x14b: c.mouth(4); break;
    case 0x12d: a.subState = 3; c.eyes(7); c.mouthCell(0xb); break;
    case 0x231: a.subState = 1; c.anim(0xe); c.mouth(5); break;
    case 0x277: c.eyes(8); c.mouth(5); break;
    case 0x295: case 0x317: case 0x37b: case 0x3cb: case 0x411: c.mouth(5); break;
    case 0x353: c.eyes(8); break;
    case 0x461: // exe 0x5e1a00(a, 1, 1): +0xeb = 0x22
        a.subState = kSparkleAlways;
        a.semiTransparency = semiTransparencyBits(1, 1);
        a.tint = 0x18c6;
        break;
    case 0x4f1: a.stepCounter = 1; break;
    default: break;
    }
    const uint8_t m = a.subState;
    if (m != 0 && (m == kSparkleAlways || !(uint8_t(gameState().frame) & m))) {
        for (int k = 0; k < 2; ++k) {
            const uint32_t slot = allocEndingEffect(); // exe 0x5e13a0
            if (!slot) continue;
            EffectActor& e = effectActorAt(slot);
            EndingEffectParams params(effectActorAt(slot));
            const uint8_t loop = e.loopFlags;
            e.flags = kActorLiveDrawn;
            e.type = kEndingEffectType;
            e.variant = uint8_t(EndingEffect::BoneSparkles);
            e.loopFlags = uint8_t(loop | kActorSweepGroup2);
            params.slot = int8_t(a.slot);
            const uint32_t r = rngStep();
            params.arg1 = 0;
            params.arg0 = int8_t(r & 0xf);
            params.arg2 = 0;
        }
    }
    if (a.stepCounter == 1) {
        a.stepCounter = 2;
        c.extra.fadeTimer = 6;
    } else if (a.stepCounter != 2) {
        return;
    }
    const int v = c.extra.fadeTimer;
    if (v >= 1) {
        a.tint = uint16_t(((v << 5 | v) << 5) | v);
        c.extra.fadeTimer = int16_t(v - 1);
        return;
    }
    freeActor(a); // exe 0x5e1490
}

// ---- 0x4ef2a0 (type 0x68): box 0x896768, tracks 0x8967b4 / 0x896828 ------------------------------------------------
// 0x4efe10: the glow ring -- a pool-0xa0 type-0x21 variant-1 effect following this actor (+0xc = slot +2, +0xd
// = 0x10 radius, +0xe = 8 height, +0xf = 0)
void glowRing(const AnimatedActor& a) {
    const uint32_t slot = allocEndingEffect(); // exe 0x5e13a0
    if (!slot) return;
    EffectActor& e = effectActorAt(slot);
    EndingEffectParams params(effectActorAt(slot));
    e.flags = kActorLiveDrawn;
    e.type = kEndingEffectType;
    e.loopFlags |= kActorSweepGroup2 | kActorLateLoop;
    e.variant = uint8_t(EndingEffect::GlowDisc);
    params.arg0 = 0x10;
    params.slot = int8_t(a.slot);
    params.arg1 = 8;
    params.arg2 = 0;
}
void glowFaces(Cue& c, uint32_t c0) {
    AnimatedActor& a = c.a;
    c.faces(stateAddress(TableId::EndCastGlowFirstTracks), stateAddress(TableId::EndCastGlowSecondTracks));
    c.eyesCell(c0);
    c.mouthCell(0);
    glowRing(a);
    showPart(a, 20);
    hidePart(a, 6);
    showPart(a, 9);
    hidePart(a, 19);
}
void glowFaceSwap(AnimatedActor& a) { // parts 6 / 19 shown, 20 / 9 hidden
    showPart(a, 6);
    hidePart(a, 20);
    showPart(a, 19);
    hidePart(a, 9);
}
void glowFirstCues(Cue& c) { // variant 0
    AnimatedActor& a = c.a;
    if (a.state == 0) {
        if (!castStart(c, stateAddress(TableId::EndCastGlowBox), 0, false)) return;
        glowFaces(c, 0);
    }
    switch (tick()) {
    case 0xa: case 0x5a: c.eyes(0); return;
    case 0x29: case 0x65: case 0x14b: case 0x32b: c.mouth(0); return;
    case 0x51: case 0x8d: case 0xd3: case 0x17b: case 0x1e1: case 0x303: c.mouthCell(0); return;
    case 0xb5: c.anim(1); c.eyes(1); c.mouth(0); return;
    case 0xdd: c.eyesCell(3); c.mouthCell(0); return;
    case 0x191: c.eyes(1); c.mouth(0); return;
    case 0x23b: // walks
        c.anim(2);
        a.velocity.z = int16_t(0xffd0);
        c.mouth(0);
        glowFaceSwap(a);
        return;
    case 0x28b: c.anim(3); a.velocity.z = 0; return;
    case 0x37b: c.anim(4); c.eyes(4); c.mouthCell(0); return;
    case 0x385: c.mouth(1); return;
    case 0x3b6: // turns around and walks away
        c.anim(5);
        a.rotation.y = int16_t(uint16_t(a.rotation.y) + 0x800u);
        a.velocity.z = int16_t(0xffd0);
        c.mouthCell(3);
        return;
    case 0x45a: a.velocity.z = 0; return;
    default: return;
    }
}
void glowSecondCues(Cue& c) { // variant 1 (anims 7 / 8 / 9 keep the entry +0x15d)
    AnimatedActor& a = c.a;
    if (a.state == 0) {
        if (!castStart(c, stateAddress(TableId::EndCastGlowBox), 6, false)) return;
        glowFaces(c, 7);
    }
    switch (tick()) {
    case 0x79f:
        a.anim = 7;
        c.mouth(2);
        glowFaceSwap(a);
        return;
    case 0x7db: case 0x867: case 0x8ad: case 0x8df: case 0x92f: case 0x993: case 0xa01: case 0xa65: case 0xabf:
    case 0xafb: case 0xc31: case 0xd3f: case 0xe1b: case 0xec5:
        c.mouthCell(0);
        return;
    case 0x835: c.eyes(2); c.mouth(2); return;
    case 0x87b: case 0x8d5: case 0x8f3: case 0x961: case 0x9a7: case 0xa1f: case 0xa83: case 0xad3: case 0xc4f:
    case 0xd5d: case 0xe39:
        c.mouth(2);
        return;
    case 0xa0b: case 0xac9: c.eyes(2); return;
    case 0xb23:
        a.anim = 8;
        c.cast.castFlags |= kCastHoldPoseB;
        c.eyes(3);
        return;
    case 0xb2d: c.cast.castFlags &= uint8_t(~kCastHoldPoseB); return;
    case 0xb91: a.anim = 9; c.eyes(2); c.mouth(2); return;
    default: return;
    }
}

// ---- 0x49c270 (ST5C type 0x2f): states [0x8937a8 + +8 * 4], box 0x8937c0, tracks 0x893814 / 0x8938ec ----------------
// Each state runs while the scene byte S+3 is (state + 2), else the next state (+8 = the dword state + 1).
void finaleAScene2(Cue& c) {
    AnimatedActor& a = c.a;
    if (a.subState == 0) {
        if (!castStart(c, stateAddress(TableId::EndCastFinaleABox), 0, true)) return;
        c.faces(stateAddress(TableId::EndCastFinaleAFirstTracks), stateAddress(TableId::EndCastFinaleASecondTracks));
        c.eyesCell(0);
        c.mouthCell(3);
        c.extra.fadeTimer = 0;
        if (const uint32_t slot = allocEndingEffect()) { // exe 0x5e13a0: the light pillar (type 0x24 variant 0)
            EffectActor& e = effectActorAt(slot);
            e.flags = kActorLiveDrawn;
            e.type = kStageDustEffectType;
            e.variant = 0;
            e.loopFlags |= kActorSweepGroup2 | kActorLateLoop;
            e.pos.x = a.pos.x.whole;
            e.pos.y = a.pos.y.whole;
            e.pos.z = a.pos.z.whole;
            e.setRecordYaw(0);
        }
    }
    if (scene() != 2) {
        a.enterState(1);
        return;
    }
    switch (tick()) {
    case 0x51: c.eyes(0); c.mouth(0); return;
    case 0x79: c.anim(1); a.velocity.z = int16_t(0xffbf); return;
    case 0x86: c.anim(2); return;
    case 0x8d: c.mouthCell(0); return;
    case 0xab: c.eyesCell(3); c.mouthCell(3); return;
    case 0xbf: case 0xdd: case 0x14b: case 0x1b9: c.mouth(1); return;
    case 0xc9: case 0x123: case 0x187: case 0x1e1: c.mouthCell(3); return;
    case 0xd2: c.anim(3); return;
    case 0xd8: a.velocity.z = 0; return;
    case 0x19b: c.eyes(1); return;
    case 0x1f5: c.eyesCell(6); c.mouth(2); return;
    case 0x223: c.mouthCell(7); return;
    default: return;
    }
}
void finaleAScene3(Cue& c) {
    AnimatedActor& a = c.a;
    if (scene() != 3) {
        a.enterState(2);
        return;
    }
    if (tick() != 1) return;
    c.eyesCell(10);
    c.mouthCell(7);
}
void finaleAScene4(Cue& c) {
    AnimatedActor& a = c.a;
    if (scene() != 4) {
        a.enterState(3);
        return;
    }
    switch (tick()) {
    case 0x1: c.anim(4); c.mouthCell(0xb); return;
    case 0x15: c.mouth(3); return;
    case 0x33: case 0x79: case 0xb5: c.mouthCell(7); return;
    case 0x65: c.anim(5); c.eyesCell(0xa); c.mouth(2); return;
    case 0x83: c.mouthCell(9); return;
    case 0x97: c.mouth(2); return;
    case 0xc9: c.eyesCell(3); c.mouth(1); return;
    case 0x105: case 0x173: case 0x1f0: case 0x233: c.mouthCell(3); return;
    case 0x10f: case 0x1ff: c.mouth(1); return;
    case 0x187: c.eyes(1); return;
    case 0x19b: c.eyesCell(7); c.mouth(1); return;
    case 0x2b3: c.anim(6); c.eyesCell(0xd); c.mouthCell(7); return;
    case 0x2d1: c.eyes(4); return;
    case 0x3df: c.anim(7); c.eyesCell(0xd); c.mouthCell(7); return;
    default: return;
    }
}
void finaleAScene5(Cue& c) {
    AnimatedActor& a = c.a;
    if (scene() != 5) {
        a.enterState(4);
        return;
    }
    switch (tick()) {
    case 0x1: c.anim(8); c.mouth(5); return;
    case 0x10: case 0x77: case 0xdd: case 0x13d: case 0x1c5: case 0x227: case 0x25f: c.mouthCell(6); return;
    case 0x1f: case 0xa1: case 0xf1: case 0x1eb: case 0x231: c.mouth(5); return;
    case 0x8d: case 0x14b: case 0x1d7: case 0x2a9: c.eyes(4); return;
    case 0x169: c.anim(9); c.mouth(5); return;
    case 0x277: c.eyesCell(0xd); c.mouthCell(0); return;
    case 0x28b: c.anim(0xa); c.mouth(4); return;
    case 0x29f: case 0x303: case 0x33f: c.mouthCell(0); return;
    case 0x2bd: case 0x317: c.mouth(4); return;
    default: return;
    }
}
void finaleAScene6(Cue& c) {
    AnimatedActor& a = c.a;
    if (scene() != 6) {
        a.enterState(5);
        return;
    }
    switch (tick()) {
    case 0x51: c.anim(0xb); c.mouth(4); return;
    case 0xc9: c.mouthCell(0); return;
    case 0x1a5: c.anim(0xc); c.eyesCell(9); c.mouth(2); return;
    case 0x1d7: c.mouthCell(9); return;
    default: return;
    }
}
void finaleAScene7(Cue& c) {
    if (scene() != 7) return;
    switch (tick()) {
    case 0x1: c.anim(0xd); c.eyesCell(8); c.mouth(1); return;
    case 0x3d: case 0xc7: c.mouthCell(3); return;
    case 0x51: c.mouth(1); return;
    default: return;
    }
}

// ---- 0x4f0050 (ST5C type 0x68): states [0x896850 + +8 * 4], box 0x896868, tracks 0x896898 / 0x8968e4 ----------------
void finaleBScene2(Cue& c) {
    AnimatedActor& a = c.a;
    if (a.subState == 0) {
        if (!castStart(c, stateAddress(TableId::EndCastFinaleBBox), 0, true)) return;
        c.faces(stateAddress(TableId::EndCastFinaleBFirstTracks), stateAddress(TableId::EndCastFinaleBSecondTracks));
        c.eyesCell(0);
        c.mouthCell(0);
        showPart(a, 20);
        hidePart(a, 6);
        showPart(a, 9);
        hidePart(a, 19);
        c.extra.fadeTimer = 0;
    }
    if (scene() != 2) {
        a.enterState(1);
        return;
    }
    switch (tick()) {
    case 0x79: c.anim(1); a.velocity.z = int16_t(0xffbf); return;
    case 0x86: c.anim(2); return;
    case 0xd2: c.anim(3); return;
    case 0xd8: a.velocity.z = 0; return;
    default: return;
    }
}
void finaleBScene3(Cue& c) {
    AnimatedActor& a = c.a;
    if (scene() != 3) a.enterState(2);
}
void finaleBScene4(Cue& c) {
    AnimatedActor& a = c.a;
    if (scene() != 4) {
        a.enterState(3);
        return;
    }
    switch (tick()) {
    case 0x2a9: c.mouth(0); return;
    case 0x2dd: c.mouthCell(4); return;
    case 0x321: c.mouth(1); return;
    case 0x35d: c.mouthCell(1); return;
    case 0x38a: c.anim(4); return;
    case 0x393: c.eyes(0); return;
    case 0x3df: c.anim(5); return;
    default: return;
    }
}
void finaleBScene5(Cue& c) {
    AnimatedActor& a = c.a;
    if (scene() != 5) {
        a.enterState(4);
        return;
    }
    switch (tick()) {
    case 0x1: c.anim(6); c.eyesCell(4); c.mouthCell(7); return;
    case 0x97: c.eyes(1); return;
    case 0x169: c.anim(7); c.eyes(1); return;
    case 0x277: c.anim(8); c.eyesCell(7); c.mouthCell(1); return;
    default: return;
    }
}
void finaleBScene6(Cue& c) {
    AnimatedActor& a = c.a;
    if (scene() != 6) {
        a.enterState(5);
        return;
    }
    switch (tick()) {
    case 0x1: c.anim(9); c.mouth(1); return;
    case 0x35: case 0xe3: case 0x121: case 0x15b: c.mouthCell(1); return;
    case 0x5b: case 0x12d: case 0x1af: c.eyes(2); return;
    case 0xd3: c.anim(0xa); c.mouth(1); return;
    case 0xf1: case 0x137: c.mouth(1); return;
    default: return;
    }
}
void finaleBScene7(Cue& c) {
    if (scene() != 7) return;
    switch (tick()) {
    case 0x1: c.anim(0xb); c.eyesCell(1); c.mouthCell(1); return;
    case 0x1f: case 0xb5: c.eyes(0); return;
    case 0xe7: c.mouth(1); return;
    case 0x137: c.mouthCell(1); return;
    default: return;
    }
}


// ---- the bobbing prop (types 0x71 / 0x75, exe 0x50b750): kinds [0x8982e8 + kind * 4], box 0x8982f8 -------------------
// exe 0x5e0bc0(actor, actor + 6) = the model of its variant; its tick counter and base height (16.16): the bob y = base +
// sin(tick * 0x10) * 0x400
enum class PropKind : int8_t { Bobbing = 0, BobbingUntilStopped = 1, Still = 2 };
bool bindProp(AnimatedActor& a) {
    if (!bindActorModel(a, a.variant)) { // exe 0x5e0bc0
        freeActor(a);                    // exe 0x5e1490
        return false;
    }
    a.collisionBox.pointAt(TableId::EndCastPropBox);
    a.clearClassBlock();
    return true;
}
void bob(AnimatedActor& a, BobbingPropFields& prop) {
    a.pos.y = int32_t(uint32_t(mul(fixedSin(int((prop.ticks & 0xff) << 4)), 0x400)) + uint32_t(prop.baseY));
    prop.ticks += 1u;
}
// exe 0x50b7d0
void bobbing(AnimatedActor& a, BobbingPropFields& prop) {
    if (a.state == 0) {
        if (!bindProp(a)) return;
        a.anim = 0;
        a.enterState(1);
        a.prevAnim = 0xff;
        a.airborne = kAirborneFalling;
        prop.ticks = 0;
        prop.baseY = a.pos.y;
        return;
    }
    if (a.state != 1) return;
    bob(a, prop);
}
// exe 0x50b870
void bobbingUntilStopped(AnimatedActor& a, BobbingPropFields& prop) {
    if (a.state == 0) {
        if (!bindProp(a)) return;
        a.enterState(1);
        a.anim = 0;
        a.prevAnim = 0xff;
        a.airborne = kAirborneFalling;
        prop.baseY = a.pos.y;
        return;
    }
    if (a.state != 1) return;
    if (prop.stop != 0) { // stopped from outside: back to the base, state 2
        a.enterState(2);
        a.pos.y = prop.baseY;
        prop.ticks = 0;
        return;
    }
    bob(a, prop);
}
// exe 0x50b930
void stillProp(AnimatedActor& a) {
    if (a.state != 0) return;
    if (!bindProp(a)) return;
    a.enterState(1);
    a.anim = 0;
    a.prevAnim = 0xff;
    a.scale.z = 0x80;
    a.scale.y = 0x80;
    a.scale.x = 0x80;
    a.rotation.x = 0xc00;
    a.rotation.z = 0;
    a.airborne = kAirborneFalling;
}

// ---- the ending effects: the scratch and the GTE helpers ------------------------------------------------------------
// The projection scratch's bytes the effects write as the exe does: the colours (the GTE colour registers, eight
// words), the texture words of the FT4 / GT4 packets (the CLUT / tpage word, two texel words), the OT depth, the
// screen mean, a point and its spare lane, the corners and the angles of the glow fan, its rotated point
struct EffectScratch {
    std::array<Lane32, 8> colours = geometryScratch().words<8>(0x78);
    std::array<Lane8, 0x20> colourBytes = geometryScratch().bytes<0x20>(0x78);
    Lane16 clut = geometryScratch().lane(0xb8);
    Lane16 tpage = geometryScratch().lane(0xba);
    Lane32 textureWord = geometryScratch().word(0xb8);
    std::array<Lane32, 2> texelWords = geometryScratch().words<2>(0xa0);
    std::array<Lane16, 4> texelHalves = geometryScratch().lanes<4>(0xa0);
    Lane32 depth = geometryScratch().word(0xb4);
    Lane16 meanY = geometryScratch().lane(0x116);
    LaneVec3 point = geometryScratch().vec3(0x0);
    Lane16 pointSpare = geometryScratch().lane(0x6);
    Lane32 pointXY = geometryScratch().word(0x0);
    Lane32 pointZ = geometryScratch().word(0x4);
    LaneVec3 drift = geometryScratch().vec3(0x8);
    LaneVec3 cornerA = geometryScratch().vec3(0x8);
    LaneVec3 cornerB = geometryScratch().vec3(0x10);
    LaneVec3 angles = geometryScratch().vec3(0x18);
    std::array<Lane32, 3> rotated = geometryScratch().words<3>(0x20);
};
EffectScratch& scratch() {
    static EffectScratch s;
    return s;
}
int32_t depthRange() { return int32_t(stage().depthRange); }
// GTE NCLIP over the truncated screen points (render only, as content/late_game/st40_fx)
int64_t nclip(const CameraPoint& p0, const CameraPoint& p1, const CameraPoint& p2) {
    return int64_t(p0.sx) * (p1.sy - p2.sy) + int64_t(p1.sx) * (p2.sy - p0.sy) + int64_t(p2.sx) * (p0.sy - p1.sy);
}
constexpr float kNoOff[4][2] = {};
constexpr float kNoOff2[2][2] = {};
constexpr float kNoOff3[3][2] = {};
// the shared quad test of the ring effects: drawn unless all four u16 sy >= 0xf0 and their mean (u16, -> the screen
// mean) > 0xef; OTZ = the deepest SZ >> 2 (-> the OT depth) in [1, the depth range] (`far` false: no upper bound)
bool quadOtz(const CameraPoint c[4], bool far) {
    bool any = false;
    for (int k = 0; k < 4; ++k) any = any || uint16_t(c[k].sy) < 0xf0;
    if (!any) {
        const uint16_t mean = uint16_t((int(c[0].sy) + c[1].sy + c[2].sy + c[3].sy) >> 2);
        scratch().meanY = mean;
        if (mean > 0xef) return false;
    }
    int32_t mx = cameraPointDepth(c[0]);
    for (int k = 1; k < 4; ++k) mx = std::max(mx, cameraPointDepth(c[k]));
    mx >>= 2;
    scratch().depth = mx;
    return mx >= 1 && (!far || mx <= depthRange());
}
void mid(const int16_t a[3], const int16_t b[3], int16_t o[3]) {
    for (int k = 0; k < 3; ++k) o[k] = t16((int(a[k]) + b[k]) >> 1);
}
void cp3(int16_t d[3], const int16_t s[3]) {
    for (int k = 0; k < 3; ++k) d[k] = s[k];
}
// the band halves of 0x43d050 / 0x43d970: two POLY_G4 0x3a (DR_MODE 0x40b6d0(p, 1, 1, 0x3e): abr 1) -- {v0 black,
// mid01 col, v2 black, mid23 col} and {mid01 col, v1 black, mid23 col, v3 black}
void bandHalves(uint32_t owner, const int16_t v[4][3], uint32_t col) {
    int16_t m01[3], m23[3];
    mid(v[0], v[1], m01);
    mid(v[2], v[3], m23);
    int16_t q[4][3];
    cp3(q[0], v[0]);
    cp3(q[1], m01);
    cp3(q[2], v[2]);
    cp3(q[3], m23);
    const uint32_t c0[4] = {0x3a000000u, col, 0, col};
    dc::otQuadG4(owner, c0, 1, q, kNoOff);
    cp3(q[0], m01);
    cp3(q[1], v[1]);
    cp3(q[2], m23);
    cp3(q[3], v[3]);
    const uint32_t c1[4] = {col | 0x3a000000u, 0, col, 0};
    dc::otQuadG4(owner, c1, 1, q, kNoOff);
}
// the POLY_FT4 texture words from the scratch (uv0 / uv1, uv2 / uv3, CLUT / tpage)
void ft4Words(uint32_t col, uint32_t w[10]) {
    EffectScratch& s = scratch();
    for (int k = 0; k < 10; ++k) w[k] = 0;
    w[1] = col;
    w[3] = uint32_t(s.clut) << 16 | s.texelHalves[0];
    w[5] = uint32_t(s.tpage) << 16 | s.texelHalves[1];
    w[7] = s.texelHalves[2];
    w[9] = s.texelHalves[3];
}

void ringBand(EffectActor& a) {
    RingBandFields& f = classState<RingBandFields>(a);
    EffectScratch& s = scratch();
    int v13 = 0x20;
    if (a.state == 0) {
        a.enterState(1);
        f.clearBlockBytes(); // exe 0x40dc40
        f.lift = int16_t(uint32_t(int32_t(f.arg0)) << 2);
        f.radius = int16_t(uint32_t(int32_t(f.arg1)) << 2);
        f.counter = int16_t(uint32_t(int32_t(f.arg2 & 0x10)) << 4);
    } else if (a.state == 1) {
        if (a.stepCounter != 0) a.enterState(2);
    } else {
        if (a.subState == 0) {
            f.counter = 0;
            ++a.subState;
        }
        f.radius = int16_t(uint16_t(f.radius) + 4u);
        v13 = (0x10 - f.counter) * 2;
        if (f.counter == 0x10) {
            freeActor(a); // exe 0x5e1580
            return;
        }
    }
    const ShortVec3& bone = player().bonePoints[0];
    a.pos.x = int16_t(bone.x >> sh());
    a.pos.y = int16_t((bone.y >> sh()) - f.lift);
    a.pos.z = int16_t(bone.z >> sh());
    f.spin = int16_t(uint16_t(f.spin) + ((f.arg2 & 1) ? 0x18u : uint32_t(-0x18)));
    const uint32_t frame = gameState().frame;
    f.wobbleX = int16_t(fixedSin(int((frame & 0x7f) << 5)) >> 7);
    f.wobbleZ = int16_t(fixedSin(int((frame & 0x7f) << 5)) >> 7);
    if (!(a.flags & kActorDrawn)) return;
    const int16_t ctr[3] = {shl(a.pos.x), shl(a.pos.y), shl(a.pos.z)};
    const CameraPoint cc = cameraPoint(ctr); // 0x40c3c0 / 0x40c3e0 / 0x40c6a0 / 0x40c860
    if (cc.clipped) return;                                          // exe 0x40cae0 & 8
    if (uint16_t(cc.sy) > 0xef || uint16_t(cc.sx) > 0x13f) return;   // exe 0x40cb10
    const int32_t d = cameraPointDepth(cc) >> 2;                     // exe 0x40d010
    s.depth = d;
    if (d < 1 || d > depthRange()) return;
    s.tpage = 0x3e;
    s.clut = 0x7c49;
    s.texelWords[0] = 0x40c07fc0;
    s.texelWords[1] = 0x40ff7fff;
    const Matrix3 R = rotationZXY(f.wobbleX, f.spin, f.wobbleZ); // exe 0x40c020
    const int r = f.radius;
    const int32_t dx = (mul(fixedSin(int(0x80)), r) >> 12) - (mul(fixedSin(int(0)), r) >> 12);
    const int32_t dz = (-mul(fixedCos(int(0x80)), r) >> 12) - (-mul(fixedCos(int(0)), r) >> 12);
    const int32_t chord = isqrt(int32_t(uint32_t(dz) * uint32_t(dz) + uint32_t(dx) * uint32_t(dx))); // exe 0x40b980
    const uint32_t g = frame & 0x3f;
    const uint8_t b0 = uint8_t(uint32_t(((fixedCos(int(g << 6)) >> 6) + 0x50) * v13) >> 5);
    const uint8_t b1 = uint8_t(uint32_t(((fixedSin(int(g << 6)) >> 6) + 0x50) * v13) >> 5);
    s.colours[0] = uint32_t(b1) << 8 | b0;
    s.colours[1] = uint32_t(b1 >> 1) << 8 | uint32_t(b0 >> 1);
    const int16_t h = t16(chord >> 1);
    const int16_t centre[3] = {a.pos.x, a.pos.y, a.pos.z};
    // the point at angle index k (k * 0x80) and height y, rotated by R (exe 0x40bab0 >> 12), then + the centre << shift
    auto point = [&](uint32_t k, int16_t y, int16_t o[3]) {
        const uint32_t an = (k & 0x1f) * 0x80;
        const int32_t v[3] = {t16(mul(fixedSin(int(an)), r) >> 12), y, t16(-mul(fixedCos(int(an)), r) >> 12)};
        for (int i = 0; i < 3; ++i) {
            const int32_t mac = (R.e[i * 3] * v[0] + R.e[i * 3 + 1] * v[1] + R.e[i * 3 + 2] * v[2]) >> 12;
            o[i] = t16(int32_t(uint32_t(int32_t(t16(centre[i] + mac))) << sh()));
        }
    };
    int16_t prevTop[3], prevBot[3];
    point(0, t16(-h), prevTop);
    point(0, h, prevBot);
    for (uint32_t i = 0; i < 0x20; ++i) {
        int16_t v[4][3];
        cp3(v[0], prevTop);
        cp3(v[1], prevBot);
        point(i + 1, t16(-h), v[2]);
        point(i + 1, h, v[3]);
        CameraPoint c[4];
        for (int k = 0; k < 4; ++k) c[k] = cameraPoint(v[k]);
        const int64_t n1 = nclip(c[0], c[1], c[2]), n2 = nclip(c[1], c[2], c[3]); // 0x6d01e8 / 0x6d01ec
        if (quadOtz(c, true)) {
            const uint32_t col = (n1 >= 1 || n2 <= -1) ? s.colours[1] : s.colours[0];
            s.colours[2] = col;
            if (!(i & 1)) {
                uint32_t w[10];
                ft4Words(col | 0x2e000000u, w);
                dc::otQuadFT4(a.at, w, v);
            }
            bandHalves(a.at, v, col);
        }
        cp3(prevTop, v[2]);
        cp3(prevBot, v[3]);
    }
    f.counter = int16_t(uint16_t(f.counter) + 1u);
}

void glowDisc(EffectActor& a) {
    GlowDiscFields& f = classState<GlowDiscFields>(a);
    EffectScratch& s = scratch();
    if (a.state == 0) {
        f.clearBlockBytes(); // exe 0x40dc40
        f.counter = 0;
        f.radius = int16_t(uint32_t(int32_t(f.arg0)) << 2);
        f.height = int16_t(uint32_t(int32_t(f.arg1)) << 2);
        if (f.followPlayer != 0) {
            a.enterState(1);
            f.slotByte = player().slot;
        } else {
            a.enterState(2);
        }
    } else if (a.state == 1 && f.counter == 0x10) {
        f.counter = 0;
        a.enterState(2);
    }
    const uint32_t p = actorBySlot(uint32_t(int32_t(f.slot))); // exe 0x5e0fa0
    if (p == 0) notPortedAt(kStoryPort, "0x43d970: exe 0x5e0fa0 null (the exe reads through it), slot", f.slotByte);
    const Actor& followed = actorLayoutAt(p);
    if (!(followed.flags & kActorLive)) {
        freeActor(a); // exe 0x5e1580
        return;
    }
    a.pos.x = followed.pos.x.whole;
    a.pos.y = followed.pos.y.whole;
    a.pos.z = followed.pos.z.whole;
    if (!(a.flags & kActorDrawn)) return;
    s.textureWord = 0x3e7c49;
    const uint32_t g = gameState().frame & 0x3f;
    const int sn = fixedSin(int(g << 6)), cs = fixedCos(int(g << 6));
    uint8_t c1, c2;
    if (f.followPlayer != 0) {
        s.texelWords[0] = 0x803f8000u;
        s.texelWords[1] = 0xbf3fbf00u;
        if (a.state == 2) {
            c1 = uint8_t((cs >> 6) + 0x50);
            c2 = uint8_t((sn >> 6) + 0x50);
        } else {
            const int n = f.counter;
            c1 = uint8_t(uint32_t(((cs >> 6) + 0x50) * n) >> 4);
            c2 = uint8_t(uint32_t(((sn >> 6) + 0x50) * n) >> 4);
        }
    } else {
        s.texelWords[0] = 0x20bf2080u;
        s.texelWords[1] = 0x5fbf5f80u;
        c1 = uint8_t((sn >> 6) + 0x50);
        c2 = uint8_t((cs >> 6) + 0x50);
    }
    const uint32_t col = uint32_t(c2) << 16 | uint32_t(c1) << 8;
    s.colours[0] = col;
    const int x = a.pos.x, y = a.pos.y, z = a.pos.z, r = f.radius;
    {
        int16_t v[4][3] = {{shl(x - r), shl(y), shl(z - r)},
                           {shl(x + r), shl(y), shl(z - r)},
                           {shl(x - r), shl(y), shl(z + r)},
                           {shl(x + r), shl(y), shl(z + r)}};
        CameraPoint c[4];
        for (int k = 0; k < 4; ++k) c[k] = cameraPoint(v[k]);
        if (quadOtz(c, true)) {
            uint32_t w[10];
            ft4Words(col | 0x2e000000u, w);
            dc::otQuadFT4(a.at, w, v);
        }
    }
    const int rr = r + 0x10;
    const int32_t hh = (sn >> 9) + f.height;
    const int16_t top = shl(y - t16(hh)), bot = shl(t16(hh >> 1) + y);
    auto point = [&](uint32_t k, int16_t yy, int16_t o[3]) {
        const uint32_t an = (k & 0x1f) * 0x80;
        o[0] = shl(t16(mul(fixedSin(int(an)), rr) >> 12) + x);
        o[1] = yy;
        o[2] = shl(t16(-mul(fixedCos(int(an)), rr) >> 12) + z);
    };
    int16_t prevTop[3], prevBot[3];
    point(0, top, prevTop);
    point(0, bot, prevBot);
    for (uint32_t i = 0; i < 0x20; ++i) {
        int16_t v[4][3];
        cp3(v[0], prevTop);
        cp3(v[1], prevBot);
        point(i + 1, top, v[2]);
        point(i + 1, bot, v[3]);
        CameraPoint c[4];
        for (int k = 0; k < 4; ++k) c[k] = cameraPoint(v[k]);
        if (quadOtz(c, true)) bandHalves(a.at, v, col);
        cp3(prevTop, v[2]);
        cp3(prevBot, v[3]);
    }
    f.counter = int16_t(uint16_t(f.counter) + 1u);
}

// ---- 0x43e260 (variant 2): a 32 x 32 POLY_GT4 cylinder (radius 0x280) around the world origin, its rows turning
// alternately with the frame counter; back faces (NCLIP) skipped. Never freed by itself.
void swirl(const EffectActor& a) {
    EffectScratch& s = scratch();
    const int32_t dx = (mul(fixedSin(int(0x80)), 0x280) >> 12) - (mul(fixedSin(int(0)), 0x280) >> 12);
    const int32_t dz = (mul(fixedCos(int(0x80)), -0x280) >> 12) - (mul(fixedCos(int(0)), -0x280) >> 12);
    const int32_t seg = isqrt(int32_t(uint32_t(dz) * uint32_t(dz) + uint32_t(dx) * uint32_t(dx))); // 0x40b980
    const int16_t v2 = t16(int32_t(uint32_t(seg) << sh()));
    s.textureWord = 0x3e7c49;
    s.texelWords[0] = 0x3fff00ff;
    s.texelWords[1] = 0x3fc000c0;
    s.colours[1] = 0x201020;
    s.colours[0] = 0x201020;
    const uint32_t f = gameState().frame;
    for (uint32_t row = 0; row < 0x20; ++row) {
        const int16_t y0 = t16(int32_t(t16(row)) * v2 + (v2 * -0x10 - int32_t(uint32_t(0x80) << sh())));
        const int16_t y1 = t16(y0 + v2);
        const uint32_t base = (row & 1) ? f : uint32_t(-int32_t(f));
        auto point = [&](uint32_t an, int16_t yy, int16_t o[3]) {
            o[0] = t16(int32_t(uint32_t(mul(fixedSin(int(an & 0xfff)), 0x280) >> 12) << sh()));
            o[1] = yy;
            o[2] = t16(int32_t(uint32_t(mul(fixedCos(int(an & 0xfff)), -0x280) >> 12) << sh()));
        };
        int16_t pt[3], pb[3];
        point(base, y0, pt);
        point(base, y1, pb);
        for (uint32_t i = 0; i < 0x20; ++i) {
            int16_t v[4][3];
            cp3(v[0], pt);
            cp3(v[1], pb);
            point((i + 1) * 0x80 + base, y0, v[2]);
            point((i + 1) * 0x80 + base, y1, v[3]);
            CameraPoint c[4];
            for (int k = 0; k < 4; ++k) c[k] = cameraPoint(v[k]);
            const int64_t n1 = nclip(c[0], c[1], c[2]), n2 = nclip(c[1], c[2], c[3]);
            if ((n1 >= 0 || n2 <= 0) && quadOtz(c, true)) {
                const uint32_t ca = s.colours[0], cb = s.colours[1];
                const uint32_t w[13] = {0,  ca | 0x3e000000u, 0, uint32_t(s.clut) << 16 | s.texelHalves[0], cb, 0,
                                        uint32_t(s.tpage) << 16 | s.texelHalves[1], ca, 0,
                                        uint32_t(s.texelHalves[2]), cb, 0, uint32_t(s.texelHalves[3])};
                dc::otQuadGT4(a.at, w, v, kNoOff);
            }
            cp3(pt, v[2]);
            cp3(pb, v[3]);
        }
    }
}

// the billboard record (10 words; word 0 left as is) at the scratch point
void billboard(uint32_t owner, uint32_t colour, int16_t x, int16_t y, int16_t z, uint32_t w3, uint32_t w6) {
    EffectScratch& s = scratch();
    s.point.x = x;
    s.point.y = y;
    s.point.z = z;
    s.pointSpare = 0;
    dc::emit({0, dc::billboardColour(colour), uint32_t(s.pointXY), w3, uint32_t(s.pointZ), 0x2f203fu, w6, 0x3f20u, 0, 0x3f3fu}, owner,
             false);
}

// ---- 0x43e730 (variant 3): sparkles at random bones of the actor in its spawn slot -----------------------------------
// Init: 2 points at bones (arg0 + i) % 9 (>> shift, - (rng & 0x1f) + 0x10), 2 around the actor (+-0x20, y - 0x60 ..
// -0x20); its life 0x18 - (rng & 7), its yaw and yaw step, its size 3 / 4, its drift (s8). Each tick 4 billboards
// (brightness ramping to 0xc0 at half life and back), the age + 1 (freed at the life); the points drift by Ry(yaw << 4 +
// k * step * 16) * drift >> 16 (exe 0x5e3750); the yaw += 4, the step += 2.
struct BoneSparklesFields : EndingEffectParams {
    explicit BoneSparklesFields(const EffectActor& actor) : BoneSparklesFields(const_cast<EffectActor&>(actor)) {}
    explicit BoneSparklesFields(EffectActor& actor)
        : EndingEffectParams(actor), points{actor}, size(actor.params[0]), drift(actor.params, 1), slot_(actor) {}
    EffectPoints points; // the four points
    FieldAlias<int8_t, uint8_t> size;
    ArrayFields<int8_t, 3, uint8_t, 4> drift;
    // the block from the trail on (the last two points and a half) cleared
    void clearBlock() { slot_.trail = {}; }

private:
    EffectActor& slot_;
};
void boneSparkles(EffectActor& a) {
    BoneSparklesFields f(a);
    EffectScratch& s = scratch();
    if (a.state == 0) {
        f.clearBlock(); // exe 0x40dc40
        const uint32_t p = actorBySlot(uint32_t(int32_t(f.slot)));
        if (p == 0) notPortedAt(kStoryPort, "0x43e730: exe 0x5e0fa0 null, slot", uint8_t(f.slot));
        const AnimatedActor& owner = animatedActorAt(p);
        uint32_t q = 0;
        for (int i = 0; i < 2; ++i, ++q) {
            const int32_t bn = (int32_t(f.arg0) + i) % 9;
            if (bn < 0) notPortedAt(kStoryPort, "0x43e730: negative bone index", uint32_t(bn));
            BoneBlock* block = boneBlockOf(owner, int(uint32_t(bn) >> 2)); // exe 0x5e1780 + 0x80 + (n & 3) * 8
            if (!block) notPortedAt(kStoryPort, "0x43e730: exe 0x5e1780 null, bone", uint32_t(bn));
            const ShortVec4& bp = block->points[uint32_t(bn) & 3];
            const int16_t lanes[3] = {bp.x, bp.y, bp.z};
            ShortTriple point = f.points[q];
            int16_t* out[3] = {&point.x, &point.y, &point.z};
            for (uint32_t k = 0; k < 3; ++k) {
                const uint32_t r = rngStep();
                *out[k] = int16_t((lanes[k] >> sh()) - int(r & 0x1f) + 0x10);
            }
        }
        for (int i = 0; i < 2; ++i, ++q) {
            ShortTriple point = f.points[q];
            point.x = int16_t(owner.pos.x.whole - int(rngStep() & 0x3f) + 0x20);
            point.y = int16_t(int(rngStep() & 0x3f) + owner.pos.y.whole - 0x60);
            point.z = int16_t(owner.pos.z.whole - int(rngStep() & 0x3f) + 0x20);
        }
        a.subState = uint8_t(0x18 - (rngStep() & 7));
        a.step = uint8_t(rngStep());
        a.stepCounter = uint8_t(rngStep());
        f.size = int8_t((rngStep() & 1) + 3);
        f.drift[0] = int8_t(0x40 - (rngStep() & 0x7f));
        f.drift[1] = int8_t(0xe0 - (rngStep() & 0xf));
        f.drift[2] = int8_t(0x40 - (rngStep() & 0x7f));
    }
    const uint32_t life = a.subState, age = a.state, half = life >> 1;
    if (half == 0) notPorted(kStoryPort, "0x43e730: zero half life (the exe divides by zero)");
    const uint32_t v9 = (half <= age ? life * 0xc0 - age * 0xc0 : age * 0xc0) / half;
    const uint32_t colour = v9 | ((v9 & 0x1fe) << 7 | v9) << 8;
    for (uint32_t k = 0; k < 4; ++k) {
        const ShortTriple q = f.points[k];
        billboard(a.at, colour, shl(q.x), shl(q.y), shl(q.z), 0x7c502020u, uint32_t(int32_t(f.size)));
    }
    a.state = uint8_t(age + 1u);
    if (a.state == a.subState) {
        freeActor(a); // exe 0x5e1580
        return;
    }
    const uint32_t step = a.stepCounter;
    uint32_t yaw = uint32_t(a.step) << 4;
    s.drift.x = f.drift[0];
    s.drift.y = f.drift[1];
    s.drift.z = f.drift[2];
    for (uint32_t k = 0; k < 4; ++k) {
        ShortTriple q = f.points[k];
        s.point.x = q.x;
        s.point.y = q.y;
        s.point.z = q.z;
        moveAlongYaw16(s.point, int(yaw), s.drift.x, s.drift.y, s.drift.z); // exe 0x5e3750
        q.x = s.point.x;
        q.y = s.point.y;
        q.z = s.point.z;
        yaw += step * 0x10;
    }
    a.step += 4u;
    a.stepCounter += 2u;
}

void trail(EffectActor& a) {
    TrailFields& f = classState<TrailFields>(a);
    EffectScratch& s = scratch();
    uint32_t v2;
    if (a.state == 0) {
        if (a.subState == 0) {
            f.block = {}; // exe 0x40dc40
            a.step = 0;
            for (uint32_t k = 0; k < 8; ++k) f.heights[k] = a.pos.y;
            ++a.subState;
        }
        v2 = a.step;
        if (a.step == 0x3f) a.enterStateKeepingStep(1);
    } else if (a.state == 1) {
        if (a.subState == 0) {
            a.subState = 1;
            a.step = 0;
        }
        v2 = 0xffffffffu;
        if (a.stepCounter == 1) a.enterStateKeepingStep(2);
    } else {
        if (a.subState == 0) {
            a.subState = 1;
            a.step = 0;
        }
        v2 = (0x20 - uint32_t(a.step)) * 2;
        if (a.step == 0x20) {
            freeActor(a); // exe 0x5e1580
            return;
        }
    }
    for (uint32_t k = 7; k >= 1; --k) f.heights[k] = f.heights[k - 1];
    const uint32_t c = f.counter;
    f.heights[0] = int16_t((fixedSin(int((c & 0xff) << 4)) >> 10) + a.pos.y);
    if (!(f.counterLow & 3) && f.riseCount >= 1) {
        a.pos.y = int16_t(uint16_t(a.pos.y) + 1u);
        f.riseCount -= 1;
    }
    uint32_t b0 = uint8_t((uint32_t(uint16_t(fixedSin(int((c & 0xff) << 4)))) >> 7) + 0x50);
    uint32_t b1 = uint8_t((uint32_t(uint16_t(fixedCos(int((c & 0x7f) << 5)))) >> 7) + 0x50);
    uint32_t b2 = uint8_t(0x50 - (uint32_t(uint16_t(fixedSin(int((c & 0x3f) << 6)))) >> 7));
    if (v2 != 0xffffffffu) {
        b0 = uint8_t(int32_t(b0 * v2) >> 6);
        b1 = uint8_t(int32_t(b1 * v2) >> 6);
        b2 = uint8_t(int32_t(b2 * v2) >> 6);
    }
    s.colours[0] = b2 << 16 | b1 << 8 | b0;
    for (uint32_t k = 1; k < 8; ++k) {
        const uint32_t sc = (8 - k) * 8, o = 4 + (k - 1) * 4;
        s.colourBytes[o] = uint8_t(int32_t(b0 * sc) >> 6);
        s.colourBytes[o + 1] = uint8_t(int32_t(b1 * sc) >> 6);
        s.colourBytes[o + 2] = uint8_t(int32_t(b2 * sc) >> 6);
        s.colourBytes[o + 3] = 0;
    }
    for (uint32_t j = 0; j < 8; ++j)
        billboard(a.at, s.colours[j], shl(a.pos.x), shl(f.heights[j]), shl(a.pos.z), 0x7d502020u,
                  ((c + j) & 0xf) << 24 | ((0x100u + j * 0x100u) >> 6));
    if (a.state == 1 && !(rngStep() & 7)) {
        if (const uint32_t slot = allocEndingEffect()) { // exe 0x5e13a0: a puff
            EffectActor& puff = effectActorAt(slot);
            puff.flags = kActorLiveDrawn;
            puff.type = kEndingEffectType;
            puff.variant = uint8_t(EndingEffect::Puff);
            puff.loopFlags |= kActorSweepGroup2;
            puff.pos.x = a.pos.x;
            puff.pos.y = f.heights[0];
            puff.pos.z = a.pos.z;
            puff.setParamsWord(s.colours[0]);
        }
    }
    f.counter += 1u;
    ++a.step;
}

void puff(EffectActor& a) {
    PuffFields& f = classState<PuffFields>(a);
    EffectScratch& s = scratch();
    if (a.state == 0) {
        a.enterState(1);
        f.clearBlockBytes(); // exe 0x40dc40
        f.counter = 0;
        f.life = int16_t(0x28 - (rngStep() & 0xf));
        f.yaw = int16_t(rngStep() & 0xfff);
        f.yawStep = int16_t((rngStep() & 7) + 8);
        f.size = int16_t((rngStep() & 1) + 2);
        f.drift.x = int16_t(0x20 - (rngStep() & 0x3f));
        f.drift.y = int16_t(0xfff0 - (rngStep() & 0xf));
        f.drift.z = int16_t(0x20 - (rngStep() & 0x3f));
    }
    const int32_t d = f.life, n = f.counter;
    const uint32_t den = uint32_t(d) >> 1;
    if (den == 0) notPorted(kStoryPort, "0x43ef80: zero life (the exe divides by zero)");
    const int32_t k = (d >> 1) <= n ? d - n : n;
    const uint32_t b0 = uint8_t(uint32_t(mul(k, f.colour[0])) / den);
    const uint32_t b1 = uint8_t(uint32_t(mul(k, f.colour[1])) / den);
    const uint32_t b2 = uint8_t(uint32_t(mul(k, f.colour[2])) / den);
    s.colours[0] = b2 << 16 | b1 << 8 | b0;
    billboard(a.at, s.colours[0], shl(a.pos.x), shl(a.pos.y), shl(a.pos.z), 0x7c502020u, uint32_t(int32_t(f.size)));
    f.counter = int16_t(uint16_t(f.counter) + 1u);
    if (f.counter == f.life) {
        freeActor(a); // exe 0x5e1580
        return;
    }
    moveAlongYaw16(a.pos, f.yaw, f.drift.x, f.drift.y, f.drift.z); // exe 0x5e3750
    f.yaw = int16_t(uint16_t(f.yaw) + uint16_t(f.yawStep));
}

// ---- 0x43ffa0 (type 0x24 variant 0): the light pillar ------------------------------------------------------------------
// 0x40 ticks (its counter); colours from v = 0x40 - counter ({v / 2, v * 2, v * 255 / 64} and 28 / 24 / 20 / 16 / 20 /
// 24 / 28 >> 5 copies in the scratch); a 64-segment POLY_G4 ring of radius 0x60 from y - 0x140 (black) to y (coloured),
// turning with the counter; streaks (variant 1) around it: 16 the first tick, then 2 per tick while the counter <= 0x1f
constexpr uint8_t kStreakVariant = 1;
void lightPillar(EffectActor& a) {
    EffectScratch& s = scratch();
    if (a.state == 0) a.enterState(1);
    const uint32_t v7 = 0x40 - uint32_t(a.subState);
    if (v7 == 0) {
        freeActor(a); // exe 0x5e1580
        return;
    }
    const uint32_t b[3] = {((v7 << 5) >> 6) & 0xff, ((v7 << 7) >> 6) & 0xff, ((v7 * 0xff) >> 6) & 0xff};
    const uint32_t scale[8] = {32, 28, 24, 20, 16, 20, 24, 28};
    for (uint32_t j = 0; j < 8; ++j) {
        s.colours[j] = 0;
        for (uint32_t k = 0; k < 3; ++k) s.colourBytes[j * 4 + k] = uint8_t(j == 0 ? b[k] : (b[k] * scale[j]) >> 5);
    }
    const int x = a.pos.x, y = a.pos.y, z = a.pos.z;
    const uint32_t s9 = a.subState;
    auto point = [&](uint32_t k, int yy, int16_t o[3]) {
        const int an = int((k & 0x3f) << 6);
        o[0] = shl(t16(mul(fixedSin(an), 0x60) >> 12) + x);
        o[1] = shl(yy);
        o[2] = shl(t16(mul(fixedCos(an), -0x60) >> 12) + z);
    };
    int16_t pt[3], pb[3];
    point(s9, y - 0x140, pt);
    point(s9, y, pb);
    for (uint32_t i = 0; i < 0x40; ++i) {
        int16_t v[4][3];
        cp3(v[0], pt);
        cp3(v[1], pb);
        point(s9 + 1 + i, y - 0x140, v[2]);
        point(s9 + 1 + i, y, v[3]);
        CameraPoint c[4];
        for (int k = 0; k < 4; ++k) c[k] = cameraPoint(v[k]);
        if (quadOtz(c, false)) {
            const uint32_t col[4] = {0x3a000000u, uint32_t(s.colours[i & 7]), 0, uint32_t(s.colours[(i + 1) & 7])};
            dc::otQuadG4(a.at, col, 1, v, kNoOff); // DR_MODE 0x40b6d0(p, 1, 1, 0x3e)
        }
        cp3(pt, v[2]);
        cp3(pb, v[3]);
    }
    if (s9 <= 0x1f) {
        const uint32_t n = s9 != 0 ? 2 : 0x10;
        for (uint32_t k = 0; k < n; ++k) {
            const uint32_t slot = allocEndingEffect(); // exe 0x5e13a0
            if (!slot) continue;
            EffectActor& streak = effectActorAt(slot);
            streak.flags = kActorLiveDrawn;
            streak.type = kStageDustEffectType;
            streak.variant = kStreakVariant;
            streak.loopFlags |= kActorSweepGroup2 | kActorLateLoop;
            const int angle = int(rngStep() & 0xfff);
            streak.pos.x = int16_t(t16(mul(fixedSin(angle), 0x60) >> 12) + a.pos.x);
            const uint32_t r = rngStep();
            streak.pos.y = n == 2 ? int16_t(a.pos.y - int(r & 0x1f)) : int16_t(a.pos.y - int(r & 0x3f) - 0x40);
            streak.setRecordYaw(0);
            streak.pos.z = int16_t(t16(mul(fixedCos(angle), 0x60) >> 12) + a.pos.z);
        }
    }
    a.subState = uint8_t(s9 + 1u);
}

// ---- 0x440670 (type 0x24 variant 1): a rising streak -------------------------------------------------------------------
// Init: its life (rng & 7) + 0x10, its colour (rng & 0x7f) + 0x80 each, its length (rng & 3) + 4. Each tick a LINE_G2
// 0x52 (DR_MODE abr 1) from the point (colour * left / life, unsigned division of the s8 product) to the length below
// (black), y -= length; its age + 1, freed at its life.
struct StreakFields {
    explicit StreakFields(const EffectActor& actor) : StreakFields(const_cast<EffectActor&>(actor)) {}
    explicit StreakFields(EffectActor& actor) : life(actor.step), colour(actor.params, 0), length(actor.params[3]) {}
    uint8_t& life;
    ArrayFields<int8_t, 3, uint8_t, 4> colour;
    FieldAlias<int8_t, uint8_t> length;
};
void risingStreak(EffectActor& a) {
    StreakFields f(a);
    EffectScratch& s = scratch();
    if (a.state == 0) {
        a.enterStateBytes(1);
        const uint32_t r = rngStep();
        a.stepCounter = 0;
        f.life = uint8_t((r & 7) + 0x10);
        f.colour[0] = int8_t((rngStep() & 0x7f) + 0x80);
        f.colour[1] = int8_t((rngStep() & 0x7f) + 0x80);
        f.colour[2] = int8_t((rngStep() & 0x7f) + 0x80);
        f.length = int8_t((rngStep() & 3) + 4);
    }
    const int32_t left = int32_t(f.life) - int32_t(a.subState);
    if (left == 0) {
        freeActor(a); // exe 0x5e1580
        return;
    }
    const uint32_t life = f.life;
    const uint32_t b0 = uint8_t(uint32_t(mul(f.colour[0], left)) / life);
    const uint32_t b1 = uint8_t(uint32_t(mul(f.colour[1], left)) / life);
    const uint32_t b2 = uint8_t(uint32_t(mul(f.colour[2], left)) / life);
    s.colours[0] = b2 << 16 | b1 << 8 | b0;
    const int x = a.pos.x, y = a.pos.y, z = a.pos.z;
    const int16_t v[2][3] = {{shl(x), shl(y), shl(z)}, {shl(x), shl(t16(f.length + y)), shl(z)}};
    a.pos.y = int16_t(y - f.length);
    const CameraPoint c0 = cameraPoint(v[0]), c1 = cameraPoint(v[1]); // 0x40c6a0 / 0x40c860 / 0x40d010
    const int32_t otz = ((cameraPointDepth(c1) >> 2) + (cameraPointDepth(c0) >> 2)) >> 1;
    s.depth = otz;
    if (otz >= 1) {
        const uint32_t col[2] = {s.colours[0] | 0x52000000u, 0};
        dc::otLineG2(a.at, col, 1, v, kNoOff2); // DR_MODE 0x40b6d0(p, 1, 1, 0x3e)
    }
    ++a.subState;
}

// the three-corner test of 0x43f260 (as quadOtz): drawn unless all three u16 sy >= 0xf0 and their mean (s16 sum / 3
// toward zero, u16 -> the screen mean) >= 0xf0; OTZ = the deepest SZ1..3 (exe 0x40cf40 -> 0x6d01f0..) >> 2 (-> the OT
// depth) in [1, the depth range]
bool triOtz(const CameraPoint c[3]) {
    if (uint16_t(c[0].sy) >= 0xf0 && uint16_t(c[1].sy) >= 0xf0 && uint16_t(c[2].sy) >= 0xf0) {
        const uint16_t mean = uint16_t((int32_t(c[0].sy) + c[1].sy + c[2].sy) / 3);
        scratch().meanY = mean;
        if (mean >= 0xf0) return false;
    }
    int32_t mx = std::max(cameraPointDepth(c[0]), cameraPointDepth(c[1]));
    mx = std::max(mx, cameraPointDepth(c[2]));
    mx >>= 2;
    scratch().depth = mx;
    return mx >= 1 && mx <= depthRange();
}

// the three words a followed actor's slot holds from +0x10 (the centre copies them as they are): a model-bearing
// actor's position's first lanes (x's fraction and whole, y's fraction), an effect's s16 position, a controller's
// record point
std::array<uint16_t, 3> followedWords(uint32_t slot) {
    const ActorHeader& h = actorHeaderAt(slot);
    if (const auto* e = dynamic_cast<const EffectActor*>(&h)) return {uint16_t(e->pos.x), uint16_t(e->pos.y), uint16_t(e->pos.z)};
    if (const auto* c = dynamic_cast<const ControllerActor*>(&h))
        return {uint16_t(c->recordPoint.x), uint16_t(c->recordPoint.y), uint16_t(c->recordPoint.z)};
    const Actor& a = actorAt(slot);
    return {a.pos.x.frac, uint16_t(a.pos.x.whole), a.pos.y.frac};
}
void tumblingFan(EffectActor& a) {
    GlowFanFields& f = classState<GlowFanFields>(a);
    EffectScratch& s = scratch();
    int32_t k; // the fade factor, -1 = full
    if (a.state == 0) {
        if (a.subState == 0) {
            f.clearBlockBytes(); // exe 0x40dc40
            f.counter = 0;
            f.hold = int16_t(EndingEffectParams(a).arg0);
            f.anglesA.x = int16_t(rngStep() & 0xfff);
            f.anglesA.y = int16_t(rngStep() & 0xfff);
            f.anglesA.z = int16_t(rngStep() & 0xfff);
            f.anglesB.x = int16_t(uint16_t(f.anglesA.x) - (rngStep() & 0xff) + 0x80u);
            f.anglesB.y = int16_t(uint16_t(f.anglesA.y) - (rngStep() & 0xff) + 0x80u);
            f.anglesB.z = int16_t(uint16_t(f.anglesA.z) - (rngStep() & 0xff) + 0x80u);
            f.spins[0] = int8_t(2u - (rngStep() & 3));
            f.spins[1] = int8_t(2u - (rngStep() & 3));
            f.spins[2] = int8_t(2u - (rngStep() & 3));
            ++a.subState;
        }
        k = int32_t(uint32_t(int32_t(f.counter)) << 1);
        if (uint16_t(f.counter) == 0x20) a.enterStateKeepingStep(1);
    } else if (a.state == 1) {
        if (a.subState == 0) {
            f.counter = 0;
            ++a.subState;
        }
        k = -1;
        if (uint16_t(f.counter) == uint16_t(f.hold)) a.enterStateKeepingStep(2);
    } else {
        if (a.subState == 0) {
            f.counter = 0;
            ++a.subState;
        }
        k = int32_t(uint32_t(0x20 - int32_t(f.counter)) << 1);
        if (uint16_t(f.counter) == 0x20) {
            freeActor(a); // exe 0x5e1580
            return;
        }
    }
    if (f.followSlot != 0) {
        const uint32_t p = actorBySlot(f.followSlot); // exe 0x5e0fa0
        if (p == 0)
            notPortedAt(kStoryPort, "0x43f260: exe 0x5e0fa0 null (the exe reads through it), slot", f.followSlot);
        const std::array<uint16_t, 3> followed = followedWords(p);
        a.pos.x = int16_t(followed[0]);
        a.pos.y = int16_t(followed[1]);
        a.pos.z = int16_t(followed[2]);
    }
    int32_t r = f.radius;
    uint32_t colour;
    if (k != -1) {
        r = mul(r, k) >> 6;
        const uint32_t b = uint32_t(int32_t(uint32_t(k) << 7) >> 6);
        colour = b | ((b & 0x1fe) << 7 | b) << 8;
    } else {
        colour = 0x408080;
    }
    s.point.x = shl(a.pos.x);
    s.point.y = shl(a.pos.y);
    s.point.z = shl(a.pos.z);
    const uint32_t an = (uint8_t(f.counter) & 0x1f) * 0x80;
    const int16_t h = t16(r >> 2);
    const int16_t centre[3] = {a.pos.x, a.pos.y, a.pos.z};
    // one corner: the local point through exe 0x40c020(angles -> 0x6d00d8) on the identity (translation 0; 0x40c3c0 /
    // 0x40c3e0), 0x40c6a0 / 0x40c800 / exe 0x40caa0 (-> 0x6d00e0..), + the centre (16-bit add) << shift -> dst
    auto corner = [&](const ShortVec3& ang, LaneVec3& dst, int16_t y) {
        s.angles = ang;
        const Matrix3 R = rotationZXY(ang.x, ang.y, ang.z);
        const int16_t lv[3] = {t16(mul(fixedSin(int(an)), r) >> 12), y,
                               t16(int32_t(0u - uint32_t(mul(fixedCos(int(an)), r))) >> 12)};
        dst.x = lv[0];
        dst.y = lv[1];
        dst.z = lv[2];
        for (uint32_t i = 0; i < 3; ++i) {
            const uint32_t sum = uint32_t(int32_t(R.e[i * 3])) * uint32_t(int32_t(lv[0])) +
                                 uint32_t(int32_t(R.e[i * 3 + 1])) * uint32_t(int32_t(lv[1])) +
                                 uint32_t(int32_t(R.e[i * 3 + 2])) * uint32_t(int32_t(lv[2]));
            s.rotated[i] = int32_t(sum) >> 12;
        }
        Lane16* out[3] = {&dst.x, &dst.y, &dst.z};
        for (uint32_t i = 0; i < 3; ++i)
            *out[i] = int16_t(uint32_t(int32_t(uint32_t(uint16_t(uint16_t(centre[i]) + uint16_t(s.rotated[i]))))) << sh());
    };
    corner(f.anglesA, s.cornerA, h);
    corner(f.anglesB, s.cornerB, t16(-int32_t(h)));
    const int16_t v[3][3] = {{s.point.x, s.point.y, s.point.z},
                             {s.cornerA.x, s.cornerA.y, s.cornerA.z},
                             {s.cornerB.x, s.cornerB.y, s.cornerB.z}};
    // camera 0x40c3c0 / 0x40c3e0(0x6d0014), 0x40c770(0x6d00c0) / RTPT 0x40c930
    CameraPoint c[3];
    for (int i = 0; i < 3; ++i) c[i] = cameraPoint(v[i]);
    if (triOtz(c)) {
        int16_t m12[3];
        mid(v[1], v[2], m12); // the screen midpoint 0x6d011c, drawn at the 3D midpoint (file header deviation)
        const uint32_t cw[3] = {colour | 0x32000000u, 0, 0};
        int16_t t[3][3];
        cp3(t[0], v[0]);
        cp3(t[1], v[1]);
        cp3(t[2], m12);
        dc::otTriG3(a.at, cw, 1, t, kNoOff3); // DR_MODE 0x40b6d0(p, 1, 1, 0x3e) + POLY_G3 {c, v1, mid}
        cp3(t[1], m12);
        cp3(t[2], v[2]);
        dc::otTriG3(a.at, cw, 1, t, kNoOff3); // POLY_G3 {c, mid, v2}
    }
    f.counter = int16_t(uint16_t(f.counter) + 1u);
    f.anglesA.x = int16_t(uint16_t(f.anglesA.x) + uint32_t(int32_t(f.spins[0])));
    f.anglesA.y = int16_t(uint16_t(f.anglesA.y) + uint32_t(int32_t(f.spins[1])));
    f.anglesA.z = int16_t(uint16_t(f.anglesA.z) + uint32_t(int32_t(f.spins[2])));
    f.anglesB.x = int16_t(uint16_t(f.anglesB.x) + uint32_t(int32_t(f.spins[0])));
    f.anglesB.y = int16_t(uint16_t(f.anglesB.y) + uint32_t(int32_t(f.spins[1])));
    f.anglesB.z = int16_t(uint16_t(f.anglesB.z) + uint32_t(int32_t(f.spins[2])));
}

enum class PulseState : uint8_t { FadeIn = 0, Hold = 1, Pulse = 2, Flare = 3 };
void pulseGlow(EffectActor& a) {
    PulseGlowFields& f = classState<PulseGlowFields>(a);
    auto first = [&]() {
        if (a.subState == 0) {
            f.counter = 0;
            ++a.subState;
        }
    };
    int32_t e = 0;
    bool hold = false; // the hold: 0x306060 without the e test
    switch (a.state) { // [0x43fc54]
    case uint8_t(PulseState::FadeIn):
        first();
        e = int32_t(f.counter) * 2;
        if (f.counter == 0x20) a.enterState(uint8_t(PulseState::Hold));
        break;
    case uint8_t(PulseState::Hold):
        first();
        if (a.stepCounter != 0) a.enterState(uint8_t(PulseState::Pulse));
        hold = true;
        break;
    case uint8_t(PulseState::Pulse): {
        first();
        const uint32_t ph = uint16_t(f.counter);
        e = (fixedCos(int((ph & 0x1f) * 0x80)) >> 9) + 0x38; // 0x847ada + (ph & 0x1f) << 9
        if (a.stepCounter != 0 && (ph & 0x1f) == 0x1f) a.enterState(uint8_t(PulseState::Flare));
        break;
    }
    case uint8_t(PulseState::Flare):
        first();
        e = int32_t(f.counter) * 4 + 0x40;
        if (f.counter == 0x10) a.enterState(4);
        break;
    default:
        first();
        e = (0x20 - int32_t(f.counter)) * 4;
        a.pos.y = int16_t(uint16_t(a.pos.y) - 1u);
        if (f.counter == 0x20) {
            freeActor(a); // exe 0x5e1580
            return;
        }
        break;
    }
    uint32_t colour = 0x306060u;
    if (!hold && e != 0x40) {
        const uint32_t c = uint32_t(int32_t(uint32_t(e) * 0x60u) >> 6); // lea *3; shl 5; sar 6
        colour = c | (((c & 0x1fe) << 7) | c) << 8;
    }
    const int16_t pos[3] = {a.pos.x, a.pos.y, a.pos.z};
    int16_t sxy[2];
    projectOnScreen(pos, sxy); // exe 0x5f2dd0 (result ignored; S rebuilt in glowFan)
    const int32_t wy = projectedWidth(pos, t16(int32_t(f.slot) * 4), nullptr); // exe 0x5f2f20(.., 0)
    const int32_t wx = projectedWidth(pos, t16(int32_t(f.arg0) * 4), nullptr);
    glowFan(a, colour, wy, wx);
    f.counter = int16_t(uint16_t(f.counter) + 1u);
}

} // namespace

void endingTalkerAUpdate(const PlayerEnv& env, uint32_t actor) {
    Cue c(animatedActorAt(actor));
    castSavePose(c.a);
    if (c.a.variant != 0) notPortedAt(kStoryPort, "0x44b0b0 variant past [0x88e550]", c.a.variant);
    talkerACues(c);
    castTail(env, c, kCastOnGround, false, kCastHeld);
}
void endingTalkerBUpdate(const PlayerEnv& env, uint32_t actor) {
    Cue c(animatedActorAt(actor));
    castSavePose(c.a);
    if (c.a.variant != 0) notPortedAt(kStoryPort, "0x4787d0 variant past [0x890d88]", c.a.variant);
    talkerBCues(c);
    castTail(env, c, kCastOnGround, false, kCastHeld);
}
void endingMouthTalkerUpdate(const PlayerEnv& env, uint32_t actor) {
    Cue c(animatedActorAt(actor));
    castSavePose(c.a);
    if (c.a.variant != 0) notPortedAt(kStoryPort, "0x47d8c0 variant past [0x8912a0]", c.a.variant);
    mouthTalkerCues(c);
    castTail(env, c, kCastOnGround, true, kCastHeld);
}
void endingWalkerUpdate(const PlayerEnv& env, uint32_t actor) {
    Cue c(animatedActorAt(actor));
    castSavePose(c.a);
    if (c.a.variant != 0) notPortedAt(kStoryPort, "0x49b4c0 variant past [0x8933b0]", c.a.variant);
    walkerCues(c);
    castTail(env, c, kCastHoldPose, false, uint8_t(kCastHoldPose | kCastShadow)); // the class's ground / hold bits
}
void endingGroupCastUpdate(const PlayerEnv& env, uint32_t actor) {
    Cue c(animatedActorAt(actor));
    castSavePose(c.a);
    switch (c.a.variant) { // [0x895530]
    case 0: groupFirstCues(c); break;
    case 1: groupSecondCues(c); break;
    case 2: groupThirdCues(c); break;
    default: notPortedAt(kStoryPort, "0x4d5230 variant past [0x895530]", c.a.variant);
    }
    castTail(env, c, kCastOnGround, false, kCastHeld);
}
void endingGlowCastUpdate(const PlayerEnv& env, uint32_t actor) {
    Cue c(animatedActorAt(actor));
    castSavePose(c.a);
    switch (c.a.variant) { // [0x896760]
    case 0: glowFirstCues(c); break;
    case 1: glowSecondCues(c); break;
    default: notPortedAt(kStoryPort, "0x4ef2a0 variant past [0x896760]", c.a.variant);
    }
    castTail(env, c, kCastOnGround, false, kCastHeld);
}
void finaleCastAUpdate(const PlayerEnv& env, uint32_t actor) {
    Cue c(animatedActorAt(actor));
    castSavePose(c.a);
    switch (c.a.state) { // [0x8937a8]
    case 0: finaleAScene2(c); break;
    case 1: finaleAScene3(c); break;
    case 2: finaleAScene4(c); break;
    case 3: finaleAScene5(c); break;
    case 4: finaleAScene6(c); break;
    case 5: finaleAScene7(c); break;
    default: notPortedAt(kStoryPort, "0x49c270 state past [0x8937a8]", c.a.state);
    }
    castTail(env, c, 0, false, uint8_t(kCastHoldPose | kCastShadow));
}
void finaleCastBUpdate(const PlayerEnv& env, uint32_t actor) {
    Cue c(animatedActorAt(actor));
    castSavePose(c.a);
    switch (c.a.state) { // [0x896850]
    case 0: finaleBScene2(c); break;
    case 1: finaleBScene3(c); break;
    case 2: finaleBScene4(c); break;
    case 3: finaleBScene5(c); break;
    case 4: finaleBScene6(c); break;
    case 5: finaleBScene7(c); break;
    default: notPortedAt(kStoryPort, "0x4f0050 state past [0x896850]", c.a.state);
    }
    castTail(env, c, 0, false, uint8_t(kCastHoldPose | kCastShadow));
}
void bobbingPropUpdate(const PlayerEnv&, uint32_t actor) {
    AnimatedActor& a = animatedActorAt(actor);
    BobbingPropFields& prop = classState<BobbingPropFields>(a);
    castSavePose(a);
    switch (PropKind(prop.kind.get())) { // [0x8982e8]
    case PropKind::Bobbing: bobbing(a, prop); break;
    case PropKind::BobbingUntilStopped: bobbingUntilStopped(a, prop); break;
    case PropKind::Still: stillProp(a); break;
    default: notPortedAt(kStoryPort, "0x50b750 variant past [0x8982e8]", uint8_t(prop.kind));
    }
    if (!(a.flags & kActorLive)) return;
    if (a.anim == a.prevAnim) animStepPose(a); // exe 0x5e1e80
    else animSetPose(a, a.anim, 0);            // exe 0x5e1c30
}
void endingEffectUpdate(const PlayerEnv& env, uint32_t actor) {
    EffectActor& a = effectActorAt(actor);
    switch (EndingEffect(a.variant)) { // [0x88d970]
    case EndingEffect::RingBand: ringBand(a); return;
    case EndingEffect::GlowDisc: glowDisc(a); return;
    case EndingEffect::Swirl: swirl(a); return;
    case EndingEffect::BoneSparkles: boneSparkles(a); return;
    case EndingEffect::Trail: trail(a); return;
    case EndingEffect::Puff: puff(a); return;
    case EndingEffect::GlowFan: tumblingFan(a); return;
    case EndingEffect::PulseGlow: pulseGlow(a); return;
    case EndingEffect::AnimatedSprite: animatedFollowerSpriteUpdate(env, actor); return; // content/late_game/st4c_story
    case EndingEffect::StillSprite: stillFollowerSpriteUpdate(env, actor); return;       // content/late_game/st4c_story
    case EndingEffect::LightPillar: lightPillar(a); return;
    case EndingEffect::RisingStreak: risingStreak(a); return;
    case EndingEffect::DriftingPuffs: driftingPuffsUpdate(env, actor); return;  // content/late_game/st5c
    case EndingEffect::WarningRing: rockWarningRingUpdate(env, actor); return;  // content/late_game/st5c_fx2
    case EndingEffect::LandingDust: rockLandingDustUpdate(env, actor); return;  // content/late_game/st5c_fx2
    default: notPortedAt(kStoryPort, "0x43d030 variant past [0x88d970] (15 entries)", a.variant);
    }
}
void lightPillarUpdate(const PlayerEnv&, uint32_t actor) { lightPillar(effectActorAt(actor)); }
void risingStreaksUpdate(const PlayerEnv&, uint32_t actor) { risingStreak(effectActorAt(actor)); }

} // namespace od::port
