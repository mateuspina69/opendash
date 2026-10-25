// ST3C (the pirate base, story 12): a cast member of the base's script (pool 0x20 type 0x72 sub 0, exe 0x4f4740;
// hypothesis names), one routine per exe routine. See pirate_base_cast.hpp.
//
// Its acts follow the script's timeline (the step tick) and move on with the script's step sub-id; its two texture
// tracks (the face) are the cast's (game/cutscene_actor CastFields), its held pose (kCastHoldPose) decodes the current
// key each tick instead of stepping it.
#include "content/saul_kada/pirate_base_cast.hpp"

#include "game/actor_animation.hpp"
#include "game/actor_model.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/cutscene_actor.hpp"
#include "game/vectors.hpp"
#include "game/game_state.hpp"
#include "game/keyframe_tracks.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/script_state.hpp"
#include "game/spark_emitter.hpp"
#include "game/story.hpp"

namespace od::port {

namespace {

// [0x89710c]
enum class CastState : uint8_t { Bind = 0, Run = 1 };
// variant 0's acts [0x897114]
enum class CaptainAct : uint8_t { Wait = 0, First = 1, Second = 2, Third = 3, Done = 4 };
// variant 1's acts [0x897128]
enum class GuardAct : uint8_t { First = 0, Second = 1 };
enum class CastVariant : uint8_t { Captain = 0, Guard = 1 };

constexpr uint8_t kSparkVariant = 7;
constexpr uint32_t kSparkPattern = 0x15a200; // the RNG's low bits that drop a spark (bit 31 - (r & 31))

void eyes(CastFields& cast, uint8_t sequence) { cast.faceTracks.first.select(sequence, 0); }  // exe 0x5e33d0
void mouth(CastFields& cast, uint8_t sequence) { cast.faceTracks.second.select(sequence, 0); } // exe 0x5e33f0
void nextAct(AnimatedActor& actor, uint8_t act) {
    actor.subState = act;
    actor.restartStep();
}

// exe 0x4f4900: the step sub-id 1 -> the first act
void waitForCue(AnimatedActor& actor) {
    if (scriptState().stepSub == 1) nextAct(actor, uint8_t(CaptainAct::First));
}
// exe 0x4f4920: the first act's timeline (the step sub-id 2 -> the second act)
void firstAct(AnimatedActor& actor, CastFields& cast) {
    switch (scriptState().stepTick) {
    case 0x6e: actor.anim = 1; mouth(cast, 1); break;
    case 0x91: case 0xfa: mouth(cast, 0); break;
    case 0xa0: mouth(cast, 1); break;
    case 0x109: actor.anim = 2; mouth(cast, 1); break;
    case 0x14a: case 0x18d: mouth(cast, 0); break;
    case 0x15d: actor.anim = 3; mouth(cast, 1); break;
    case 0x19c: actor.anim = 4; break;
    case 0x22d: actor.anim = 5; break;
    case 0x27b: eyes(cast, 1); mouth(cast, 6); break;
    case 0x29e: mouth(cast, 5); break;
    case 0x2a8: mouth(cast, 6); break;
    case 0x2e1: mouth(cast, 0); break;
    case 0x327: actor.anim = 6; eyes(cast, 3); break;
    case 0x354: mouth(cast, 1); break;
    case 0x3a2: mouth(cast, 0); break;
    case 0x3ac: mouth(cast, 1); break;
    case 0x3e0: mouth(cast, 0); break;
    case 0x3ef: eyes(cast, 3); mouth(cast, 1); break;
    case 0x442: case 0x4b7: mouth(cast, 0); break;
    case 0x453: actor.anim = 7; mouth(cast, 1); break;
    case 0x4d5: eyes(cast, 1); mouth(cast, 1); actor.anim = 8; break;
    case 0x528: case 0x5bf: mouth(cast, 0); break;
    case 0x532: actor.anim = 9; mouth(cast, 1); break;
    case 0x60a: eyes(cast, 2); actor.anim = 0xa; break;
    case 0x640: actor.anim = 0xb; mouth(cast, 3); break;
    case 0x65e: case 0x690: mouth(cast, 2); break;
    case 0x668: actor.anim = 0xc; mouth(cast, 3); break;
    case 0x6c1: actor.anim = 0xd; eyes(cast, 1); mouth(cast, 1); break; // jumps into 0x4f4aaa
    case 0x6f4: case 0x759: mouth(cast, 0); break;
    case 0x6fd: actor.anim = 0xe; break;
    case 0x6fe: mouth(cast, 1); break;
    default: break;
    }
    if (scriptState().stepSub == 2) nextAct(actor, uint8_t(CaptainAct::Second));
}
// exe 0x4f5020: the second act's timeline (the step sub-id 3 -> the third act, the pose held)
void secondAct(AnimatedActor& actor, CastFields& cast) {
    switch (scriptState().stepTick) {
    case 0x1e: actor.anim = 0xf; mouth(cast, 0); break;
    case 0x3c: actor.anim = 0x10; eyes(cast, 4); mouth(cast, 5); break;
    case 0x96: case 0xc8: mouth(cast, 4); break;
    case 0x9e: mouth(cast, 5); break;
    case 0xdc: actor.anim = 0x11; break;
    case 0x1fe: actor.anim = 0x12; eyes(cast, 2); mouth(cast, 5); break; // -> 0x4f5086
    case 0x226: case 0x273: mouth(cast, 4); break;
    case 0x230: mouth(cast, 5); break;
    case 0x2db: actor.anim = 0x13; eyes(cast, 4); mouth(cast, 5); break; // -> 0x4f5082
    case 0x335: actor.anim = 0x14; break;
    case 0x346: case 0x391: case 0x3f7: case 0x428: mouth(cast, 4); break;
    case 0x34d: case 0x404: mouth(cast, 5); break;
    case 0x3a5: actor.anim = 0x15; break;
    case 0x3f2: actor.anim = 0x16; eyes(cast, 5); mouth(cast, 5); break; // -> 0x4f5086
    default: break;
    }
    if (scriptState().stepSub == 3) {
        nextAct(actor, uint8_t(CaptainAct::Third));
        actor.anim = 0x17;
        cast.castFlags = kCastHoldPose;
    }
}
// exe 0x4f53a0: the third act's timeline (the step sub-id 4 -> done)
void thirdAct(AnimatedActor& actor, CastFields& cast) {
    switch (scriptState().stepTick) {
    case 0xa: actor.anim = 0x17; cast.castFlags = 0; break;
    case 0x69: actor.anim = 0x18; break;
    case 0x19: case 0x50: mouth(cast, 1); break;
    case 0x41: case 0xb9: mouth(cast, 0); break;
    default: break;
    }
    if (scriptState().stepSub == 4) {
        mouth(cast, 4);
        nextAct(actor, uint8_t(CaptainAct::Done));
    }
}

// exe 0x4f5730(actor, bone): every 16th tick one RNG step; its bit in the spark pattern set -> a spark emitter (the
// stage's static type 3 variant 7) at the bone's point, 8 units up
void boneSpark(const AnimatedActor& actor, uint32_t bone) {
    if (gameState().frame & 0xf) return;
    const uint32_t r = rngStep();
    if (int32_t(kSparkPattern << (r & 0x1f)) >= 0) return;
    const uint32_t slot = allocActor60(kActorStageClass);
    if (!slot) return;
    StaticActor& spark = staticActorAt(slot);
    SparkEmitterParams& params = classState<SparkEmitterParams>(staticActorAt(slot));
    spark.flags = kActorLive;
    spark.type = kSparkEmitterStageType;
    spark.variant = kSparkVariant;
    spark.loopFlags |= kActorSweepGroup2;
    const ShortVec4& point = bonePoint(actor, bone, kStoryPort); // exe 0x5e1810 (no bone block: a port gap)
    spark.pos.x = int32_t(uint32_t(int32_t(point.x)) << 16);
    spark.pos.y.whole = int16_t(uint16_t(point.y) - 8);
    spark.pos.z = int32_t(uint32_t(int32_t(point.z)) << 16);
    params.driftX = 0;
    params.driftZ = 0;
    params.lifetime = 1;
}
// exe 0x4f5500: once the script has started (its first byte; latched in the step), the first act's timeline; the
// step sub-id 1 -> the second act; sparks at bones 3 and 6
void guardFirstAct(AnimatedActor& actor, CastFields& cast) {
    if (actor.step == 0) {
        if (scriptState().part<ScriptCastStart>().started == 0) return;
        actor.step = 1;
    }
    switch (scriptState().stepTick) {
    case 0x1e: mouth(cast, 5); actor.anim = 0x1a; break;
    case 0x50: case 0xb9: case 0x122: case 0x361: case 0x3bb: mouth(cast, 5); break;
    case 0x32: case 0xaa: case 0x104: case 0x181: case 0x348: case 0x3ac: case 0x401: mouth(cast, 4); break;
    case 0x2e9:
        actor.anim = 0x1a;
        actor.prevAnim = kNoAnimShown;
        eyes(cast, 2);
        mouth(cast, 5);
        break;
    default: break;
    }
    if (scriptState().stepSub == 1) nextAct(actor, uint8_t(GuardAct::Second));
    boneSpark(actor, 3);
    boneSpark(actor, 6);
}
// exe 0x4f5620
void guardSecondAct(AnimatedActor& actor, CastFields& cast) {
    switch (scriptState().stepTick) {
    case 0x1d: actor.anim = 0x1b; eyes(cast, 5); mouth(cast, 5); break;
    case 0x46: case 0xae: mouth(cast, 4); break;
    case 0x6e: mouth(cast, 5); break;
    default: break;
    }
}

// exe 0x4f4800
void bind(AnimatedActor& actor, CastFields& cast) {
    if (!bindActorModel(actor, 0)) { // exe 0x5e0ba0
        freeActor(actor);            // exe 0x5e1490
        return;
    }
    actor.maxHitPoints = 1;
    actor.hitPoints = 1;
    actor.velocity.clearWords();
    actor.collisionBox.pointAt(TableId::PirateBaseCastBox);
    actor.interactionBox = nullptr;
    actor.interaction = 0;
    actor.airborne = 0;
    actor.onPlatform = 0;
    actor.clearClassBlock();                            // exe 0x40dc40
    cast.faceTracks.init(stateAddress(TableId::PirateBaseCastEyesTable), stateAddress(TableId::PirateBaseCastMouthTable)); // exe 0x5e33a0
    switch (CastVariant(actor.variant)) {
    case CastVariant::Captain:
        actor.anim = 0;
        eyes(cast, 0);
        mouth(cast, 0);
        break;
    case CastVariant::Guard:
        actor.anim = 0x19;
        eyes(cast, 1);
        mouth(cast, 4);
        break;
    default: break;
    }
    actor.prevAnim = kNoAnimShown;
    actor.enterState(uint8_t(CastState::Run));
}
// exe 0x4f48c0: variant 0 -> 0x4f48e0 [0x897114], else 0x4f54e0 [0x897128]
void run(AnimatedActor& actor, CastFields& cast) {
    if (CastVariant(actor.variant) == CastVariant::Captain) {
        switch (CaptainAct(actor.subState)) {
        case CaptainAct::Wait: waitForCue(actor); return;
        case CaptainAct::First: firstAct(actor, cast); return;
        case CaptainAct::Second: secondAct(actor, cast); return;
        case CaptainAct::Third: thirdAct(actor, cast); return;
        case CaptainAct::Done: return; // 0x52ef00
        default: notPortedAt(kStoryPort, "0x4f48e0 sub-state (table 0x897114)", actor.subState);
        }
    }
    switch (GuardAct(actor.subState)) {
    case GuardAct::First: guardFirstAct(actor, cast); return;
    case GuardAct::Second: guardSecondAct(actor, cast); return;
    default: notPortedAt(kStoryPort, "0x4f54e0 sub-state (table 0x897128)", actor.subState);
    }
}

// exe 0x4f4740
void pirateBaseCast(AnimatedActor& actor) {
    CastFields& cast = classState<CastFields>(actor);
    actor.prevAnim = actor.anim;
    actor.savePreviousPose();
    actor.savePreviousContact();
    switch (CastState(actor.state)) { // [0x89710c]
    case CastState::Bind: bind(actor, cast); break;
    case CastState::Run: run(actor, cast); break;
    default: notPortedAt(kStoryPort, "0x4f4740 state (table 0x89710c)", actor.state);
    }
    if (!(actor.flags & kActorLive)) return;
    textureTracksTick(actor, cast.faceTracks); // exe 0x5e34f0
    if (actor.anim == actor.prevAnim) {
        if (cast.castFlags == 0) animStepPose(actor); // exe 0x5e1e80
        else animDecodeCurrent(actor);                // exe 0x5e1f30
    } else {
        animSetPose(actor, actor.anim, 0); // exe 0x5e1c30
    }
    // drawn: the shadow exe 0x5f20f0(actor, 0x60, 0x160, 0) -- render output
}

} // namespace

void pirateBaseCastUpdate(const PlayerEnv&, uint32_t actor) { pirateBaseCast(animatedActorAt(actor)); }

} // namespace od::port
