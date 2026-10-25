// The class states of st40_fx.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/field_alias.hpp"
#include "game/state_array.hpp"

namespace od::port::st40_fx_state {

// The ring's bytes: its release (set from outside: the fade), its ticks, its radius and its pitch
struct RingFields : ActorClassState {
    explicit RingFields(EffectActor& actor) : release(actor.params[0]) {}
    uint8_t& release;
    uint32_t ticks = 0;
    uint32_t radius = 0;
    int32_t pitch = 0; // read as its low short
};

// the spark's bytes: game/exit_transitions_state TeleporterSparkFields (the teleport spawns it too)

} // namespace od::port::st40_fx_state
