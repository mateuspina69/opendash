// ST28 (story 13): the props of the type 0x48 cast, the static type 0x40 subs 1 / 2 (hypothesis names), one routine
// per exe routine. See st28_cast_props.hpp. Neither has an animation; their drawing is the static pool's render.
#include "content/saul_kada/st28_cast_props.hpp"
#include "content/saul_kada/st28_cast_props_state.hpp"

#include "game/actor_animation.hpp"
#include "game/actor_model.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/collision.hpp"
#include "game/footstep_dust.hpp"
#include "game/game_state.hpp"
#include "game/port_gap.hpp"
#include "game/script_state.hpp"
#include "game/spark_emitter.hpp"
#include "game/story.hpp"

namespace od::port {

using namespace st28_cast_props_state;

namespace {

enum class PropState : uint8_t { Bind = 0, Run = 1, Free = 2 };
// the carried prop's point: bone 3's offset
constexpr uint32_t kCarryBone = 3;
constexpr int16_t kCarryOffset[3] = {0, 0, -0x20};

// exe 0x5e1970(carrier, 3, {0, 0, -0x20}): the prop at its carrier's bone 3
void rideCarrier(const AnimatedActor& carrier, StaticActor& prop) {
    int16_t point[3];
    bonePointOffset(carrier, kCarryBone, kCarryOffset, point);
    prop.pos.x = int32_t(uint32_t(int32_t(point[0])) << 16);
    prop.pos.y = int32_t(uint32_t(int32_t(point[1])) << 16);
    prop.pos.z = int32_t(uint32_t(int32_t(point[2])) << 16);
}

// ---- exe 0x5b2170 (static type 0x40 sub 1): states [0x8d0760]: 0x5b2190, 0x5b21d0, 0x5afdd0 ------------------------
enum class ThrownSub : uint8_t { Carried = 0, Flying = 1, Sliding = 2 };
constexpr int32_t kThrowTick = 0x50;
constexpr uint8_t kLandedCue = 2;
constexpr uint8_t kEmitterVariant = 0xb;
constexpr int16_t kEmitterHeight = 0x20, kEmitterLifetime = 0x32;
constexpr uint8_t kDustVariant = 1;

// a spark emitter (the stage's static type 3 variant 0xb) at (x + dx, 0x20, z + dz)
void dropEmitter(const StaticActor& prop, int dx, int dz) {
    const uint32_t slot = allocActor60(kActorStageClass); // exe 0x5e12f0
    if (!slot) return;
    StaticActor& emitter = staticActorAt(slot);
    SparkEmitterParams& params = classState<SparkEmitterParams>(staticActorAt(slot));
    emitter.flags = kActorLive;
    emitter.type = kSparkEmitterStageType;
    emitter.variant = kEmitterVariant;
    emitter.loopFlags |= kActorSweepGroup2;
    emitter.pos.y.whole = kEmitterHeight;
    emitter.pos.x.whole = int16_t(uint16_t(prop.pos.x.whole) + dx);
    params.driftX = 0;
    emitter.pos.z.whole = int16_t(uint16_t(prop.pos.z.whole) + dz);
    params.driftZ = 0;
    params.lifetime = kEmitterLifetime;
}
uint16_t countTick(ThrownPropFields& thrown) {
    const uint16_t n = uint16_t(thrown.ticks + 1u);
    thrown.ticks = n;
    return n;
}
void thrownProp(StaticActor& actor) {
    ThrownPropFields& thrown = classState<ThrownPropFields>(actor);
    switch (PropState(actor.state)) {
    case PropState::Bind: // 0x5b2190
        if (!bindActorModel(actor, 0)) { // exe 0x5e0ba0
            freeActor(actor);            // exe 0x5e1540
            return;
        }
        actor.flags |= kActorDrawn;
        actor.velocity.clearWords();
        thrown.ticks = 0;
        actor.state = uint8_t(PropState::Run);
        actor.subState = uint8_t(ThrownSub::Carried);
        return;
    case PropState::Run: // 0x5b21d0
        switch (ThrownSub(actor.subState)) {
        case ThrownSub::Carried: // 0x5b2347: at bone 3 until the step tick 0x50, then thrown
            rideCarrier(*thrown.carrier, actor);
            if (scriptState().stepTick == kThrowTick) {
                actor.velocity.x = 0;
                actor.velocity.y = int16_t(0xfc00);
                actor.velocity.z = int16_t(0xfe40);
                actor.rotation.y = 0xb00;
                thrown.ticks = 0;
                ++actor.subState;
            }
            return;
        case ThrownSub::Flying: // 0x5b22aa: flies (gravity 0x40) while the step sub-id is not 2; at 0x2a ticks an emitter
            if (scriptState().stepSub == kLandedCue) return;
            integrateVelocity(actor, 0, actor.velocity.y, actor.velocity.z); // exe 0x5e3690
            actor.velocity.y += 0x40u;
            if (countTick(thrown) <= 0x29) return;
            dropEmitter(actor, 0x40, 0x40);
            thrown.ticks = 0;
            actor.velocity.y = 0x10;
            ++actor.subState;
            return;
        case ThrownSub::Sliding: { // 0x5b21f6: dust every 8th tick, an emitter at tick 0x3c, freed after 0x96
            integrateVelocity(actor, 0, actor.velocity.y, 0);
            if ((gameState().frame & 7) == 0) {
                const int16_t point[3] = {actor.pos.x.whole, 0, actor.pos.z.whole};
                footstepDustAt(actor, kDustVariant, point); // exe 0x5ad170(a, 1, point)
            }
            if (countTick(thrown) == 0x3c) dropEmitter(actor, -0x40, 0x60);
            if (thrown.ticks > 0x96) actor.state = uint8_t(PropState::Free);
            return;
        }
        default: return; // 0x5b23c3
        }
    case PropState::Free: freeActor(actor); return; // 0x5afdd0
    default: notPortedAt(kStoryPort, "0x5b2170 state", actor.state);
    }
}

// ---- exe 0x5b1ef0 (static type 0x40 sub 2): states [0x8d074c]: 0x5b1f40, 0x5b1fe0, 0x5afdd0; sub-states
// [0x8d0758] ----------------------------------------------------------------------------------------------------------
enum class DroppedSub : uint8_t { Carried = 0, Hopping = 1 };
// the hop's target (0x240, -0x40, -0x100), 16.16
constexpr int32_t kHopTarget[3] = {0x2400000, int32_t(0xffc00000u), int32_t(0xff000000u)};
constexpr uint8_t kBounces = 2;

void droppedProp(const PlayerEnv& env, StaticActor& actor) {
    DroppedPropParams& dropped = classState<DroppedPropParams>(actor);
    actor.prevPos = actor.pos;
    actor.prevRotation = actor.rotation;
    actor.savePreviousContact();
    switch (PropState(actor.state)) {
    case PropState::Bind: { // 0x5b1f40
        if (!bindActorModel(actor, actor.variant)) { // exe 0x5e0bc0(a, &variant)
            freeActor(actor);                         // exe 0x5e1540
            return;
        }
        actor.flags |= kActorDrawn;
        actor.airborne = 0;
        actor.onPlatform = 0;
        actor.collisionBox.pointAt(TableId::St28CastPropsDroppedBox);
        actor.wallHit = 0;
        actor.actorHit = 0;
        const int32_t from[3] = {actor.pos.x, actor.pos.y, actor.pos.z};
        actor.velocity.y = int16_t(launchSpeed(from, kHopTarget, 0x180, 0x30, 0xffffffffu)); // exe 0x5e3ee0
        actor.subState = uint8_t(DroppedSub::Carried);
        actor.velocity.z = int16_t(0xfe80);
        actor.acceleration.y = 0x30;
        ++actor.state;
        return;
    }
    case PropState::Run: // 0x5b1fe0
        switch (DroppedSub(actor.subState)) {
        case DroppedSub::Carried: // 0x5b2000: at bone 3 until the carrier lets go (its first parameter)
            rideCarrier(*dropped.carrier, actor);
            if (PropCarrierParams(*dropped.carrier).letGo != 0) ++actor.subState;
            return;
        case DroppedSub::Hopping: // 0x5b2070: hops, bounces twice, then spins
            if (actor.step < kBounces) {
                integrateVelocity(actor, 0, actor.velocity.y, actor.velocity.z); // exe 0x5e3690
                actor.velocity.y += uint16_t(actor.acceleration.y);
                if (actor.velocity.y > 0 && (actorPushOut(env, actor) & kWallOnY)) { // exe 0x410960
                    actor.velocity.y = int16_t(-(int32_t(actor.velocity.y) / 2));
                    ++actor.step;
                }
            } else {
                actor.rotation.y = int16_t((uint16_t(actor.rotation.y) + 0x20u) & 0xfffu);
            }
            return;
        default: notPortedAt(kStoryPort, "0x5b1ef0 sub-state", actor.subState); // [0x8d0758] has two entries
        }
    case PropState::Free: freeActor(actor); return; // 0x5afdd0
    default: notPortedAt(kStoryPort, "0x5b1ef0 state", actor.state);
    }
}

} // namespace

void thrownPropUpdate(const PlayerEnv&, uint32_t actor) { thrownProp(staticActorAt(actor)); }
void droppedPropUpdate(const PlayerEnv& env, uint32_t actor) { droppedProp(env, staticActorAt(actor)); }

} // namespace od::port
