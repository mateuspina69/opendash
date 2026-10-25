// The ST3C rising / sinking model (static, exe 0x596170; hypothesis names): variant 0 rises to 0x200 above the floor and
// bobs there (+-0x20), the others sink to -0x90.
//
// States [0x8ce0ac]: 0 bind 0x5961c0 (box 0x8ce0a0), 1 move 0x596230 (variant 0 0x596260, else 0x596350).
#include "content/saul_kada/st3c_rising_model.hpp"

#include "game/actor_model.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/port_gap.hpp"
#include "game/story.hpp"

namespace od::port {

namespace {

enum class ModelState : uint8_t { Bind = 0, Move = 1 };
enum class RiseStep : uint8_t { Start = 0, Rise = 1, Brake = 2, Bob = 3 };
enum class SinkStep : uint8_t { Start = 0, Sink = 1, Down = 2 };
constexpr int kRiseAboveFloor = 0x200;
constexpr int16_t kBobSpeed = 0x20, kBobAccel = 2, kSinkSpeed = -0x90;

// exe 0x596260 (variant 0)
void rise(const PlayerEnv& env, StaticActor& actor) {
    switch (RiseStep(actor.subState)) {
    case RiseStep::Start:
        actor.velocity.y = 0x80;
        actor.subState = uint8_t(RiseStep::Rise);
        [[fallthrough]];
    case RiseStep::Rise:
        if (actorGround(env, actor) - kRiseAboveFloor < int(actor.pos.y.whole)) { // exe 0x40f190
            ++actor.subState;
            integrateVelocity(actor, 0, actor.velocity.y, 0);
            return;
        }
        break;
    case RiseStep::Brake:
        actor.velocity.y = int16_t(actor.velocity.y - 8);
        if (actor.velocity.y < 0) {
            actor.velocity.y = 0;
            actor.subState = uint8_t(RiseStep::Bob);
            actor.acceleration.y = -kBobAccel;
            integrateVelocity(actor, 0, actor.velocity.y, 0);
            return;
        }
        break;
    case RiseStep::Bob:
        actor.velocity.y = int16_t(actor.velocity.y + actor.acceleration.y);
        if (actor.velocity.y >= kBobSpeed + 1) {
            actor.velocity.y = kBobSpeed;
            actor.acceleration.y = -kBobAccel;
        }
        if (actor.velocity.y <= -kBobSpeed - 1) {
            actor.velocity.y = -kBobSpeed;
            actor.acceleration.y = kBobAccel;
        }
        break;
    default: break;
    }
    integrateVelocity(actor, 0, actor.velocity.y, 0);
}
// exe 0x596350 (the other variants)
void sink(StaticActor& actor) {
    const uint8_t step = actor.subState;
    if (step > uint8_t(SinkStep::Sink)) {
        integrateVelocity(actor, 0, actor.velocity.y, 0);
        return;
    }
    if (SinkStep(step) == SinkStep::Start) {
        actor.velocity.y = 0;
        actor.subState = uint8_t(SinkStep::Sink);
    }
    actor.velocity.y = int16_t(actor.velocity.y - 4);
    if (actor.velocity.y <= kSinkSpeed - 1) {
        actor.velocity.y = kSinkSpeed;
        ++actor.subState;
    }
    integrateVelocity(actor, 0, actor.velocity.y, 0);
}
// exe 0x596170
void risingModel(const PlayerEnv& env, StaticActor& actor) {
    actor.savePreviousPose();
    actor.savePreviousContact();
    switch (ModelState(actor.state)) { // [0x8ce0ac]
    case ModelState::Bind:
        if (!bindActorModel(actor, 0)) {
            freeActor(actor);
            return;
        }
        actor.velocity.setXY(0, 0);
        actor.rotation.z = 0;
        actor.airborne = 0;
        actor.onPlatform = 0;
        actor.interactionBox = nullptr;
        actor.interaction = 0;
        actor.flags |= kActorDrawn;
        actor.maxHitPoints = 1;
        actor.hitPoints = 1;
        actor.collisionBox.pointAt(TableId::St3cRisingModelBox);
        actor.clearClassBlock();
        actor.enterState(uint8_t(ModelState::Move));
        return;
    case ModelState::Move:
        if (actor.variant == 0) rise(env, actor);
        else sink(actor);
        return;
    default: notPortedAt(kStoryPort, "static 0x596170 state", actor.state);
    }
}

} // namespace

void st3cRisingModelUpdate(const PlayerEnv& env, uint32_t actor) { risingModel(env, staticActorAt(actor)); }

} // namespace od::port
