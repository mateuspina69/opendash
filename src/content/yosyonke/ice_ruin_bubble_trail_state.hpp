// The class states of ice_ruin_bubble_trail.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/field_alias.hpp"
#include "game/state_array.hpp"

namespace od::port::ice_ruin_bubble_trail_state {

constexpr uint32_t kBubbleSlots = 8;

// The trail's own bytes: the ring's next slot, the bubbles' points, lives, flags and yaw bytes; the torpedo
struct TrailFields : ActorClassState {
    explicit TrailFields(StaticActor& actor) : nextSlot(actor.rootMatrix.e[0], 0) {}
    PartField<uint8_t, int16_t> nextSlot;
    StateArray<uint8_t, 7> clearedPad1{}; // bytes the block clear covers (no reader)
    StateArray<int16_t, kBubbleSlots> bubbleX{};
    StateArray<int16_t, kBubbleSlots> bubbleY{};
    StateArray<int16_t, kBubbleSlots> bubbleZ{};
    union {
        StateArray<uint8_t, kBubbleSlots> life{};
        StateArray<int8_t, kBubbleSlots> lifeSigned;
    };
    StateArray<uint8_t, kBubbleSlots> bubbleFlags{};
    ActorRef<ProjectileActor> torpedo;
    StateArray<uint8_t, kBubbleSlots> bubbleYaw{};
    StateArray<uint8_t, 4> clearedPad0{}; // bytes the block clear covers (no reader)
    void clearBubbles() {
        bubbleYaw = {};
        clearedPad0 = {};
        clearedPad1 = {};
        bubbleX = {};
        bubbleY = {};
        bubbleZ = {};
        life = {};
        bubbleFlags = {};
        nextSlot = 0;
    }

    void clearBlock() override {
        torpedo = {};
        bubbleYaw = {};
        clearedPad0 = {};
    }
};

} // namespace od::port::ice_ruin_bubble_trail_state
