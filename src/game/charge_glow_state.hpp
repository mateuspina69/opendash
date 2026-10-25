// The class states of charge_glow.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/charge_glow.hpp"
#include "game/field_alias.hpp"

namespace od::port::charge_glow_state {

// The glow's fields: its parameters and its radius (/ 2)
struct ChargeGlowFields : ChargeGlowParams {
    explicit ChargeGlowFields(const EffectActor& actor) : ChargeGlowFields(const_cast<EffectActor&>(actor)) {}
    explicit ChargeGlowFields(EffectActor& actor) : ChargeGlowParams(actor), radius(actor.params[3]) {}
    FieldAlias<int8_t, uint8_t> radius;
};

} // namespace od::port::charge_glow_state
