// The class states of attract_effects.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/field_alias.hpp"
#include "game/state_array.hpp"

namespace od::port::attract_effects_state {

constexpr size_t kBoltPoints = 16;

// One point of a bolt (8 bytes): its position and its fade
struct BoltPoint {
    int16_t x = 0;
    int16_t y = 0;
    int16_t z = 0;
    int16_t fade = 0;
};

// The class's bytes: the emitters' timer (the slot's rotation bytes), the bolt's points (from +0x2c: the slot's bytes
// up to +0xab, the bolt binds no model) and the two counts (the sub-state / step bytes), the CLUT sequences of a
// flashing emitter (its spawn bytes 8 / 9: first .. end)
struct LightningFields : ActorClassState {
    explicit LightningFields(StaticActor& actor) : firstSequence(actor.params[0]), endSequence(actor.params[1]) {}
    FieldAlias<int8_t, uint8_t> firstSequence;
    FieldAlias<int8_t, uint8_t> endSequence;
    int32_t timer = 0;
    StateArray<BoltPoint, kBoltPoints> points{};
};

} // namespace od::port::attract_effects_state
