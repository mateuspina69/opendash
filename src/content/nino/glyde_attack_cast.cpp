// The cast of the cutscenes of Glyde's attack on Nino (ST17 script 0x39, ST18 script 0x3b): pool 0x20 actors driven by
// the script's timeline (its step sub and step tick) and its first own byte (the cast's release). Models come from the
// script's alternate type-1 bank (bound as every class-0x20 actor). One routine per exe routine; the names are
// hypotheses from the observed behaviour.
//   0x498860  ST17 type 0x2d sub 2: the Birdbots of script 0x39 (variants 0..4, records 0x8a6ac4..0x8a6b50; blinking
//             part pairs on the step tick)
//   0x498bf0  ST18 type 0x2d sub 3: the Birdbots of script 0x3b (variants 0..3: hops / fly up + sparks)
//   0x4e34f0  ST17 type 0x5c (record 0x8a6b78 variant 1): a cast member that comes down and lands, timed animations and
//             texture cells
//   0x4e30f0  ST17 type 0x5b (record 0x8a6b8c variant 1): a cast member that comes down and lands, a blinking part pair
//   0x44acc0  ST17 type 2 (record 0x8a6ba0 variant 1): timed animations and texture cells; variant 1 rises away
// Render output left out as in the other ports: the shadows (exe 0x5f20f0; its ground probe is replicated).
#include "content/nino/glyde_attack_cast.hpp"
#include "content/nino/glyde_attack_cast_state.hpp"
#include "engine/data_source.hpp"
#include "game/loaded_data.hpp"
#include "game/actor_state.hpp"
#include "game/stage_smoke.hpp"

#include "game/actor_animation.hpp"
#include "game/actor_model.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_stats.hpp"
#include "game/cutscene_actor.hpp"
#include "game/distances.hpp"
#include "game/explosions.hpp"
#include "game/global_class_types.hpp"
#include "game/hit_effects.hpp"
#include "game/keyframe_tracks.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/script_state.hpp"
#include "game/shadows.hpp"
#include "game/stage_state.hpp"
#include "game/story.hpp"

using namespace od::port::glyde_attack_cast_state;

namespace od::port {

using namespace glyde_attack_cast_state;

namespace {

// the states of every class here: 0 init, 1 the script's cues
enum class CastState : uint8_t { Init = 0, Cue = 1 };

bool castReleased() { return scriptState().part<DescentScriptBytes>().released != 0; }

int32_t stepTick() { return scriptState().stepTick; }
uint8_t stepSub() { return scriptState().stepSub; }

// the copies at the top of the updates
void savePose(AnimatedActor& actor) {
    actor.prevAnim = actor.anim;
    actor.savePreviousPose();
    actor.savePreviousContact();
}
// the hit points of the type's stats row (0 when no row)
int16_t rowHitPoints(const AnimatedActor& actor) {
    return hasAnimatedStats(actor.type) ? int16_t(animatedStats(actor.type).hitPoints) : int16_t(0);
}
// the part `k` hidden / shown
void hidePart(AnimatedActor& actor, size_t k) { actor.parts[k].flags |= kPartHidden; }
void showPart(AnimatedActor& actor, size_t k) { actor.parts[k].flags &= uint8_t(~kPartHidden); }

// the models of the scene Birdbots show their body (part 2) or their wings (part 7) of parts 2 / 6 / 7 / 8
constexpr size_t kBodyPart = 2, kLeftPart = 6, kWingsPart = 7, kRightPart = 8;
void showBody(AnimatedActor& actor) {
    showPart(actor, kBodyPart);
    hidePart(actor, kLeftPart);
    hidePart(actor, kWingsPart);
    hidePart(actor, kRightPart);
}
void showWings(AnimatedActor& actor) {
    hidePart(actor, kBodyPart);
    hidePart(actor, kLeftPart);
    showPart(actor, kWingsPart);
    hidePart(actor, kRightPart);
}

// the blinking pair of a variant (exe data: two s16 part indices)
struct BlinkPair : DataRecord {
    uint16_t first = 0;
    uint16_t second = 0;
    static BlinkPair load(const TableReader& r) {
        BlinkPair rec;
        rec.first = r.u16(0x0);
        rec.second = r.u16(0x2);
        return rec;
    }
};
const DataTable<ScalarTable<uint8_t, 6>> script39BirdbotModels{TableId::GlydeAttackCastScript39BirdbotModels};
const DataTable<ScalarTable<uint8_t, 6>> script39BirdbotAnims{TableId::GlydeAttackCastScript39BirdbotAnims};
const DataTable<RecordTable<BlinkPair, 6, 4>> script39BirdbotPairs{TableId::GlydeAttackCastScript39BirdbotPairs};
const DataTable<CollisionBoxRecord> script39BirdbotBox{TableId::GlydeAttackCastScript39BirdbotBox};
constexpr uint8_t kScript39BlinkingVariant = 4, kScript39MarchingVariant = 5;

// exe 0x498ac0: the pair off (the first part hidden, the second shown)
void script39PairOff(AnimatedActor& actor, Script39BirdbotFields& fields) {
    const BlinkPair pair = script39BirdbotPairs[actor.variant];
    fields.pairFlipped = 0;
    hidePart(actor, pair.first);
    showPart(actor, pair.second);
}
// exe 0x498b60: the pair's hide bits swapped
void script39Blink(AnimatedActor& actor, Script39BirdbotFields& fields) {
    const BlinkPair pair = script39BirdbotPairs[actor.variant];
    if (fields.pairFlipped & 1) {
        showPart(actor, pair.first);
        hidePart(actor, pair.second);
    } else {
        hidePart(actor, pair.first);
        showPart(actor, pair.second);
    }
    fields.pairFlipped ^= 1u;
}
// exe 0x498b20: a blink every 3rd tick of the step ticks [lo, hi]
void script39BlinkBetween(AnimatedActor& actor, Script39BirdbotFields& fields, int32_t lo, int32_t hi) {
    const int32_t t = stepTick();
    if (t < lo || t > hi) return;
    if ((t - lo) % 3 == 0) script39Blink(actor, fields);
}
// exe 0x498a80 (variant 4)
void script39BlinkCues(AnimatedActor& actor, Script39BirdbotFields& fields) {
    script39BlinkBetween(actor, fields, 0xf, 0x3c);
    if (stepTick() == 0x3d) script39PairOff(actor, fields);
}
// exe 0x498a50: by the variant [0x892ee4] (7 entries)
void script39BirdbotCues(AnimatedActor& actor, Script39BirdbotFields& fields) {
    switch (actor.variant) {
    case 0:
    case 1: fields.stepping = 1; break; // exe 0x498a70
    case 2:
    case 3:
    case 5:
    case 6: break; // exe 0x52ef00 (ret)
    case kScript39BlinkingVariant: script39BlinkCues(actor, fields); break;
    default: notPortedAt(kStoryPort, "0x498860 variant (table 0x892ee4 has 7 entries)", actor.variant);
    }
}
// exe 0x498930
void script39BirdbotInit(AnimatedActor& actor, Script39BirdbotFields& fields) {
    if (actor.variant > 5)
        notPortedAt(kStoryPort, "0x498860 init variant (tables 0x892ed4 / 0x892f00 hold 6)", actor.variant);
    if (!bindActorModel(actor, script39BirdbotModels[actor.variant])) { // exe 0x5e0bc0
        freeActor(actor);                                              // exe 0x5e1490
        return;
    }
    actor.flags |= kActorDrawn;
    actor.anim = script39BirdbotAnims[actor.variant];
    actor.prevAnim = kNoAnimShown;
    const int16_t hp = rowHitPoints(actor);
    actor.rotation.z = 0;
    actor.rotation.x = 0;
    actor.interactionBox = nullptr;
    actor.interaction = 0;
    actor.airborne = 0;
    actor.onPlatform = 0;
    actor.maxHitPoints = hp;
    actor.hitPoints = hp;
    actor.collisionBox.pointAt(script39BirdbotBox.stateAddress());
    actor.clearClassBlock(); // exe 0x40dc40
    showBody(actor);
    script39PairOff(actor, fields);
    actor.enterState(uint8_t(CastState::Cue));
    if (actor.variant == kScript39MarchingVariant) {
        fields.stepping = 1;
        fields.startKey = 0x10;
        fields.slotPhase = uint16_t(uint32_t(actor.slot) * 8u - 0x100u);
    }
    script39BirdbotCues(actor, fields);
}
// exe 0x498860
void script39Birdbot(const PlayerEnv& env, AnimatedActor& actor) {
    Script39BirdbotFields& fields = classState<Script39BirdbotFields>(actor);
    savePose(actor);
    switch (CastState(actor.state)) { // [0x892ecc]
    case CastState::Init: script39BirdbotInit(actor, fields); break;
    case CastState::Cue: script39BirdbotCues(actor, fields); break;
    default: notPortedAt(kStoryPort, "0x498860 state (table 0x892ecc has 2 entries)", actor.state);
    }
    if (!(actor.flags & kActorLive)) return;
    if (actor.anim == actor.prevAnim) {
        if (fields.stepping != 0) animStepPose(actor); // exe 0x5e1e80
        else animDecodeCurrent(actor);                 // exe 0x5e1f30
    } else {
        animSetPose(actor, actor.anim, fields.startKey); // exe 0x5e1c30
        fields.startKey = 0;
    }
    if (actor.variant >= 2 && actor.variant <= 4) shadowGroundProbe(env, actor); // exe 0x5f20f0(a, 0x30, 0x100, 0)
}

// a yaw step of a hop's list (exe data)
struct YawStep : DataRecord {
    static constexpr uint32_t kBytes = 0x2;
    int16_t step = 0; // +0x0
    template <class R> static YawStep load(const R& r) {
        YawStep v;
        v.step = r.template read<int16_t>(0x0);
        return v;
    }
};
enum class Script3bVariant : uint8_t { Hop = 0, FlyLeft = 1, FlyRight = 2, Still = 3 };
// the hop (exe 0x498e20 by the sub-state)
enum class HopPhase : uint8_t { Jump = 0, Air = 1 };
// the fly (exe 0x498eb0 / 0x4990a0 by the sub-state [0x498f68 / 0x49915c])
enum class FlyPhase : uint8_t { Start = 0, Rise = 1, Turn = 2, Spark = 3, Sparks = 4 };
const DataTable<ScalarTable<uint8_t, 4>> script3bBirdbotModels{TableId::GlydeAttackCastScript3bBirdbotModels};
const DataTable<ScalarTable<uint8_t, 4>> script3bBirdbotAnims{TableId::GlydeAttackCastScript3bBirdbotAnims};
// the hop's yaw-step lists by the spawn's first parameter: 0x892f24 (-0x200 x4) / 0x892f30 (0x300 x4), 0-ended
// (exe data 0x892f3c: the lists' addresses, as the cast stores them: the address is the record's identity)
const DataTable<ScalarTable<uint32_t, 2>> hopYawStepLists{TableId::GlydeAttackCastHopYawStepLists};
const DataTable<CollisionBoxRecord> script3bBirdbotBox{TableId::GlydeAttackCastScript3bBirdbotBox};
constexpr uint8_t kSparkBone = 2, kSparkFrame = 9, kSparkAnim = 0xa, kSparksToBlast = 3;
constexpr uint8_t kSparkSmokeVariant = 6;
constexpr uint8_t kBlastVariant = 0x10;

// exe 0x499170: blocked -> the push-out's result
void script3bKeepOut(const PlayerEnv& env, AnimatedActor& actor) {
    if (int32_t(actorTestMove(env, actor)) < 0) actor.wallHit = actorPushOut(env, actor); // exe 0x40f570 / 0x410960
}
// exe 0x498f80: a hit spark at the bone 2's point (0x60 ahead); the third one also a blast (the global static type 7)
// and a smoke
void script3bSparks(AnimatedActor& actor) {
    const int16_t ahead[3] = {0, 0, -0x60};
    int16_t point[3];
    bonePointOffset(actor, kSparkBone, ahead, point); // exe 0x5e1970
    if (const uint32_t e = allocActorA0(0)) {         // exe 0x5e13b0
        EffectActor& spark = effectActorAt(e);
        spark.flags = kActorLiveDrawn;
        spark.loopFlags |= kActorSweepGroup2;
        spark.type = typeByte(GlobalEffectType::HitSpark);
        spark.variant = kSparkVariantHit;
        // the point's words (its spare lane: the exe's uninitialised stack short, 0 here)
        spark.setPositionWords(uint32_t(uint16_t(point[0])) | uint32_t(uint16_t(point[1])) << 16,
                               uint32_t(uint16_t(point[2])));
        classState<HitSparkFields>(effectActorAt(e)).size = 0x38;
    }
    actor.step += 1u;
    if (actor.step != kSparksToBlast) return;
    if (const uint32_t e = allocActor60(0)) { // exe 0x5e1300
        const uint32_t shift = stage().coordinateShift & 0x1fu;
        StaticActor& blast = staticActorAt(e);
        auto&& params = BlastParams(actorHeaderAt(e));
        blast.flags = kActorLive;
        blast.loopFlags |= kActorSweepGroup2;
        blast.type = typeByte(GlobalStaticType::Blast);
        blast.subtype = 0;
        blast.variant = kBlastVariant;
        blast.pos.x.whole = int16_t(uint16_t(uint32_t(uint16_t(point[0])) << shift));
        blast.pos.y.whole = int16_t(uint16_t(uint32_t(uint16_t(point[1])) << shift));
        params.size = 0x30;
        params.attack = 0;
        blast.pos.z.whole = int16_t(uint16_t(uint32_t(uint16_t(point[2])) << shift));
    }
    if (const uint32_t e = allocActor60(kActorStageClass)) { // exe 0x5e12f0
        StaticActor& smoke = staticActorAt(e);
        auto&& params = SmokeParams(actorHeaderAt(e));
        smoke.flags = kActorLive;
        smoke.loopFlags |= kActorSweepGroup2;
        smoke.type = kSmokeType;
        smoke.variant = kSparkSmokeVariant;
        smoke.pos.x.whole = point[0];
        smoke.pos.y.whole = point[1];
        smoke.pos.z.whole = point[2];
        params.driftX = 0;
        params.driftZ = 0;
        params.life = 0;
    }
}
// exe 0x498e20 (variant 0): a jump; on each landing the yaw turned by the list's next step (at its end, from the first)
void script3bHop(const PlayerEnv& env, AnimatedActor& actor, Script3bBirdbotFields& fields) {
    switch (HopPhase(actor.subState)) {
    case HopPhase::Jump:
        actor.velocity.y = -0x100;
        actor.airborne = 1;
        actor.subState = uint8_t(HopPhase::Air);
        [[fallthrough]];
    case HopPhase::Air: {
        actor.velocity.y += 0x40u;
        integrateVelocity(actor, 0, actor.velocity.y, -0x40); // exe 0x5e3690
        if (actor.airborne == 0) {
            uint32_t next = fields.nextYawStep;
            int16_t step = dataRecord<YawStep>(next, YawStep::kBytes).step;
            fields.nextYawStep = next + 2;
            if (step == 0) {
                next = fields.firstYawStep;
                step = dataRecord<YawStep>(next, YawStep::kBytes).step;
                fields.nextYawStep = next + 2;
            }
            actor.rotation.y = int16_t(uint16_t(actor.rotation.y) + uint16_t(step));
            actor.subState = uint8_t(HopPhase::Jump);
        }
        script3bKeepOut(env, actor);
        return;
    }
    default: script3bKeepOut(env, actor); return;
    }
}
// exe 0x498eb0 / 0x4990a0 (variants 1 / 2): rise `rise` ticks, turn `turn` ticks by `yawStep` a tick, then animation 0xa
// with its sparks at its frame 9
void script3bFly(AnimatedActor& actor, Script3bBirdbotFields& fields, uint8_t rise, uint8_t turn, uint16_t yawStep) {
    const uint8_t phase = actor.subState;
    switch (FlyPhase(phase)) {
    case FlyPhase::Start:
        fields.ticks = rise;
        actor.subState += 1u;
        [[fallthrough]];
    case FlyPhase::Rise:
        integrateVelocity(actor, 0, 0, -0xc0);
        fields.ticks -= 1u;
        if (fields.ticks != 0) return;
        fields.ticks = turn;
        actor.subState += 1u;
        return;
    case FlyPhase::Turn:
        actor.rotation.y = int16_t(uint16_t(actor.rotation.y) + yawStep);
        fields.ticks -= 1u;
        if (fields.ticks != 0) return;
        actor.subState += 1u;
        return;
    case FlyPhase::Spark:
        actor.anim = kSparkAnim;
        actor.subState = uint8_t(phase + 1u);
        actor.restartStep();
        return;
    case FlyPhase::Sparks:
        if (actor.animEnded()) actor.prevAnim = kNoAnimShown;
        if (actor.animKey.frame == kSparkFrame) script3bSparks(actor);
        return;
    default: return;
    }
}
// exe 0x498e00: by the variant [0x892f58]
void script3bBirdbotCues(const PlayerEnv& env, AnimatedActor& actor, Script3bBirdbotFields& fields) {
    switch (Script3bVariant(actor.variant)) {
    case Script3bVariant::Hop: script3bHop(env, actor, fields); break;
    case Script3bVariant::FlyLeft: script3bFly(actor, fields, 0x19, 4, 0x40); break;     // exe 0x498eb0
    case Script3bVariant::FlyRight: script3bFly(actor, fields, 0x15, 7, 0xffa0); break;  // exe 0x4990a0
    case Script3bVariant::Still: break;                                                   // exe 0x52ef00
    default: notPortedAt(kStoryPort, "0x498bf0 variant (table 0x892f58 has 4 entries)", actor.variant);
    }
}
// exe 0x498ca0
void script3bBirdbotInit(const PlayerEnv& env, AnimatedActor& actor, Script3bBirdbotFields& fields) {
    if (actor.variant > 3) notPortedAt(kStoryPort, "0x498bf0 init variant (table 0x892f4c holds 4)", actor.variant);
    if (!bindActorModel(actor, script3bBirdbotModels[actor.variant])) { // exe 0x5e0bc0
        freeActor(actor);                                              // exe 0x5e1490
        return;
    }
    actor.flags |= kActorDrawn;
    actor.anim = script3bBirdbotAnims[actor.variant];
    actor.prevAnim = kNoAnimShown;
    const int16_t hp = rowHitPoints(actor);
    actor.maxHitPoints = hp;
    actor.hitPoints = hp;
    actor.rotation.z = 0;
    actor.rotation.x = 0;
    actor.collisionBox.pointAt(script3bBirdbotBox.stateAddress());
    actor.interactionBox = nullptr;
    actor.interaction = 0;
    actor.airborne = 0;
    actor.onPlatform = 0;
    actor.clearClassBlock(); // exe 0x40dc40
    switch (Script3bVariant(actor.variant)) { // jump table 0x498de4
    case Script3bVariant::Hop:
    case Script3bVariant::Still: showBody(actor); break;
    default: showWings(actor); break;
    }
    actor.enterState(uint8_t(CastState::Cue));
    if (Script3bVariant(actor.variant) == Script3bVariant::Hop) {
        const uint32_t list = hopYawStepLists[size_t(int8_t(actor.params[0]))];
        fields.nextYawStep = list;
        fields.firstYawStep = list;
    }
    script3bBirdbotCues(env, actor, fields);
}
// exe 0x498bf0
void script3bBirdbot(const PlayerEnv& env, AnimatedActor& actor) {
    Script3bBirdbotFields& fields = classState<Script3bBirdbotFields>(actor);
    savePose(actor);
    switch (CastState(actor.state)) { // [0x892f44]
    case CastState::Init: script3bBirdbotInit(env, actor, fields); break;
    case CastState::Cue: script3bBirdbotCues(env, actor, fields); break;
    default: notPortedAt(kStoryPort, "0x498bf0 state (table 0x892f44 has 2 entries)", actor.state);
    }
    if (!(actor.flags & kActorLive)) return;
    if (actor.anim == actor.prevAnim) {
        animStepPose(actor); // exe 0x5e1e80
    } else {
        animSetPose(actor, actor.anim, fields.startKey); // exe 0x5e1c30
        fields.startKey = 0;
    }
    if (Script3bVariant(actor.variant) != Script3bVariant::Still)
        shadowGroundProbe(env, actor); // exe 0x5f20f0(a, 0x30, 0x100, 0)
}

// the common init of the three script 0x39 cast members below (after the model bound): animation `anim`, one hit
// point, no motion, the box
void castMemberFields(AnimatedActor& actor, uint8_t anim, uint32_t box) {
    actor.anim = anim;
    actor.prevAnim = kNoAnimShown;
    actor.maxHitPoints = 1;
    actor.hitPoints = 1;
    actor.velocity.clearWords();
    actor.spin.setXY(0, 0);
    actor.rotation.z = 0;
    actor.rotation.x = 0;
    actor.collisionBox.pointAt(box);
    actor.interactionBox = nullptr;
    actor.interaction = 0;
    actor.airborne = 0;
    actor.onPlatform = 0;
}
// the common head of the three: the neutral tint, the previous pose
void castMemberHead(AnimatedActor& actor) {
    actor.prevAnim = actor.anim;
    actor.tint = kTintNeutral;
    actor.savePreviousPose();
    actor.savePreviousContact();
}

// variant 0's phases (exe 0x4e36b0 by the step [0x4e37f0])
enum class DescentPhase : uint8_t { Wait = 0, Descend = 1, Land = 2, Settle = 3, Cells = 4 };
// variant 0 / 1's sub-states
enum class DescenderCue : uint8_t { First = 0, Second = 1, Third = 2 };
const DataTable<CollisionBoxRecord> descenderBox{TableId::GlydeAttackCastDescenderBox};

// exe 0x4e36b0 (variant 0, sub-state 0)
void descenderComeDown(AnimatedActor& actor, DescenderFields& fields) {
    switch (DescentPhase(actor.step)) {
    case DescentPhase::Wait:
        actor.anim = 1;
        fields.ticks = 0x9f;
        if (!castReleased()) {
            fields.frozen = 1;
            return;
        }
        fields.frozen = 0;
        actor.step += 1u;
        [[fallthrough]];
    case DescentPhase::Descend:
        integrateVelocity(actor, 0, 0, -0xf0); // exe 0x5e3690
        fields.ticks -= 1u;
        if (fields.ticks != 0) return;
        actor.anim = 2;
        actor.step += 1u;
        return;
    case DescentPhase::Land:
        if (int8_t(actor.keyIndex) < 0xb) integrateVelocity(actor, 0, 0, -0xf0);
        if (!actor.animEnded()) return;
        actor.anim = 3;
        actor.stepCounter = 0;
        actor.step += 1u;
        return;
    case DescentPhase::Settle:
        if (!actor.animEnded()) return;
        actor.stepCounter += 1u;
        if (actor.stepCounter != 5) return;
        actor.step += 1u;
        fields.frozen = 1;
        return;
    case DescentPhase::Cells: // the step tick - 0x1c2 over the byte table 0x4e3814 (0xb3 entries)
        switch (stepTick()) {
        case 0x1c2:
        case 0x23c: fields.tracks.first.select(2, 0); return; // exe 0x5e33d0
        case 0x234: fields.tracks.first.select(0, 0); return;
        case 0x274:
            fields.tracks.first.select(0, 0);
            actor.subState = uint8_t(DescenderCue::Second);
            actor.restartStep();
            return;
        default: return;
        }
    default: return;
    }
}
// exe 0x4e38f0 (variant 1, sub-state 0)
void descenderOpening(AnimatedActor& actor, DescenderFields& fields) {
    switch (stepTick()) {
    case 0:
        actor.anim = 0;
        [[fallthrough]];
    case 0x11d:
        actor.anim = 1;
        fields.tracks.first.select(1, 0);
        return;
    case 0x13b:
        fields.tracks.first.select(0, 0);
        actor.subState = uint8_t(DescenderCue::Second);
        actor.restartStep();
        return;
    default: return;
    }
}
// exe 0x4e3950 (variant 1, sub-state 1): from the step sub 5
void descenderTalk(AnimatedActor& actor, DescenderFields& fields) {
    if (stepSub() < 5) return;
    switch (stepTick()) {
    case 0x3e: actor.anim = 2; return;
    case 0x96: actor.anim = 3; return;
    case 0x146: actor.anim = 4; return;
    case 0x17c: fields.tracks.first.select(2, 0); return;
    case 0x18b: fields.tracks.first.select(0, 0); return;
    // the step tick - 0x1ae over the byte table 0x4e3a0c (0x6f entries)
    case 0x1ae:
    case 0x1d6:
        actor.anim = 5;
        fields.tracks.first.select(2, 0);
        return;
    case 0x1c2:
    case 0x21c: fields.tracks.first.select(0, 0); return;
    default: return;
    }
}
// exe 0x4e3670
void descenderCues(AnimatedActor& actor, DescenderFields& fields) {
    if (actor.variant == 0) {
        switch (DescenderCue(actor.subState)) { // exe 0x4e3690: [0x895f04]
        case DescenderCue::First: descenderComeDown(actor, fields); return;
        case DescenderCue::Second:
        case DescenderCue::Third: return; // exe 0x52ef00
        default: notPortedAt(kStoryPort, "0x4e34f0 variant 0 sub-state (table 0x895f04 has 3 entries)", actor.subState);
        }
    }
    switch (DescenderCue(actor.subState)) { // exe 0x4e38d0: [0x895f10]
    case DescenderCue::First: descenderOpening(actor, fields); return;
    case DescenderCue::Second: descenderTalk(actor, fields); return;
    default: notPortedAt(kStoryPort, "0x4e34f0 variant 1 sub-state (table 0x895f10 has 2 entries)", actor.subState);
    }
}
// exe 0x4e35c0
void descenderInit(AnimatedActor& actor, DescenderFields& fields) {
    if (!bindActorModel(actor, 0)) { // exe 0x5e0ba0
        freeActor(actor);           // exe 0x5e1490
        return;
    }
    castMemberFields(actor, actor.variant == 0 ? 1 : 0, descenderBox.stateAddress());
    descenderCues(actor, fields); // exe 0x4e3670 runs once before the block is cleared
    actor.clearClassBlock();      // exe 0x40dc40
    fields.tracks.init(stateAddress(TableId::GlydeAttackCastDescenderCells), 0); // exe 0x5e33a0
    fields.tracks.first.select(0, 0);       // exe 0x5e33d0
    actor.enterState(uint8_t(CastState::Cue));
}
// exe 0x4e34f0
void descender(const PlayerEnv& env, AnimatedActor& actor) {
    DescenderFields& fields = classState<DescenderFields>(actor);
    castMemberHead(actor);
    switch (CastState(actor.state)) { // [0x895efc]
    case CastState::Init: descenderInit(actor, fields); break;
    case CastState::Cue: descenderCues(actor, fields); break;
    default: notPortedAt(kStoryPort, "0x4e34f0 state (table 0x895efc has 2 entries)", actor.state);
    }
    if (!(actor.flags & kActorLive)) return;
    textureTracksTick(actor, fields.tracks); // exe 0x5e34f0
    if (actor.anim == actor.prevAnim) {
        if (fields.frozen == 0) animStepPose(actor); // exe 0x5e1e80
        else animDecodeCurrent(actor);               // exe 0x5e1f30
    } else {
        animSetPose(actor, actor.anim, 0); // exe 0x5e1c30
    }
    shadowGroundProbe(env, actor); // exe 0x5f20f0(a, 0x30, 0x160, 0)
}

enum class BlinkerPhase : uint8_t { Wait = 0, Descend = 1, Land = 2, Blink = 3 };
enum class BlinkerCue : uint8_t { First = 0, Second = 1, Third = 2 };
enum class BlinkerWindow : uint8_t { Wait = 0, Blink = 1 };
const DataTable<CollisionBoxRecord> blinkerBox{TableId::GlydeAttackCastBlinkerBox};
constexpr size_t kBlinkerShownPart = 17, kBlinkerHiddenPart = 1;
constexpr uint8_t kBlinkerWindowSub = 4;

// exe 0x4e34a0: the phase flipped; set -> part 17 shown / part 1 hidden, clear -> the reverse
void blinkerBlink(AnimatedActor& actor, BlinkerFields& fields) {
    fields.flipped ^= 1u;
    if (fields.flipped != 0) {
        showPart(actor, kBlinkerShownPart);
        hidePart(actor, kBlinkerHiddenPart);
    } else {
        hidePart(actor, kBlinkerShownPart);
        showPart(actor, kBlinkerHiddenPart);
    }
}
// one blink tick: a blink every 3rd tick of the count
void blinkerTick(AnimatedActor& actor, BlinkerFields& fields) {
    if (fields.ticks % 3 == 0) blinkerBlink(actor, fields);
    fields.ticks += 1u;
}
// exe 0x4e32c0 (variant 0, sub-state 0): by the step [0x4e3418]
void blinkerComeDown(AnimatedActor& actor, BlinkerFields& fields) {
    switch (BlinkerPhase(actor.step)) {
    case BlinkerPhase::Wait:
        actor.anim = 1;
        fields.ticks = 0x88;
        fields.blinking = 0;
        if (!castReleased()) {
            fields.frozen = 1;
            return;
        }
        fields.frozen = 0;
        actor.step += 1u;
        [[fallthrough]];
    case BlinkerPhase::Descend:
        integrateVelocity(actor, 0, 0, -0xf0); // exe 0x5e3690
        fields.ticks -= 1u;
        if (fields.ticks != 0) return;
        actor.anim = 2;
        actor.step += 1u;
        return;
    case BlinkerPhase::Land:
        if (int8_t(actor.keyIndex) < 0xe) integrateVelocity(actor, 0, 0, -0xf0);
        if (!actor.animEnded()) return;
        fields.ticks = 0;
        fields.blinking = 1;
        actor.step += 1u;
        return;
    case BlinkerPhase::Blink:
        // the blink windows [0x9c, 0xf2) / [0x100, 0x188) / [0x198, 0x1b0); past `cmp eax, 0x100; jg 0x4e33e2` the
        // register still holds the tick: sub 0x188 / 0x10 / 0x18 are the cases 0x188 / 0x198 / 0x1b0
        switch (stepTick()) {
        case 0x80:
        case 0xf2:
        case 0x188: fields.blinking = 0; break;
        case 0x9c:
        case 0x100:
        case 0x198: fields.blinking = 1; break;
        case 0x1b0:
            fields.blinking = 0;
            actor.step += 1u;
            break;
        default: break;
        }
        if (fields.blinking == 0) {
            fields.ticks = 0;
            return;
        }
        blinkerTick(actor, fields);
        return;
    default: return;
    }
}
// exe 0x4e3430 (the other variants): the pair blinks from the step tick 0x19 to 0xfa of the step sub 4
void blinkerWindow(AnimatedActor& actor, BlinkerFields& fields) {
    if (stepSub() != kBlinkerWindowSub) return;
    switch (BlinkerWindow(actor.step)) {
    case BlinkerWindow::Wait:
        if (stepTick() != 0x19) return;
        fields.ticks = 0;
        actor.step += 1u;
        return;
    case BlinkerWindow::Blink:
        blinkerTick(actor, fields);
        if (stepTick() == 0xfa) actor.step += 1u;
        return;
    default: return;
    }
}
// exe 0x4e3280
void blinkerCues(AnimatedActor& actor, BlinkerFields& fields) {
    if (actor.variant != 0) {
        blinkerWindow(actor, fields);
        return;
    }
    switch (BlinkerCue(actor.subState)) { // exe 0x4e32a0: [0x895e8c]
    case BlinkerCue::First: blinkerComeDown(actor, fields); return;
    case BlinkerCue::Second:
    case BlinkerCue::Third: return; // exe 0x52ef00
    default: notPortedAt(kStoryPort, "0x4e30f0 variant 0 sub-state (table 0x895e8c has 3 entries)", actor.subState);
    }
}
// exe 0x4e31d0
void blinkerInit(AnimatedActor& actor, BlinkerFields& fields) {
    if (!bindActorModel(actor, 0)) { // exe 0x5e0ba0
        freeActor(actor);           // exe 0x5e1490
        return;
    }
    castMemberFields(actor, actor.variant == 0 ? 1 : 0, blinkerBox.stateAddress());
    hidePart(actor, kBlinkerShownPart);
    showPart(actor, kBlinkerHiddenPart);
    blinkerCues(actor, fields); // exe 0x4e3280 runs once before the block is cleared
    actor.clearClassBlock();    // exe 0x40dc40
    actor.enterState(uint8_t(CastState::Cue));
}
// exe 0x4e30f0
void blinker(const PlayerEnv& env, AnimatedActor& actor) {
    BlinkerFields& fields = classState<BlinkerFields>(actor);
    castMemberHead(actor);
    switch (CastState(actor.state)) { // [0x895e84]
    case CastState::Init: blinkerInit(actor, fields); break;
    case CastState::Cue: blinkerCues(actor, fields); break;
    default: notPortedAt(kStoryPort, "0x4e30f0 state (table 0x895e84 has 2 entries)", actor.state);
    }
    if (!(actor.flags & kActorLive)) return;
    if (actor.anim == actor.prevAnim) {
        if (fields.frozen == 0) animStepPose(actor); // exe 0x5e1e80
        else animDecodeCurrent(actor);               // exe 0x5e1f30
    } else {
        animSetPose(actor, actor.anim, 0); // exe 0x5e1c30
    }
    shadowGroundProbe(env, actor); // exe 0x5f20f0(a, 0x30, 0x160, 0)
}

enum class RiserCue : uint8_t { Wait = 0, Watch = 1, React = 2 };
enum class RisePhase : uint8_t { Gestures = 0, Lift = 1, Rise = 2, Gone = 3 };
const DataTable<CollisionBoxRecord> riserBox{TableId::GlydeAttackCastRiserBox};
constexpr uint32_t kRiseAwayDistance = 0x7d0;

// exe 0x44aea0 (variant 0, sub-state 0)
void riserWait(AnimatedActor& actor) {
    actor.anim = 4;
    actor.prevAnim = kNoAnimShown;
    if (stepSub() != 1) return;
    actor.subState = uint8_t(RiserCue::Watch);
    actor.restartStep();
}
// exe 0x44aed0 (variant 0, sub-state 1)
void riserWatch(AnimatedActor& actor) {
    const int32_t t = stepTick();
    if (t < 0) return;
    if (t <= 1) {
        actor.anim = 4;
        actor.prevAnim = kNoAnimShown;
    } else if (t == 0x1c4) {
        actor.anim = 5;
        actor.subState = uint8_t(RiserCue::React);
        actor.restartStep();
    }
}
// exe 0x44af10 (variant 0, sub-state 2)
void riserReact(AnimatedActor& actor, RiserFields& fields) {
    if (stepSub() != 2) return;
    switch (stepTick()) {
    case 0: actor.anim = 6; return;
    case 0x12:
        fields.tracks.first.select(2, 0);  // exe 0x5e33d0
        fields.tracks.second.select(2, 0); // exe 0x5e33f0
        return;
    case 0x64:
    case 0x94: fields.tracks.second.select(3, 0); return;
    case 0x70: fields.tracks.second.select(2, 0); return;
    case 0x12a: fields.tracks.first.select(2, 0); return;
    default: return;
    }
}
// exe 0x44afc0 (the other variants): from the step sub 5
void riserRiseAway(AnimatedActor& actor, RiserFields& fields) {
    if (stepSub() < 5) return;
    switch (RisePhase(actor.step)) {
    case RisePhase::Gestures:
        switch (stepTick()) {
        case 0x3c: actor.anim = 1; return;
        case 0x104: actor.anim = 2; return;
        case 0x136:
            actor.anim = 3;
            actor.enterStep(uint8_t(RisePhase::Lift));
            return;
        default: return;
        }
    case RisePhase::Lift:
        if (int8_t(actor.keyIndex) > 0xf) integrateVelocity(actor, 0, 0, 0x140); // exe 0x5e3690
        if (!actor.animEnded()) return;
        actor.rotation.y -= 0x10u;
        actor.anim = 4;
        actor.enterStep(uint8_t(RisePhase::Rise));
        fields.riseTicks = 0x400;
        return;
    case RisePhase::Rise: {
        actor.rotation.y -= 0x10u;
        fields.riseTicks -= 0x10u;
        integrateVelocity(actor, 0, 0, 0x140);
        const uint32_t d = uint32_t(distance3(actor.pos, player().pos)); // exe 0x5e3a60
        if (d <= kRiseAwayDistance && fields.riseTicks > 0) return;
        actor.enterStep(uint8_t(RisePhase::Gone));
        return;
    }
    default: return;
    }
}
// exe 0x44ae60
void riserCues(AnimatedActor& actor, RiserFields& fields) {
    if (actor.variant == 0) {
        switch (RiserCue(actor.subState)) { // exe 0x44ae80: [0x88e540]
        case RiserCue::Wait: riserWait(actor); return;
        case RiserCue::Watch: riserWatch(actor); return;
        case RiserCue::React: riserReact(actor, fields); return;
        default: notPortedAt(kStoryPort, "0x44acc0 variant 0 sub-state (table 0x88e540 has 3 entries)", actor.subState);
        }
    }
    if (actor.subState != 0)
        notPortedAt(kStoryPort, "0x44acc0 variant 1 sub-state (table 0x88e54c has 1 entry)", actor.subState);
    riserRiseAway(actor, fields); // exe 0x44afa0: [0x88e54c]
}
// exe 0x44ad80
void riserInit(AnimatedActor& actor, RiserFields& fields) {
    if (!bindActorModel(actor, 0)) { // exe 0x5e0ba0
        freeActor(actor);           // exe 0x5e1490
        return;
    }
    actor.flags |= kActorDrawn;
    castMemberFields(actor, actor.variant == 0 ? 4 : 0, riserBox.stateAddress());
    actor.clearClassBlock(); // exe 0x40dc40
    actor.enterState(uint8_t(CastState::Cue));
    fields.tracks.init(stateAddress(TableId::GlydeAttackCastRiserCells), stateAddress(TableId::GlydeAttackCastRiserSecondCells)); // exe 0x5e33a0
    const uint8_t seq = actor.variant == 0 ? 1 : 0;
    fields.tracks.first.select(seq, 0);  // exe 0x5e33d0
    fields.tracks.second.select(seq, 0); // exe 0x5e33f0
}
// exe 0x44acc0
void riser(const PlayerEnv& env, AnimatedActor& actor) {
    RiserFields& fields = classState<RiserFields>(actor);
    castMemberHead(actor);
    switch (CastState(actor.state)) { // [0x88e538]
    case CastState::Init: riserInit(actor, fields); break;
    case CastState::Cue: riserCues(actor, fields); break;
    default: notPortedAt(kStoryPort, "0x44acc0 state (table 0x88e538 has 2 entries)", actor.state);
    }
    if (!(actor.flags & kActorLive)) return;
    textureTracksTick(actor, fields.tracks); // exe 0x5e34f0
    if (actor.anim == actor.prevAnim) animStepPose(actor); // exe 0x5e1e80
    else animSetPose(actor, actor.anim, 0);               // exe 0x5e1c30
    shadowGroundProbe(env, actor); // exe 0x5f20f0(a, 0x30, 0x160, 0)
}

} // namespace

void script39BirdbotUpdate(const PlayerEnv& env, uint32_t actor) { script39Birdbot(env, animatedActorAt(actor)); }
void script3bBirdbotUpdate(const PlayerEnv& env, uint32_t actor) { script3bBirdbot(env, animatedActorAt(actor)); }
void script39DescenderUpdate(const PlayerEnv& env, uint32_t actor) { descender(env, animatedActorAt(actor)); }
void script39BlinkerUpdate(const PlayerEnv& env, uint32_t actor) { blinker(env, animatedActorAt(actor)); }
void script39RiserUpdate(const PlayerEnv& env, uint32_t actor) { riser(env, animatedActorAt(actor)); }

} // namespace od::port
