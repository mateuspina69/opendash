// The class states of spark_emitter.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/state_array.hpp"
#include "game/vectors.hpp"
#include "game/spark_emitter.hpp"
#include "game/field_alias.hpp"

namespace od::port::spark_emitter_state {

constexpr uint32_t kParticles = 14;

// The emitter's fields: the particles live in the slot's body (from +0x28), the carry offset the riders' pass leaves
// in its previous position's x / z (game/actor_collision)
struct SparkEmitterFields : SparkEmitterParams {
    explicit SparkEmitterFields(const StaticActor& actor) : SparkEmitterFields(const_cast<StaticActor&>(actor)) {}
    explicit SparkEmitterFields(StaticActor& actor) : SparkEmitterParams(actor) {}
    StateArray<ShortVec4, kParticles> particles{};            // x, y, z and (the 4th lane) the ticks left
    StateArray<StateArray<int8_t, 2>, kParticles> wobble{};   // the x / z wobble counters
    uint16_t ticksLeft = 0; // the emits' ticks left (from lifetime)
    uint16_t emitTimer = 0;
    uint16_t liveMask = 0;  // bit k: particle k lives
    int8_t rise = 0;

    // particle k's ticks left (its 4th lane)
    FieldAlias<uint16_t, int16_t> life(size_t k) { return FieldAlias<uint16_t, int16_t>(particles[k].spare); }

    void clearBlock() override {
        SparkEmitterParams::clearBlock();
        ticksLeft = 0;
        emitTimer = 0;
        liveMask = 0;
        rise = 0;
    }
};

} // namespace od::port::spark_emitter_state
