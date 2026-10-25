// The class states of st33_key_script.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/field_alias.hpp"
#include "game/state_array.hpp"

namespace od::port::st33_key_script_state {

// Its own bytes: its hit box; its scale as the two dwords the exe stores
struct KeyItemFields : ActorClassState {
    explicit KeyItemFields(StaticActor& actor) : hitBox(actor.hitBox), scale(actor.scale), tint(actor.tint) {}
    HitBoxHead& hitBox;
    ShortVec3& scale; // the slot's scale and tint (stored as two dwords of the same word)
    uint16_t& tint;
    void setScaleWords(uint32_t word) {
        scale.z = int16_t(uint16_t(word));
        tint = uint16_t(word >> 16);
        scale.x = int16_t(uint16_t(word));
        scale.y = int16_t(uint16_t(word >> 16));
    }
};

} // namespace od::port::st33_key_script_state
