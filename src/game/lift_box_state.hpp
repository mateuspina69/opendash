// The class states of lift_box.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/field_alias.hpp"

namespace od::port::lift_box_state {

// The box's own fields
struct LiftBoxFields : ActorClassState {
    explicit LiftBoxFields(StaticActor& actor) : holder(actor.params[0]), carried(actor.params[1]), liftLock(actor.params[2]), settled(actor.params[3]), travelYaw(actor.rotation.spare), behaviour(actor.velocity.spare), collisionBits(actor.acceleration.spare) {}
    uint8_t& holder; // set by an outside holder: kHolderHolds / kHolderLetGo | kHolderMover
    uint8_t& carried; // 1 once lifted / thrown / pushed
    uint8_t& liftLock; // != 0: the player cannot lift it
    uint8_t& settled; // 1 while carried / held (no out-of-bounds check)
    int16_t& travelYaw; // the yaw it flies along (the rotation's spare lane)
    FieldAlias<uint16_t, int16_t> behaviour; // kBox* bits (the velocity's spare lane)
    FieldAlias<uint16_t, int16_t> collisionBits; // kBoxHit* bits (the acceleration's spare lane)
    HitBox hitBox; // contact: x the push's direction, y its strength (0: no hop)

    void clearBlock() override {
        hitBox = {};
    }
};

} // namespace od::port::lift_box_state
