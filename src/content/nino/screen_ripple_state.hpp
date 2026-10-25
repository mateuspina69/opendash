// The class states of screen_ripple.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/state_array.hpp"

namespace od::port::screen_ripple_state {

// ---- variant 2 (0x438bc0): 8 rays (an angle byte and a length byte each, random 0..0x3f at state 0)
// around the point, the length * the ticks left (9 down to 1: freed when they reach 0). Drawn while the point's view
// depth is in 0x30 .. the effects' far distance * 4: a DR_MODE (abr 1) and, per ray, two POLY_G4 0x3a in the camera
// plane (corners in view units around the point, >> (12 - stage shift)): (A - 0x10, l) black, (A, 3/4 l) 0x0c1808,
// (A, l) 0x404040, (A + 0x10, l) black; then the same with (A, 5/4 l). Port: dc::otQuadG4 with the view-plane offsets.
struct RayStarFields : ActorClassState {
    explicit RayStarFields(EffectActor& actor) : ticks(actor.params[3]) {}
    uint8_t& ticks;
    StateArray<uint8_t, 8> rayAngles{};
    StateArray<uint8_t, 8> rayLengths{};
};

} // namespace od::port::screen_ripple_state
