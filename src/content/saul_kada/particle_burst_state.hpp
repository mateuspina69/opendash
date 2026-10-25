// The class states of particle_burst.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/vectors.hpp"
#include "game/field_alias.hpp"
#include "game/state_array.hpp"

namespace od::port::particle_burst_state {

constexpr uint32_t kParticles = 10;

// One particle (8 bytes): its s16 point and its word (the spin nibble << 4 | the colour index)
struct BurstParticle {
    ShortVec3 point;
    uint16_t word = 0; // the low nibble: the colour step; the high: the spin
};

// The burst's own bytes (over the actor's contact and root blocks: it has no model): its hit box, its particles; the
// spin's spare lane cleared by the cutscene bind
struct BurstFields : ActorClassState {
    explicit BurstFields(StaticActor& actor) : spinSpare(actor.spin.spare) {}
    int16_t& spinSpare;
    HitBoxHead hitBox;
    StateArray<BurstParticle, kParticles> particles{};

    void clearBlock() override {
        for (size_t i = 9; i < 10; ++i) particles[i] = {};
    }
};

} // namespace od::port::particle_burst_state
