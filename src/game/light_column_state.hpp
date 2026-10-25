// The class states of light_column.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/field_alias.hpp"

namespace od::port::light_column_state {

constexpr int16_t kMoteSizeExtra = 0x5c;

// ---- the glow (pool 0xa0) ----------------------------------------------------------------------------------------
// The glow's fields (an emitter and its motes share them)
struct GlowFields : ActorClassState {
    explicit GlowFields(EffectActor& actor) : size(actor.params, 0), heightSteps(actor.params[2]), period(actor.params[3]), timer(actor.trail[0]) {}
    BytesField<int16_t, 4> size; // the glow's size (a mote's: + kMoteSizeExtra)
    FieldAlias<int8_t, uint8_t> heightSteps; // the column's height in 8-unit steps
    FieldAlias<int8_t, uint8_t> period; // an emitter's ticks between two motes
    int16_t& timer; // the emitter's countdown to the next mote / a mote's ticks left
    int8_t rise = 0; // a mote's rise per tick
    uint8_t brightness = 0; // a mote's grey (fades over its last 16 ticks)
    ActorRef<StaticActor> column; // the emitter's light column
};

} // namespace od::port::light_column_state
