// The class states of st28_guard_shot.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/field_alias.hpp"
#include "game/state_array.hpp"

namespace od::port::st28_guard_shot_state {

// The shot's own bytes (0x20 cleared at the bind): its hit box
struct ShotFields : ActorClassState {
    explicit ShotFields(ProjectileActor&) {}
    StateArray<uint8_t, 4> clearedPad0{}; // bytes the block clear covers (no reader)
    HitBoxHead hitBox;
    StateArray<uint8_t, 20> clearedPad1{}; // bytes the block clear covers (no reader)
    void clearBytes() {
        clearedPad0 = {};
        hitBox = {};
        clearedPad1 = {};
    }
};

} // namespace od::port::st28_guard_shot_state
