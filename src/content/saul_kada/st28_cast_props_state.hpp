// The class states of st28_cast_props.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "content/saul_kada/st28_cast_props.hpp"
#include "game/field_alias.hpp"

namespace od::port::st28_cast_props_state {

// The thrown prop's own bytes: a tick counter
struct ThrownPropFields : ThrownPropParams {
    explicit ThrownPropFields(const StaticActor& actor) : ThrownPropFields(const_cast<StaticActor&>(actor)) {}
    explicit ThrownPropFields(StaticActor& actor) : ThrownPropParams(actor), ticks(actor.hitBox.previousX) {}
    FieldAlias<uint16_t, int16_t> ticks;

    void clearBlock() override {
        ThrownPropParams::clearBlock();
    }
};

} // namespace od::port::st28_cast_props_state
