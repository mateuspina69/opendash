// The class states of st4b_cast.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"

namespace od::port::st4b_cast_state {

// The talker's own bytes: a word cleared at the bind, its pose let step
struct TalkerFields : ActorClassState {
    explicit TalkerFields(AnimatedActor&) {}
    uint32_t word = 0;
    uint8_t stepping = 0;

    void clearBlock() override {
        word = 0;
        stepping = 0;
    }
};

} // namespace od::port::st4b_cast_state
