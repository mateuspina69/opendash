// The class states of sign.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/field_alias.hpp"

namespace od::port::sign_state {

// the sign's own fields
struct SignFields : ActorClassState {
    explicit SignFields(StaticActor& actor) : message(actor.params, 0), dropFlag(actor.params, 2) {}
    BytesField<uint16_t, 4> message; // its event's message
    BytesField<uint16_t, 4> dropFlag; // the event flag that lets a hit drop it (0xffff: none)
    HitBox hitBox;

    void clearBlock() override {
        hitBox = {};
    }
};

} // namespace od::port::sign_state
