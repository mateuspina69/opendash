// The class states of st41_gate_line.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <array>
#include <cstdint>

#include "game/actor_state.hpp"
#include "game/player/player_state.hpp"
#include "game/vectors.hpp"

namespace od::port::st41_gate_line_state {

// The line's bytes: its point (the slot's spawn record point), its yaw, the side the player was on
struct LineFields : ActorClassState {
    explicit LineFields(ControllerActor& actor) : point(actor.recordPoint) {}
    ShortVec4& point;
    uint16_t yaw = 0;
    uint16_t side = 0;
    std::array<uint16_t, kPlayerSlots - 1> otherSides{}; // the same of players 1..7 (OpenDash's co-op)
};

} // namespace od::port::st41_gate_line_state
