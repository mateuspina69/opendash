// The class states of extinguisher.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/state_array.hpp"
#include "game/vectors.hpp"
#include "game/field_alias.hpp"

namespace od::port::extinguisher_state {

constexpr int kMistParticles = 10;
struct MistParticle {
    ShortVec3 point;
    int16_t yaw = 0;
};
struct MistMotion {
    int16_t vx = 0, vy = 0, vz = 0;
    int16_t ticks = 0;
};

struct MistFields : ActorClassState {
    explicit MistFields(StaticActor&) {}
    int16_t count = 0;
    uint16_t alive = 0;
    StateArray<MistParticle, kMistParticles> particles{};
    StateArray<MistMotion, kMistParticles> motions{};

    void clearBlock() override {
        motions[8] = {};
        motions[9] = {};
    }
};

} // namespace od::port::extinguisher_state
