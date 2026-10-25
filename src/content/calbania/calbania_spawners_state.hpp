// The class states of calbania_spawners.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/field_alias.hpp"
#include "game/state_array.hpp"

namespace od::port::calbania_spawners_state {

// the ST1F spawner's own bytes: the slots of the pair (Appo, Dah)
struct St1fSpawnerFields : ActorClassState {
    explicit St1fSpawnerFields(ControllerActor&) {}
    StateArray<uint8_t, 2> pairSlots{};
    StateArray<uint8_t, 6> clearedPad0{}; // bytes the block clear covers (no reader)
    void clearBlock() {
        pairSlots = {};
        clearedPad0 = {};
    }
};

} // namespace od::port::calbania_spawners_state
