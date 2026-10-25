// The class states of roll_spawners.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/state_array.hpp"

namespace od::port::roll_spawners_state {

// the spawners' bytes +0x18..+0x1f (cleared by their state 0; no other reader)
struct RollSpawnerFields : ActorClassState {
    explicit RollSpawnerFields(ControllerActor&) {}
    StateArray<uint8_t, 8> scratch{};
};

} // namespace od::port::roll_spawners_state
