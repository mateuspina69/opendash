// ST28 (the Saul Kada fire ruin, story 13): the props the type 0x48 cast (content/saul_kada/fire_ruin_cutscene_cast) carries
// -- the static pool's type 0x40 subs 1 and 2 (stage static classes [0x8edeac]: type 0x40 -> 0x8ede94: 0x5b12c0,
// 0x5b2170, 0x5b1ef0, 0x5b18c0 by sub).
//
//   thrownPropUpdate   exe 0x5b2170  static type 0x40 sub 1 (carried by 0x4d1040 variant 2): rides bone 3 of its
//                                    carrier until the step tick 0x50, then is thrown (gravity 0x40 per tick) while the
//                                    step sub-id is not 2; after 0x2a ticks drops a spark emitter (static type 3 variant
//                                    0xb), then slides leaving dust every 8 ticks, a second emitter at tick 0x3c, and is
//                                    freed after 0x96 ticks
//   droppedPropUpdate  exe 0x5b1ef0  static type 0x40 sub 2 (carried by 0x4d14b0): rides bone 3 of its carrier until the
//                                    carrier lets go, then hops toward the point (0x240, -0x40, -0x100), bounces twice
//                                    off the ground (the fall speed halved and reversed) and spins
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"
#include "game/actor_state.hpp"

namespace od::port {

// The thrown prop's carrier (written by the cast that spawns it)
struct ThrownPropParams : ActorClassState {
    explicit ThrownPropParams(StaticActor&) {}
    ActorRef<AnimatedActor> carrier;

    void clearBlock() override {
        carrier = nullptr;
    }
};
// The dropped prop's carrier
struct DroppedPropParams : ActorClassState {
    explicit DroppedPropParams(StaticActor&) {}
    ActorRef<AnimatedActor> carrier;

    void clearBlock() override {
        carrier = nullptr;
    }
};
// The dropping carrier's parameter (its first spawn byte; the script sets it): the prop let go
struct PropCarrierParams {
    explicit PropCarrierParams(const AnimatedActor& actor) : PropCarrierParams(const_cast<AnimatedActor&>(actor)) {}
    explicit PropCarrierParams(AnimatedActor& actor) : letGo(actor.params[0]) {}
    uint8_t& letGo;
};
constexpr uint8_t kCastPropType = 0x40;
constexpr uint8_t kHeldPropSubtype = 0, kThrownPropSubtype = 1, kDroppedPropSubtype = 2;

void thrownPropUpdate(const PlayerEnv& env, uint32_t actor);
void droppedPropUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
