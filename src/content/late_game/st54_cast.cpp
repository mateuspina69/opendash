// ST54's effect-spawning mover (pool 0x20 type 0x85, exe 0x50e190; hypothesis names).
//
// States [0x8984b8]: 0 bind 0x50e230, 1 run 0x50e2d0 (the kind (its first parameter, copied into the sub-state) ->
// [0x8984c4]: 0x50e310 / 0x50e750), 2 freed 0x468fa0, 3 / 4 = 0x50e310 / 0x50e750 directly (the table runs into the
// kind table 0x8984c4).
// Kind 0, the rising ring (0x50e310, its step set from outside): it counts its ticks. Step 0: still (velocity y 0,
//   braking 2), every 7 ticks a stage type-3 variant-0xa effect around its feet. Step 1: moves by its velocity y
//   (braked down to -0x80), every 9 ticks a variant-0x1f effect at (-0xc7.., -0x300, -0x117..) and four variant-0x23
//   effects on a ring (radius table 0x8984cc by its ring index, which grows to 0x11). Step 2: braking 0x10, speeds up
//   to -0x140, while its ticks are below 0x384 every 13 ticks a ring of four (below 0xc8) or six variant-0x24 effects.
// Kind 1, the take-off (0x50e750, its step set from outside except 1 -> 2 -> 3): step 0 moves by (0, -0x140, 0) along
//   its rotation, once spawns the four stage static type-0x67 emitters (variants 0 / 2 / 1 / 3, owner = this; the first
//   two kept). Step 1 keeps moving for 0x14 ticks (ticks 0x64 -> 0x50), hit sparks every 4 frames. Step 2: the first
//   tick a blast (size 0x300), the position frozen, hidden, the two kept emitters drained (their sub-state 2); then
//   blasts every 8 frames, sparks every 8 ticks, and (ticks below 0x50) a type-3 variant-0x21 effect every tick; after
//   0x64 ticks step 3: a variant-0x21 effect every 16 frames.
// Drawn in kind 1, or in kind 0 from step 1: two draw commands 0x50ed70 / 0x50ee00 at its position.
#include "content/late_game/st54_cast.hpp"
#include "content/late_game/st54_cast_state.hpp"

#include "engine/data_source.hpp"
#include "game/actor_animation.hpp"
#include "game/actor_model.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/draw_commands.hpp"
#include "game/explosions.hpp"
#include "game/field_alias.hpp"
#include "game/game_state.hpp"
#include "game/global_class_types.hpp"
#include "game/hit_effects.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/story.hpp"

namespace od::port {

using namespace st54_cast_state;

namespace {

enum class MoverState : uint8_t { Bind = 0, Run = 1, Free = 2, RisingRingDirect = 3, TakeoffDirect = 4 };
enum class MoverKind : uint8_t { RisingRing = 0, Takeoff = 1 };
enum class RingStep : uint8_t { Still = 0, Rise = 1, Speed = 2 };
enum class TakeoffStep : uint8_t { Climb = 0, Leave = 1, Burst = 2, Smoulder = 3 };
constexpr uint8_t kStageEffectType = 3, kEmitterType = 0x67;
constexpr uint8_t kFeetPuff = 0xa, kRisePuff = 0x1f, kRisingRing = 0x23, kSpeedRing = 0x24, kSmoke = 0x21;
constexpr uint8_t kLastRingIndex = 0x10;
constexpr uint8_t kEmitterDrain = 2; // the emitters' phase that frees them once their puffs are gone

// the spawn parameters of the stage's type-3 effects (their class is not ported here): two bytes and a word
struct StageEffectParams {
    explicit StageEffectParams(const StaticActor& actor) : StageEffectParams(const_cast<StaticActor&>(actor)) {}
    explicit StageEffectParams(StaticActor& actor)
        : paramA(actor.params[0]), paramB(actor.params[1]), paramWord(actor.params, 2) {}
    uint8_t& paramA;
    uint8_t& paramB;
    BytesField<uint16_t, 4> paramWord;
};
// the emitters' spawn parameters (two bytes and a word, then the owner they follow)
struct EmitterParams {
    explicit EmitterParams(const StaticActor& actor) : EmitterParams(const_cast<StaticActor&>(actor)) {}
    explicit EmitterParams(StaticActor& actor)
        : paramA(actor.params[0]), paramB(actor.params[1]), paramWord(actor.params, 2), owner(actor.hitBox.result) {}
    uint8_t& paramA;
    uint8_t& paramB;
    BytesField<uint16_t, 4> paramWord;
    uint32_t& owner; // the owner's identity word (the hit box's first word)
};
// the ring radii by ring index (exe data)
const DataTable<ScalarTable<int16_t, 0x12>> ringRadii{TableId::St54CastRingRadii};

// a stage type-3 effect of the variant (a stage static slot, exe 0x5e12f0); 0 when the pool is full
uint32_t spawnStageEffect(uint8_t variant) {
    const uint32_t slot = allocActor60(kActorStageClass); // exe 0x5e12f0
    if (!slot) return 0;
    StaticActor& e = staticActorAt(slot);
    e.flags = kActorLive;
    e.type = kStageEffectType;
    e.variant = variant;
    e.loopFlags |= kActorSweepGroup2;
    return slot;
}
// the inlined ring loops: n effects around (-0x48, y, -0x98), the ring index's radius away, from yaw 0xc00 on
void spawnRing(const MoverFields& mover, uint32_t n, uint32_t step, uint8_t variant, uint32_t y) {
    uint32_t yaw = 0xc00;
    for (uint32_t k = 0; k < n; ++k, yaw += step) {
        const uint32_t slot = spawnStageEffect(variant);
        if (!slot) continue;
        StaticActor& e = staticActorAt(slot);
        StageEffectParams params(staticActorAt(slot));
        e.pos.y = int32_t(y);
        e.pos.x = int32_t(0xffb80000u);
        e.pos.z = int32_t(0xff680000u);
        const int r = ringRadii[mover.ringIndex];
        moveAlongYaw(e.pos, int(yaw & 0xfff), 0, 0, -r); // exe 0x5e36f0
        params.paramA = 0;
        params.paramB = 0;
        params.paramWord = 1;
    }
}
// exe 0x50e664: the ring index grown (to 0x11)
void nextRing(MoverFields& mover) {
    const uint8_t i = mover.ringIndex;
    if (i > kLastRingIndex) return;
    mover.ringIndex = uint8_t(i + 1u);
}
// the ring timer counted to `every`, then the ring and the next ring index (0x50e44d's tail)
void ringOnTimer(MoverFields& mover, uint16_t every, uint32_t n, uint32_t step, uint8_t variant, uint32_t y) {
    const uint16_t t = mover.ringTimer;
    if (t != every) {
        mover.ringTimer = uint16_t(t + 1u); // 0x50e44d
        return;
    }
    spawnRing(mover, n, step, variant, y);
    nextRing(mover);
}

// exe 0x50e310: the rising ring
void risingRing(AnimatedActor& actor, MoverFields& mover) {
    mover.ticks = int16_t(uint16_t(mover.ticks) + 1u);
    switch (RingStep(actor.step)) {
    case RingStep::Still: // 0x50e67f
        actor.velocity.y = 0;
        actor.acceleration.y = 2;
        if (mover.spawnTimer != 0) {
            mover.spawnTimer = uint16_t(mover.spawnTimer - 1u);
            return;
        }
        if (const uint32_t slot = spawnStageEffect(kFeetPuff)) {
            StaticActor& e = staticActorAt(slot);
            StageEffectParams params(staticActorAt(slot));
            e.pos.y.whole = actor.pos.y.whole;
            e.pos.x.whole = int16_t((rngStep() & 0xff) + uint16_t(actor.pos.x.whole) - 0x7fu);
            const uint32_t r = rngStep();
            params.paramA = 0;
            params.paramB = 0;
            params.paramWord = 1;
            e.pos.z.whole = int16_t((r & 0xff) + uint16_t(actor.pos.z.whole) - 0x7fu);
        }
        mover.spawnTimer = 6;
        return;
    case RingStep::Rise: { // 0x50e4eb
        integrateVelocity(actor, 0, actor.velocity.y, 0); // exe 0x5e3690
        if (actor.velocity.y > -0x80)
            actor.velocity.y = int16_t(uint16_t(actor.velocity.y) - uint16_t(actor.acceleration.y));
        if (mover.spawnTimer == 0) {
            if (const uint32_t slot = spawnStageEffect(kRisePuff)) {
                StaticActor& e = staticActorAt(slot);
                StageEffectParams params(staticActorAt(slot));
                e.pos.y.whole = int16_t(0xfd00);
                e.pos.x.whole = int16_t((rngStep() & 0xff) - 0xc7u);
                const uint32_t r = rngStep();
                params.paramA = 0;
                params.paramB = 0;
                e.pos.z.whole = int16_t((r & 0xff) - 0x117u);
                params.paramWord = 1;
            }
            mover.spawnTimer = 8;
            mover.ringTimer = 0;
        } else {
            mover.spawnTimer = uint16_t(mover.spawnTimer - 1u);
        }
        ringOnTimer(mover, 8, 4, 0x180, kRisingRing, 0xfd000000u);
        return;
    }
    case RingStep::Speed: { // 0x50e33c
        if (actor.stepCounter == 0) {
            actor.stepCounter = 1;
            actor.acceleration.y = 0x10;
        }
        integrateVelocity(actor, 0, actor.velocity.y, 0); // exe 0x5e3690
        if (actor.velocity.y > -0x140)
            actor.velocity.y = int16_t(uint16_t(actor.velocity.y) - uint16_t(actor.acceleration.y));
        const int16_t n = mover.ticks;
        if (n >= 0x384) return;
        if (mover.spawnTimer == 0) {
            mover.spawnTimer = 0xc;
            mover.ringTimer = 0;
        } else {
            mover.spawnTimer = uint16_t(mover.spawnTimer - 1u);
        }
        if (n < 0xc8) ringOnTimer(mover, 0xc, 4, 0x180, kSpeedRing, 0xfc980000u);
        else ringOnTimer(mover, 0xc, 6, 0x100, kSpeedRing, 0xfc980000u);
        return;
    }
    default: return;
    }
}
// a hit spark (pool 0xa0 global type 0, exe 0x5e13b0): kept through warps, at the point plus off(rng) on each axis,
// its size (rng & 0x7f) + size
template <class Off> void hitSpark(int16_t px, int16_t py, int16_t pz, Off off, uint32_t size) {
    const uint32_t slot = allocActorA0(0); // exe 0x5e13b0
    if (!slot) return;
    EffectActor& spark = effectActorAt(slot);
    spark.flags = kActorLive;
    spark.runFlags |= kActorWarpKept;
    spark.type = typeByte(GlobalEffectType::HitSpark);
    spark.subtype = 0;
    spark.variant = 0;
    spark.pos.x = int16_t(off(rngStep()) + uint16_t(px));
    spark.pos.y = int16_t(off(rngStep()) + uint16_t(py));
    spark.pos.z = int16_t(off(rngStep()) + uint16_t(pz));
    classState<HitSparkFields>(effectActorAt(slot)).size = uint16_t((rngStep() & 0x7f) + size);
}
// 0x50eca7: a stage type-3 variant-0x21 effect around the frozen position
void smoke(const MoverFields& mover, uint32_t slot) {
    StaticActor& e = staticActorAt(slot);
    StageEffectParams params(staticActorAt(slot));
    e.flags = kActorLive;
    e.type = kStageEffectType;
    e.subtype = 0;
    e.variant = kSmoke;
    e.pos.x = int32_t(uint32_t(int32_t(mover.frozen.x) + int32_t(rngStep() & 0xff) - 0x7f) << 16);
    e.pos.y = int32_t(uint32_t(int32_t(mover.frozen.y) + int32_t(rngStep() & 0xff) - 0x7f) << 16);
    e.pos.z = int32_t(uint32_t(int32_t(mover.frozen.z) + int32_t(rngStep() & 0xff) - 0x7f) << 16);
    params.paramA = 0;
    params.paramB = 0;
    params.paramWord = 1;
}
// the four inlined type-0x67 spawns: an emitter following this mover
void spawnEmitter(AnimatedActor& actor, uint8_t variant, ActorRef<StaticActor>* keep) {
    const uint32_t slot = allocActor60(kActorStageClass); // exe 0x5e12f0
    if (!slot) return;
    StaticActor& e = staticActorAt(slot);
    EmitterParams params(staticActorAt(slot));
    e.flags = kActorLive;
    e.type = kEmitterType;
    e.subtype = 0;
    e.variant = variant;
    e.spawnByte = 0;
    e.pos.x = 0;
    e.pos.y = 0;
    e.pos.z = 0;
    params.paramA = 0;
    params.paramB = 0;
    params.paramWord = 0;
    params.owner = slotIdentityWord(actor);
    if (keep) *keep = &e;
}
// a blast (static global type 7, exe 0x5e1300); 0 when the pool is full
uint32_t spawnBlast() {
    const uint32_t slot = allocActor60(0); // exe 0x5e1300
    if (!slot) return 0;
    StaticActor& blast = staticActorAt(slot);
    blast.flags = kActorLive;
    blast.loopFlags |= kActorSweepGroup2;
    blast.type = typeByte(GlobalStaticType::Blast);
    blast.variant = 0;
    return slot;
}
// exe 0x50e750 (switch table 0x50ed5c): the take-off
void takeoff(AnimatedActor& actor, MoverFields& mover) {
    const uint8_t step = actor.step;
    switch (TakeoffStep(step)) {
    case TakeoffStep::Climb: // 0x50e771
        actor.velocity.y = int16_t(0xfec0);
        moveAlongRotation(actor.pos, actor.rotation, 0, -0x140, 0); // exe 0x5e3810
        mover.ringTimer = 1;
        mover.spawnTimer = 1;
        mover.ticks = 0x64;
        if (mover.flags & kEmittersSpawned) return;
        spawnEmitter(actor, 0, &mover.emitterA);
        spawnEmitter(actor, 2, &mover.emitterB);
        spawnEmitter(actor, 1, nullptr);
        spawnEmitter(actor, 3, nullptr);
        mover.flags |= kEmittersSpawned;
        return;
    case TakeoffStep::Leave: // 0x50e8a2
        if (mover.ticks > 0x50) {
            actor.velocity.y = int16_t(0xfec0);
            moveAlongRotation(actor.pos, actor.rotation, 0, -0x140, 0); // exe 0x5e3810
            mover.ticks = int16_t(uint16_t(mover.ticks) - 1u);
        } else {
            actor.step = uint8_t(step + 1u);
            mover.ticks = 0x64;
        }
        if (gameState().frame & 3) return;
        hitSpark(actor.pos.x.whole, actor.pos.y.whole, actor.pos.z.whole,
                 [](uint32_t r) { return uint32_t(((r & 0x1ff) + 0x1ffff00u) << 7) >> 8; }, 0x100);
        return;
    case TakeoffStep::Burst: { // 0x50e9dd
        if (mover.ticks != 0) mover.ticks = int16_t(uint16_t(mover.ticks) - 1u);
        else actor.step = uint8_t(step + 1u);
        if (mover.ringTimer == 1) {
            if (const uint32_t slot = spawnBlast()) {
                StaticActor& blast = staticActorAt(slot);
                BlastParams params(actorHeaderAt(slot));
                blast.pos.x.whole = actor.pos.x.whole;
                blast.pos.y.whole = int16_t(uint16_t(actor.pos.y.whole) - 0x80u);
                blast.pos.z.whole = actor.pos.z.whole;
                params.size = 0x300;
                params.attack = 0;
            }
            mover.frozen.x = actor.pos.x.whole;
            mover.frozen.y = actor.pos.y.whole;
            mover.frozen.z = actor.pos.z.whole;
            actor.setDrawn(false);
            // the exe writes through the pointers unchecked (null: access violation)
            if (!mover.emitterA || !mover.emitterB) notPortedAt(kStoryPort, "0x50e9dd: emitter pointer null", actor.at);
            mover.emitterA->subState = kEmitterDrain;
            mover.emitterB->subState = kEmitterDrain;
            mover.ringTimer = uint16_t(mover.ringTimer - 1u);
        } else if (!(gameState().frame & 7)) {
            if (const uint32_t slot = spawnBlast()) {
                StaticActor& blast = staticActorAt(slot);
                BlastParams params(actorHeaderAt(slot));
                blast.pos.x.whole = int16_t((rngStep() & 0xff) + uint16_t(mover.frozen.x) - 0x7fu);
                blast.pos.y.whole = int16_t((rngStep() & 0xff) + uint16_t(mover.frozen.y) - 0x7fu);
                const uint32_t r = rngStep();
                params.size = 0x200;
                params.attack = 0;
                blast.pos.z.whole = int16_t((r & 0xff) + uint16_t(mover.frozen.z) - 0x7fu);
            }
        }
        mover.spawnTimer = uint16_t(mover.spawnTimer - 1u); // 0x50eb5c
        if (mover.spawnTimerLow & 7) return;
        hitSpark(mover.frozen.x, mover.frozen.y, mover.frozen.z,
                 [](uint32_t r) { return (uint32_t((r & 0x1ff) * 3u) * 0x80u - 0x18000u) >> 8; }, 0x200);
        if (mover.ticks >= 0x50) return; // 0x50ec6e
        if (const uint32_t slot = allocActor60(kActorStageClass)) smoke(mover, slot); // exe 0x5e12f0
        return;
    }
    case TakeoffStep::Smoulder: // 0x50ec8b
        if (gameState().frame & 0xf) return;
        if (const uint32_t slot = allocActor60(kActorStageClass)) smoke(mover, slot); // exe 0x5e12f0
        return;
    default: return;
    }
}
// exe 0x50ed70 / 0x50ee00: one semi-transparent camera-facing quad (a billboard, word 0 not written) at the position,
// random rotation (rng & 0xfff) << 16 | 0xd0; they differ in word 5 (0x0f60ff / 0x2f60ff)
void glowQuad(const AnimatedActor& actor, uint32_t w5) {
    const uint32_t r = rngStep();
    dc::emit({0, dc::billboardColour(0x808080),
              uint32_t(uint16_t(actor.pos.x.whole)) | (uint32_t(int32_t(actor.pos.y.whole)) << 16), 0x7d1360e0u,
              uint32_t(uint16_t(actor.pos.z.whole)), w5, ((r & 0xfff) << 16) | 0xd0u, 0x7fe0u, 0u, 0x7fffu},
             actor.at, false);
}
// exe 0x50e2d0
void run(AnimatedActor& actor, MoverFields& mover) {
    switch (MoverKind(actor.subState)) { // table 0x8984c4
    case MoverKind::RisingRing: risingRing(actor, mover); break;
    case MoverKind::Takeoff: takeoff(actor, mover); break;
    default: notPortedAt(kStoryPort, "0x50e2d0 sub (table 0x8984c4 runs into data)", actor.subState);
    }
    if (!(actor.flags & kActorDrawn)) return;
    const uint8_t s = actor.subState;
    if (MoverKind(s) != MoverKind::Takeoff && (MoverKind(s) != MoverKind::RisingRing || actor.step == 0)) return;
    glowQuad(actor, 0xf60ffu);  // exe 0x50ed70
    glowQuad(actor, 0x2f60ffu); // exe 0x50ee00
}
// exe 0x50e230; false = freed
bool bind(AnimatedActor& actor, MoverFields& mover) {
    if (!bindActorModel(actor, 0)) { // exe 0x5e0ba0
        freeActor(actor);            // exe 0x5e1490
        return false;
    }
    actor.flags |= uint8_t(kActorScenery | kActorDrawn);
    actor.prevAnim = 0xff;
    actor.anim = 0;
    actor.collisionBox = nullptr;
    actor.interactionBox = nullptr;
    actor.interaction = 0;
    actor.velocity.clearWords();
    const uint8_t kind = mover.kind;
    actor.rotation.z = 0;
    actor.rotation.x = MoverKind(kind) == MoverKind::Takeoff ? 0xf80 : 0;
    actor.airborne = 0;
    actor.onPlatform = 0;
    actor.platform = nullptr;
    actor.actorHit = 0;
    actor.state = uint8_t(MoverState::Run);
    actor.subState = kind;
    actor.restartStep();
    mover.spawnTimer = 6;
    mover.ringIndex = 0;
    mover.ticks = 0;
    mover.flags = 0;
    return true;
}
// exe 0x50e190
void effectMover(AnimatedActor& actor) {
    MoverFields& mover = classState<MoverFields>(actor);
    actor.prevAnim = actor.anim;
    actor.tint = kTintNeutral;
    actor.savePreviousPose();
    actor.savePreviousContact();
    switch (MoverState(actor.state)) { // table 0x8984b8
    case MoverState::Bind:
        if (!bind(actor, mover)) return; // the exe runs the animation tail on the freed actor
        break;
    case MoverState::Run: run(actor, mover); break;
    case MoverState::Free: freeActor(actor); return; // 0x468fa0 (exe 0x5e1490); the exe runs the tail on the freed actor
    case MoverState::RisingRingDirect: risingRing(actor, mover); break;
    case MoverState::TakeoffDirect: takeoff(actor, mover); break;
    default: notPortedAt(kStoryPort, "0x50e190 state (table 0x8984b8)", actor.state);
    }
    if (actor.anim == actor.prevAnim) animStep(actor); // exe 0x5e1ea0
    else animSet(actor, actor.anim, 0);                // exe 0x5e1c50
    animPose(actor);                                   // exe 0x5e2d90
}

} // namespace

void effectMoverUpdate(const PlayerEnv&, uint32_t actor) { effectMover(animatedActorAt(actor)); }

} // namespace od::port
