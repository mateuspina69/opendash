// The class states of st27_scroll_glow.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"

namespace od::port::st27_scroll_glow_state {

// The glow's own byte: its texture's scroll
struct GlowFields : ActorClassState {
    explicit GlowFields(EffectActor&) {}
    uint8_t scroll = 0;
};

} // namespace od::port::st27_scroll_glow_state
