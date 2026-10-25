// The class states of st40_head_npc.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"

namespace od::port::st40_head_npc_state {

// Its own bytes: its flags (the pose held), its head's angles (the bind sets all four to its yaw)
struct NpcFields : ActorClassState {
    explicit NpcFields(AnimatedActor&) {}
    uint8_t flags = 0;
    int16_t angleA = 0;
    int16_t head = 0;
    int16_t angleC = 0;
    int16_t headTargetAngle = 0;

    void clearBlock() override {
        flags = 0;
        angleA = 0;
        head = 0;
        angleC = 0;
        headTargetAngle = 0;
    }
};

} // namespace od::port::st40_head_npc_state
