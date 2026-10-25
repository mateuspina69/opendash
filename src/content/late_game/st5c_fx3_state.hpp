// The class states of st5c_fx3.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/loaded_data.hpp"
#include "game/actor_state.hpp"
#include "content/late_game/st5c_fx3.hpp"
#include "game/field_alias.hpp"
#include "game/state_array.hpp"

namespace od::port::st5c_fx3_state {

constexpr uint32_t kImpactSpokes = 0x20;

// The impact's own bytes (its block from +0x28): its parameter word, its timer, a word, the randomised angles and bytes
// (never read again by the class), the copy of its parameter word
struct ImpactFields : ActorClassState {
    explicit ImpactFields(StaticActor& actor) : paramsWord(actor.params, 0), timer(actor.rotation.x), unusedWord(actor.rotation.y) {}
    BytesField<uint32_t, 4> paramsWord;
    FieldAlias<uint16_t, int16_t> timer;
    FieldAlias<uint16_t, int16_t> unusedWord;
    StateArray<uint16_t, kImpactSpokes> angles{};
    StateArray<uint8_t, kImpactSpokes> lengths{};
    StateArray<uint8_t, kImpactSpokes> widths{};
    uint32_t paramsCopy = 0;
};

// The shock ring's own bytes: the outer radius >> 4 (the step counter's byte), its count, period, radius speed and
// width >> 4 (the parameters; copied as one word), its radius, the spawner's toggle, the spawner
struct ShockRingFields : ActorClassState, ShockRingParams {
    explicit ShockRingFields(EffectActor& actor) : ShockRingParams(actor), count(actor.params[0]), period(actor.params[1]), speed(actor.params[2]), width(actor.params[3]), paramsWord(actor.params, 0), radius(actor.trail[0]) {}
    uint8_t& count;
    uint8_t& period;
    FieldAlias<int8_t, uint8_t> speed;
    uint8_t& width;
    BytesField<uint32_t, 4> paramsWord;
    int16_t& radius;
    uint8_t toggle = 0;
    ActorRef<EffectActor> spawner;
};

} // namespace od::port::st5c_fx3_state
