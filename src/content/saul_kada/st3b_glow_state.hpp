// The class states of st3b_glow.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"

namespace od::port::st3b_glow_state {

// Its own bytes: its brightness (the grey of its tint, 0..0x10)
struct GlowFields : ActorClassState {
    explicit GlowFields(StaticActor&) {}
    int8_t brightness = 0;

    void clearBlock() override {
        brightness = 0;
    }
};

} // namespace od::port::st3b_glow_state
