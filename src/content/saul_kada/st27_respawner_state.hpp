// The class states of st27_respawner.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/state_array.hpp"

namespace od::port::st27_respawner_state {

// Its own bytes: each record's spawned slot and its countdown
struct RespawnerFields : ActorClassState {
    explicit RespawnerFields(StaticActor&) {}
    StateArray<uint8_t, 8> slots{};
    StateArray<uint8_t, 8> countdowns{};

    void clearBlock() override {
        slots = {};
        countdowns = {};
    }
};

} // namespace od::port::st27_respawner_state
