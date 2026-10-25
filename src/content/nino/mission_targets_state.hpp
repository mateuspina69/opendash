// The class states of mission_targets.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/field_alias.hpp"
#include "game/state_array.hpp"

namespace od::port::mission_targets_state {

// The smoke columns' bytes (over the slot's model fields): each column's puff height, tick, life, rise speed and grey
struct SmokeColumnsFields : ActorClassState {
    explicit SmokeColumnsFields(StaticActor& actor) : unusedC8(actor.gunshipTimer()) {}
    uint8_t& unusedC8; // cleared at the start (no reader)
    StateArray<int16_t, 8> puffY{};
    StateArray<int16_t, 8> puffTick{}; // < 0: waiting
    StateArray<uint8_t, 8> puffLife{};
    StateArray<int8_t, 8> puffRise{};
    StateArray<int8_t, 8> puffGrey{};

    void clearBlock() override {
        puffGrey = {};
    }
};

} // namespace od::port::mission_targets_state
