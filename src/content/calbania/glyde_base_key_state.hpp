// The class states of glyde_base_key.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/field_alias.hpp"

namespace od::port::glyde_base_key_state {

struct GlydeKeyFields : ActorClassState {
    explicit GlydeKeyFields(StaticActor& actor) : flagIndex(actor.params[0]) {}
    FieldAlias<int8_t, uint8_t> flagIndex; // its flag's row of [0x8d07a8]
    uint16_t landedMark = 0; // set when it lands (its reader is not traced)

    void clearBlock() override {
        landedMark = 0;
    }
};

} // namespace od::port::glyde_base_key_state
