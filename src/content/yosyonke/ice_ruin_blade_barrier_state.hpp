// The class states of ice_ruin_blade_barrier.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/field_alias.hpp"
#include "game/state_array.hpp"

namespace od::port::ice_ruin_blade_barrier_state {

// The parts' own bytes: a segment's hidden / fade ticks (the acceleration's spare lane); the hub of a segment /
// blocker, the hub's mask of the segments standing (bit = segment index), a segment's hit box
struct BarrierFields : ActorClassState {
    explicit BarrierFields(StaticActor& actor) : ticks(actor.acceleration.spare) {}
    int16_t& ticks;
    ActorRef<StaticActor> hub;
    uint8_t standingSegments = 0;
    StateArray<uint8_t, 3> clearedPad0{}; // bytes the block clear covers (no reader)
    HitBoxHead hit;
    void clearOwnBytes() {
        standingSegments = 0;
        clearedPad0 = {};
        hit = {};
    }

    void clearBlock() override {
        hub = {};
        standingSegments = 0;
        clearedPad0 = {};
        hit = {};
    }
};

} // namespace od::port::ice_ruin_blade_barrier_state
