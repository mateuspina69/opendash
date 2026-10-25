// The class states of treasure_chest.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/field_alias.hpp"

namespace od::port::treasure_chest_state {

// the chest's own fields
struct ChestFields : ActorClassState {
    explicit ChestFields(AnimatedActor& actor) : group(actor.params[0]) {}
    FieldAlias<int8_t, uint8_t> group; // its row of the content table (by group, then variant)
    uint8_t startKey = 0; // the key a new animation starts at (consumed)

    void clearBlock() override {
        startKey = 0;
    }
};

} // namespace od::port::treasure_chest_state
