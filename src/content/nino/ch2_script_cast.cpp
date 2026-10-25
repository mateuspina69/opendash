// The cast of the Nino chapter's scripts 0x31 (ST46), 0x32 (ST19) and 0x37 (ST18; content/nino/ch2_scripts) and script
// 0x37's flash effect 0x4304d0, one routine per exe routine. Shadows (exe 0x5f20f0) are render output and left out.
// Names are hypotheses from the observed behaviour. The cast keys on the script's timeline: its step's sub-state and its
// step tick (game/script_state).
#include "content/nino/ch2_script_cast.hpp"
#include "content/nino/ch2_script_cast_state.hpp"
#include "engine/data_source.hpp"
#include "game/actor_state.hpp"
#include "game/stage_smoke.hpp"

#include "game/actor_animation.hpp"
#include "game/actor_model.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_stats.hpp"
#include "game/angles.hpp"
#include "game/draw_commands.hpp"
#include "game/keyframe_tracks.hpp"
#include "game/port_gap.hpp"
#include "game/projection.hpp"
#include "game/random.hpp"
#include "game/script_state.hpp"
#include "game/stage_state.hpp"
#include "game/story.hpp"

namespace od::port {

using namespace ch2_script_cast_state;

namespace {

constexpr uint8_t kNoAnim = 0xff;

int16_t wrap(int v) { return int16_t(uint16_t(uint32_t(v))); }
int32_t shiftRight(int32_t v, uint32_t n) { return v >> (n & 0x1f); }
// the script's step tick / step sub-state
int32_t stepTick() { return scriptState().stepTick; }
uint8_t stepSub() { return scriptState().stepSub; }
// the previous pose and contact of the class heads
void savePrevious(AnimatedActor& actor) {
    actor.savePreviousPose();
    actor.savePreviousContact();
}
// the class tails: exe 0x5e1c30(actor, anim, entry) on a change, else exe 0x5e1e80
void animTail(AnimatedActor& actor, uint32_t entry) {
    if (actor.anim != actor.prevAnim) animSetPose(actor, actor.anim, int(entry));
    else animStepPose(actor);
}

// =====================================================================================================================
// ---- 0x44a470: ST46 pool 0x20 type 2 sub 0 (script 0x31 record 0x8a5cec, slot 3) -----------------------------------
// States [0x88e414]: 0 0x44a530 bind (box 0x88e3d0) + a pool-0x20 type 0x47 child (its parameter 1; ST46 class 0x4cd290),
// texture tracks (0x88e3fc / 0x88e408); 1 0x44a620 -> [0x88e41c + variant] = 0x44a640 (variant 0 only); 2 0x44a640.
// 0x44a640: the tick >= 0x73 and the animation's frame 0x18 -> animation 2, tracks 1, the child's parameter 0; then
// the tick 0x9e -> animation 3, tracks 2.

// [0x88e414]
enum class TakeOffCastState : uint8_t { Bind = 0, Run = 1, Cues = 2 };
enum class TakeOffCue : uint8_t { First = 0, Second = 1, Done = 2 };
const DataTable<CollisionBoxRecord> takeOffCastBox{TableId::Ch2ScriptCastTakeOffCastBox};
constexpr uint8_t kChildType = 0x47;

// exe 0x44a530: the model, drawn, animation 1, one hit point, no motion, the box, the class block; the child; the tracks
void takeOffBind(AnimatedActor& actor, TakeOffCastFields& cast) {
    if (!bindActorModel(actor, 0)) { // exe 0x5e0ba0
        freeActor(actor);
        return;
    }
    actor.flags |= kActorDrawn;
    actor.anim = 1;
    actor.prevAnim = kNoAnim;
    actor.maxHitPoints = 1;
    actor.hitPoints = 1;
    actor.velocity.clearWords();
    actor.spin.setXY(0, 0);
    actor.rotation.z = 0;
    actor.rotation.x = 0;
    actor.collisionBox.pointAt(takeOffCastBox.stateAddress());
    actor.interactionBox = nullptr;
    actor.interaction = 0;
    actor.airborne = 0;
    actor.onPlatform = 0;
    actor.clearClassBlock(); // exe 0x40dc40
    actor.enterState(uint8_t(TakeOffCastState::Run));
    if (const uint32_t c = allocActor20()) { // exe 0x5e1160
        AnimatedActor& child = animatedActorAt(c);
        child.flags = kActorLiveDrawn;
        child.type = kChildType;
        child.subtype = 0;
        child.variant = 0;
        child.pos.x = actor.pos.x;
        child.pos.y = actor.pos.y;
        child.rotation.y = 0;
        child.params[0] = 1;
        child.pos.z = actor.pos.z;
        child.params[1] = 0;
        cast.child = &child;
    }
    cast.tracks.init(stateAddress(TableId::Ch2ScriptCastTakeOffTracksA), stateAddress(TableId::Ch2ScriptCastTakeOffTracksB)); // exe 0x5e33a0
    cast.tracks.first.select(0, 0);
    cast.tracks.second.select(0, 0);
}
// exe 0x44a640: the cues on the script's tick
void takeOffCues(AnimatedActor& actor, TakeOffCastFields& cast) {
    if (TakeOffCue(actor.subState) == TakeOffCue::First) {
        if (stepTick() < 0x73 || uint8_t(actor.animKey.frame) != 0x18) return;
        actor.anim = 2;
        cast.tracks.first.select(1, 0);
        cast.tracks.second.select(1, 0);
        if (cast.child) cast.child->params[0] = 0;
        actor.enterStep(0);
        actor.subState += 1u;
        return;
    }
    if (TakeOffCue(actor.subState) != TakeOffCue::Second || stepTick() != 0x9e) return;
    actor.anim = 3;
    cast.tracks.first.select(2, 0);
    cast.tracks.second.select(2, 0);
    actor.enterStep(0);
    actor.subState += 1u;
}
// exe 0x44a470
void takeOffCast(AnimatedActor& actor) {
    auto&& cast = classState<TakeOffCastFields>(actor);
    actor.prevAnim = actor.anim;
    actor.tint = kTintNeutral;
    savePrevious(actor);
    switch (TakeOffCastState(actor.state)) {
    case TakeOffCastState::Bind: takeOffBind(actor, cast); break;
    case TakeOffCastState::Run:
        if (actor.variant != 0) notPortedAt(kStoryPort, "0x44a470 variant (table 0x88e41c)", actor.variant);
        takeOffCues(actor, cast);
        break;
    case TakeOffCastState::Cues: takeOffCues(actor, cast); break;
    default: notPortedAt(kStoryPort, "0x44a470 state (table 0x88e414)", actor.state);
    }
    if (!(actor.flags & kActorLive)) return;
    textureTracksTick(actor, cast.tracks); // exe 0x5e34f0
    animTail(actor, 0);
    // exe 0x5f20f0(actor, 0x30, 0x160, 0): the shadow (render output)
}

// =====================================================================================================================
// ---- pool 0x20 type 0x2d: 0x497be0 (ST19 sub 0, script 0x32 records 0x8a5fe8..0x8a6038 = variants 0..4, slots 4..8)
// and 0x498160 (ST18 sub 2, script 0x37 records 0x8a6778..0x8a67c8 = variants 0..4 + 0x8a67f0.. variant 5). Variant
// tables: the model variant / the animation (0x892de4 / 0x892dec and 0x892e3c / 0x892e44), the mouth's part pair (u16
// part indices, their part flags' kPartHidden) 0x892e10 / 0x892ea8. The mouth flaps every 3 ticks inside tick windows
// (0x498090 / 0x498790); animated or decoded only; variant 5 a lying body (pitched up -0x400) with smoke (the stage's
// static type 3 variant 6 = ST18 0x592050) every 8th tick of a 32-step bit pattern.

// the mouth's part pairs by variant (exe data, u16 x 2)
struct MouthParts {
    TableId table;
};
constexpr MouthParts kMouthA = {TableId::Ch2ScriptCastMouthPartsA}, kMouthB = {TableId::Ch2ScriptCastMouthPartsB};
constexpr uint8_t kLyingVariant = 5;
constexpr uint8_t kLyingSmokeVariant = 6;

// the part flags of entry k of the variant's mouth pair
uint8_t& mouthPart(AnimatedActor& actor, const MouthParts& mouth, uint32_t k) {
    const uint32_t part = dataScalar<uint16_t>(stateAddress(mouth.table) + uint32_t(actor.variant) * 4 + k * 2);
    return actor.parts[part].flags;
}
// exe 0x497fd0 / 0x4986d0: closed -- part 0 hidden, part 1 shown
void mouthClosed(AnimatedActor& actor, TalkingCastFields& cast, const MouthParts& mouth) {
    cast.mouthOpen = 0;
    mouthPart(actor, mouth, 0) |= kPartHidden;
    mouthPart(actor, mouth, 1) &= uint8_t(~kPartHidden);
}
// exe 0x498030 / 0x498730: open -- part 0 shown, part 1 hidden
void mouthOpened(AnimatedActor& actor, TalkingCastFields& cast, const MouthParts& mouth) {
    cast.mouthOpen = 1;
    mouthPart(actor, mouth, 0) &= uint8_t(~kPartHidden);
    mouthPart(actor, mouth, 1) |= kPartHidden;
}
// exe 0x4980d0 / 0x4987d0: the mouth flipped
void mouthFlip(AnimatedActor& actor, TalkingCastFields& cast, const MouthParts& mouth) {
    if (cast.mouthOpen & 1) {
        mouthPart(actor, mouth, 0) &= uint8_t(~kPartHidden);
        mouthPart(actor, mouth, 1) |= kPartHidden;
    } else {
        mouthPart(actor, mouth, 0) |= kPartHidden;
        mouthPart(actor, mouth, 1) &= uint8_t(~kPartHidden);
    }
    cast.mouthOpen ^= 1u;
}
// exe 0x498090 / 0x498790: the mouth flapping every 3 ticks inside the window t0..t1
void mouthWindow(AnimatedActor& actor, TalkingCastFields& cast, const MouthParts& mouth, int32_t t0, int32_t t1) {
    const int32_t t = stepTick();
    if (t0 <= t && t <= t1 && (t - t0) % 3 == 0) mouthFlip(actor, cast, mouth);
}
// a cue: animated, the animation (restarted)
void cue(AnimatedActor& actor, TalkingCastFields& cast, uint8_t anim, bool restart) {
    cast.animated = 1;
    actor.anim = anim;
    if (restart) actor.prevAnim = kNoAnim;
}
// the shared tail of the two classes: a new animation from its start key, else stepped (animated) or decoded
void talkingTail(AnimatedActor& actor, TalkingCastFields& cast) {
    if (!(actor.flags & kActorLive)) return;
    if (actor.anim != actor.prevAnim) {
        animSetPose(actor, actor.anim, cast.animKeyStart); // exe 0x5e1c30
        cast.animKeyStart = 0;
        return;
    }
    if (cast.animated == 0) animDecodeCurrent(actor); // exe 0x5e1f30
    else animStepPose(actor);                         // exe 0x5e1e80
}
// the shared init: the variant's model and animation, drawn, the hit points (one, or its row's), the box, the class
// block, parts 2 shown and 6..8 hidden
bool talkingInit(AnimatedActor& actor, uint32_t models, uint32_t anims, const DataTable<CollisionBoxRecord>& box, bool oneHitPoint) {
    if (!bindActorModel(actor, dataScalar<uint8_t>(models + actor.variant))) { // exe 0x5e0bc0
        freeActor(actor);
        return false;
    }
    actor.flags |= kActorDrawn;
    actor.anim = dataScalar<uint8_t>(anims + actor.variant);
    actor.prevAnim = kNoAnim;
    int16_t hp = 1;
    if (!oneHitPoint) hp = hasAnimatedStats(actor.type) ? int16_t(animatedStats(actor.type).hitPoints) : int16_t(0);
    actor.rotation.z = 0;
    actor.rotation.x = 0;
    actor.interactionBox = nullptr;
    actor.interaction = 0;
    actor.airborne = 0;
    actor.onPlatform = 0;
    actor.maxHitPoints = hp;
    actor.hitPoints = hp;
    actor.collisionBox.pointAt(box.stateAddress());
    actor.clearClassBlock();
    actor.parts[2].flags &= uint8_t(~kPartHidden);
    actor.parts[6].flags |= kPartHidden;
    actor.parts[7].flags |= kPartHidden;
    actor.parts[8].flags |= kPartHidden;
    actor.enterState(1);
    return true;
}
// the variant-5 init: animated, the start key 0x10, the smoke pattern's step = the slot * 8 - 0x100
void lyingInit(AnimatedActor& actor, TalkingCastFields& cast) {
    cast.animated = 1;
    cast.animKeyStart = 0x10;
    cast.smokeStep = uint16_t(uint32_t(actor.slot) * 8 - 0x100u);
}

// ---- 0x497be0 variants [0x892df4]: 0 0x497dd0, 2 0x497e30, 3 0x497ed0, 4 0x497f60; 1 / 5 / 6 none ---------------
void script32Speaker0(AnimatedActor& actor, TalkingCastFields& cast) { // exe 0x497dd0
    mouthWindow(actor, cast, kMouthA, 0x3c, 0x6e);
    mouthWindow(actor, cast, kMouthA, 0x7e, 0xc6);
    const int32_t t = stepTick();
    if (t == 0x3c) {
        cast.animated = 1;
        actor.anim = 1;
        return;
    }
    if (t == 0x6f || t == 199) mouthClosed(actor, cast, kMouthA);
}
void script32Speaker2(AnimatedActor& actor, TalkingCastFields& cast) { // exe 0x497e30
    mouthWindow(actor, cast, kMouthA, 0x198, 0x1b8);
    mouthWindow(actor, cast, kMouthA, 0x1cc, 0x1ef);
    switch (stepTick()) {
    case 0x1b8:
    case 0x1ef: mouthClosed(actor, cast, kMouthA); return;
    case 0x202:
        actor.anim = 8;
        mouthOpened(actor, cast, kMouthA);
        return;
    case 0xd7: cue(actor, cast, 4, true); return;
    case 0x198: actor.anim = 5; return;
    default: return;
    }
}
void script32Speaker3(AnimatedActor& actor, TalkingCastFields& cast) { // exe 0x497ed0
    mouthWindow(actor, cast, kMouthA, 0xce, 0xd7);
    mouthWindow(actor, cast, kMouthA, 0xe8, 300);
    switch (stepTick()) {
    case 0x202:
        actor.anim = 6;
        mouthOpened(actor, cast, kMouthA);
        return;
    case 300:
    case 0xd7: mouthClosed(actor, cast, kMouthA); return;
    case 0xce: cue(actor, cast, 2, true); return;
    default: return;
    }
}
void script32Speaker4(AnimatedActor& actor, TalkingCastFields& cast) { // exe 0x497f60
    mouthWindow(actor, cast, kMouthA, 0x132, 400);
    switch (stepTick()) {
    case 0x132: cue(actor, cast, 3, true); return;
    case 400: mouthClosed(actor, cast, kMouthA); return;
    case 0x202:
        actor.anim = 7;
        mouthOpened(actor, cast, kMouthA);
        return;
    default: return;
    }
}
// exe 0x497db0: by the variant
void script32Run(AnimatedActor& actor, TalkingCastFields& cast) {
    switch (actor.variant) {
    case 0: script32Speaker0(actor, cast); return;
    case 1:
    case 5:
    case 6: return; // exe 0x52ef00
    case 2: script32Speaker2(actor, cast); return;
    case 3: script32Speaker3(actor, cast); return;
    case 4: script32Speaker4(actor, cast); return;
    default: notPortedAt(kStoryPort, "0x497be0 variant (table 0x892df4)", actor.variant);
    }
}
const DataTable<CollisionBoxRecord> script32CastBox{TableId::Ch2ScriptCastScript32CastBox};
// exe 0x497be0
void script32Cast(AnimatedActor& actor) {
    auto&& cast = classState<TalkingCastFields>(actor);
    actor.prevAnim = actor.anim;
    savePrevious(actor);
    switch (actor.state) {
    case 0: // exe 0x497c90
        if (!talkingInit(actor, stateAddress(TableId::Ch2ScriptCastScript32CastModels), stateAddress(TableId::Ch2ScriptCastScript32CastAnims), script32CastBox, false)) break;
        if (actor.variant == kLyingVariant) lyingInit(actor, cast);
        mouthClosed(actor, cast, kMouthA);
        script32Run(actor, cast);
        break;
    case 1: script32Run(actor, cast); break;
    default: notPortedAt(kStoryPort, "0x497be0 state (table 0x892ddc)", actor.state);
    }
    talkingTail(actor, cast);
}

// ---- 0x498160 variants [0x892e4c]: 0 / 2 / 3 / 4 by the step's sub-state (only sub-state 1 does something), 5
// 0x498630 --------------------------------------------------------------------------------------------------------------
void script37Speaker0(AnimatedActor& actor, TalkingCastFields& cast) { // exe 0x498360
    if (stepTick() == 0x3c) {
        cast.animated = 1;
        actor.anim = 1;
    }
    mouthWindow(actor, cast, kMouthB, 0x3c, 0x78);
}
void script37Speaker2(AnimatedActor& actor, TalkingCastFields& cast) { // exe 0x4983c0 (jump table 0x498458)
    switch (stepTick()) {
    case 0xdc: cue(actor, cast, 5, true); break;
    case 0x10f:
    case 0x18c: mouthClosed(actor, cast, kMouthB); break;
    case 0x1ae: actor.anim = 6; break;
    default: break;
    }
    mouthWindow(actor, cast, kMouthB, 0xdc, 0x10e);
    mouthWindow(actor, cast, kMouthB, 0x127, 0x18b);
}
void script37Speaker3(AnimatedActor& actor, TalkingCastFields& cast) { // exe 0x498550
    switch (stepTick()) {
    case 0x8c: cue(actor, cast, 2, true); break;
    case 0xc9: mouthOpened(actor, cast, kMouthB); break;
    case 0x1bc: actor.anim = 3; break;
    default: break;
    }
    mouthWindow(actor, cast, kMouthB, 0x8c, 200);
}
void script37Speaker4(AnimatedActor& actor, TalkingCastFields& cast) { // exe 0x4985e0
    if (stepTick() == 0x1ae) cue(actor, cast, 4, true);
    mouthWindow(actor, cast, kMouthB, 0x18b, 500);
}
// exe 0x498630: the lying body -- dimmed, pitched up; smoke every 8th tick where the 32-step pattern 0x15c200 has a bit
void lyingBody(AnimatedActor& actor, TalkingCastFields& cast) {
    actor.tint = kTintSpent;
    actor.rotation.x = -0x400;
    if ((cast.ticks & 7) == 0) {
        if (int32_t(0x15c200u << (cast.smokeStepByte & 0x1f)) < 0) {
            if (const uint32_t e = allocActor60(kActorStageClass)) { // exe 0x5e12f0
                StaticActor& smoke = staticActorAt(e);
                smoke.flags = kActorLive;
                smoke.type = kSmokeType;
                smoke.variant = kLyingSmokeVariant;
                smoke.loopFlags |= kActorSweepGroup2;
                smoke.pos.x = actor.pos.x;
                smoke.pos.y.whole = int16_t(uint16_t(actor.pos.y.whole) - 0x20u);
                smoke.pos.z = actor.pos.z;
                auto&& params = SmokeParams(actorHeaderAt(e));
                params.driftX = 0;
                params.driftZ = 0;
                params.life = 1;
            }
        }
        cast.smokeStep = uint16_t((cast.smokeStepByte + 1u) & 0x1fu);
    }
    cast.ticks += 1u;
}
// exe 0x498320: by the variant (0 / 2 / 3 / 4: by the step's sub-state [table + sub * 4], only sub-state 1 acts)
void script37Run(AnimatedActor& actor, TalkingCastFields& cast) {
    auto bySub = [&](uint32_t table, void (*speak)(AnimatedActor&, TalkingCastFields&)) {
        switch (stepSub()) {
        case 1: speak(actor, cast); return;
        case 0:
        case 2:
        case 3: return;
        default: notPortedAt(kStoryPort, "0x498160 sub-state table", table);
        }
    };
    switch (actor.variant) {
    case 0: bySub(stateAddress(TableId::Ch2ScriptCastScript37Speaker0Table), script37Speaker0); return; // exe 0x498340
    case 1:
    case 6:
    case 7: return; // exe 0x52ef00
    case 2: bySub(stateAddress(TableId::Ch2ScriptCastScript37Speaker2Table), script37Speaker2); return; // exe 0x4983a0
    case 3: bySub(stateAddress(TableId::Ch2ScriptCastScript37Speaker3Table), script37Speaker3); return; // exe 0x498530
    case 4: bySub(stateAddress(TableId::Ch2ScriptCastScript37Speaker4Table), script37Speaker4); return; // exe 0x4985c0
    case kLyingVariant: lyingBody(actor, cast); return;
    default: notPortedAt(kStoryPort, "0x498160 variant (table 0x892e4c)", actor.variant);
    }
}
const DataTable<CollisionBoxRecord> script37CastBox{TableId::Ch2ScriptCastScript37CastBox};
// exe 0x498160
void script37Cast(AnimatedActor& actor) {
    auto&& cast = classState<TalkingCastFields>(actor);
    actor.prevAnim = actor.anim;
    savePrevious(actor);
    switch (actor.state) {
    case 0: // exe 0x498210
        if (!talkingInit(actor, stateAddress(TableId::Ch2ScriptCastScript37CastModels), stateAddress(TableId::Ch2ScriptCastScript37CastAnims), script37CastBox, true)) break;
        if (actor.variant == kLyingVariant) lyingInit(actor, cast);
        else if (actor.variant < kLyingVariant) mouthClosed(actor, cast, kMouthB);
        script37Run(actor, cast);
        break;
    case 1: script37Run(actor, cast); break;
    default: notPortedAt(kStoryPort, "0x498160 state (table 0x892e34)", actor.state);
    }
    talkingTail(actor, cast);
}

// =====================================================================================================================
// ---- 0x4304d0: pool 0xa0 stage type 5 (ST18), spawned by script 0x37's step 0x5296b0 (content/nino/ch2_scripts: drawn,
// size 0x80): a 3-tick flash. States [0x88ca90]: 0 0x4304f0 16 random spikes (an angle (6 bits) | a length class (2
// bits) from the RNG, two steps each), 3 ticks; 1 0x430550 draws (0x430580) while its ticks count down, then freed
// (exe 0x5e1580).
// 0x430580: when the position << the stage shift is in the depth range 0x30 .. the effects' far distance * 4, not
// clipped and near the screen: two 0xf0 discs (radius (9 - ticks) * size >> 4, halved for the first), then DR_MODE
// 0xe1000220 and 16 POLY_G4 0x3a spikes in the camera plane at the view point (the GTE reloaded with the identity: exe
// 0x40c770 / 0x40c930 / 0x40ce40 + 0x40c6a0 / 0x40c860 / 0x40cb10): v0 (angle - 8, short), v1 (angle, long), v2 (angle,
// short), v3 (angle + 8, short); colours 0, 0, the half grey, 0. Port as content/late_game/late_cast's v6Draw:
// dc::otQuadG4 with camera-plane offsets.

// [0x88ca90]
enum class FlashState : uint8_t { Start = 0, Draw = 1 };
constexpr uint8_t kFlashTicks = 3;

// exe 0x430580
void drawFlash(const EffectActor& actor, const FlashFields& flash) {
    const uint32_t sh = stage().coordinateShift & 0x1f;
    const int16_t q[3] = {wrap(int(uint32_t(uint16_t(actor.pos.x)) << sh)), wrap(int(uint32_t(uint16_t(actor.pos.y)) << sh)),
                          wrap(int(uint32_t(uint16_t(actor.pos.z)) << sh))};
    const CameraPoint cp = cameraPoint(q);
    if (cp.view[2] > int32_t(uint32_t(stage().effectsFar) << 2)) return;
    if (cp.view[2] < 0x30) return;
    if (cp.clipped) return; // exe 0x40cae0 < 0
    if (uint16_t(cp.sx + 0x160) > 0x400) return;
    if (uint16_t(cp.sy + 0x88) > 0x400) return;
    const uint32_t life = flash.ticks;
    const uint32_t c = life * 0x40 + 0x3f;
    const int32_t radius = int32_t((9 - int32_t(life)) * int32_t(flash.size)) >> 4;
    const uint32_t col1 = (((c | 0xfffff000u) << 8) | c) << 8 | c;
    uint32_t col2 = c << 14;
    col2 &= 0xffff00ffu;
    col2 |= c * 0xc0;
    col2 &= 0xffffff00u;
    col2 |= (c * 0xc0) >> 8;
    col2 |= 0xf0000000u;
    const uint32_t xy = actor.positionWord(0), z = uint16_t(actor.pos.z) | 0x20000u;
    dc::emit({0x4000u, col1, xy, 0x22000000u, z, (uint32_t(radius) >> 1) << 16}, actor.at);
    dc::emit({0x4000u, col2, xy, 0x22000000u, z, uint32_t(radius) << 16}, actor.at);
    const uint32_t h = c >> 1;
    const uint32_t col3 = ((((h << 14) & 0xffff00ffu) | h * 0xc0) & 0xffffff00u) | ((h * 0xc0) >> 8);
    const uint32_t sc = (0xcu - stage().coordinateShift) & 0x1f;
    const uint32_t cw[4] = {0x3a000000u, 0, col3, 0};
    for (uint32_t i = 0; i < 0x10; ++i) {
        const uint32_t bb = flash.spikes[i];
        const uint32_t ang = bb & 0x3f, angB = (ang + 8) & 0x3f, angA = (ang - 8) & 0x3f;
        const int32_t len = int32_t(((bb >> 6) + 4) * (8 - life) * flash.size) >> 5;
        const int32_t shortLen = int32_t(uint32_t(len) >> 4);
        const float off[4][2] = {
            {float(wrap(shiftRight(fixedCos64(angA) * shortLen, sc))), float(wrap(shiftRight(fixedSin64(angA) * shortLen, sc)))},
            {float(wrap(shiftRight(fixedCos64(ang) * len, sc))), float(wrap(shiftRight(fixedSin64(ang) * len, sc)))},
            {float(wrap(shiftRight(fixedCos64(ang) * shortLen, sc))), float(wrap(shiftRight(fixedSin64(ang) * shortLen, sc)))},
            {float(wrap(shiftRight(fixedCos64(angB) * shortLen, sc))), float(wrap(shiftRight(fixedSin64(angB) * shortLen, sc)))}};
        const int16_t v[4][3] = {{q[0], q[1], q[2]}, {q[0], q[1], q[2]}, {q[0], q[1], q[2]}, {q[0], q[1], q[2]}};
        dc::otQuadG4(actor.at, cw, 1, v, off);
    }
}
// exe 0x4304d0
void scriptFlash(EffectActor& actor) {
    auto&& flash = classState<FlashFields>(actor);
    switch (FlashState(actor.state)) {
    case FlashState::Start: // exe 0x4304f0
        for (uint32_t i = 0; i < 0x10; ++i) {
            rngStep();
            const uint32_t r = rngStep() & 0xff;
            flash.spikes[i] = uint8_t(((r & 0x3f) | (r << 6)) & 0xff);
        }
        flash.ticks = kFlashTicks;
        actor.state += 1u;
        return;
    case FlashState::Draw: // exe 0x430550
        if (flash.ticks != 0) {
            drawFlash(actor, flash);
            flash.ticks -= 1u;
            return;
        }
        freeActor(actor);
        return;
    default: notPortedAt(kStoryPort, "0x4304d0 state (table 0x88ca90)", actor.state);
    }
}

} // namespace

void takeOffCastUpdate(const PlayerEnv&, uint32_t actor) { takeOffCast(animatedActorAt(actor)); }
void script32CastUpdate(const PlayerEnv&, uint32_t actor) { script32Cast(animatedActorAt(actor)); }
void script37CastUpdate(const PlayerEnv&, uint32_t actor) { script37Cast(animatedActorAt(actor)); }
void script37FlashUpdate(const PlayerEnv&, uint32_t actor) { scriptFlash(effectActorAt(actor)); }

} // namespace od::port
