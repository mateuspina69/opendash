// The class states of placed_models.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"

namespace od::port::placed_models_state {

// The placed model's step target (its class block): where its x / z steps stop
struct PlacedModelFields : ActorClassState {
    explicit PlacedModelFields(StaticActor&) {}
    int16_t targetX = 0;
    int16_t targetZ = 0;

    void clearBlock() override {
        targetX = 0;
        targetZ = 0;
    }
};

// The turning model's class block: a word its start clears and the yaw it was put down at (their readers are not
// traced)
struct TurningModelFields : ActorClassState {
    explicit TurningModelFields(StaticActor&) {}
    int16_t startCleared = 0;
    int16_t startYaw = 0;

    void clearBlock() override {
        startCleared = 0;
        startYaw = 0;
    }
};

} // namespace od::port::placed_models_state
