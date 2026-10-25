// The class states of st0e_spawner.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/state_array.hpp"

namespace od::port::st0e_spawner_state {

// the class's bytes +0x18..+0x1f (cleared by the init; no other reader)
struct EncounterSwitchFields : ActorClassState {
    explicit EncounterSwitchFields(ControllerActor&) {}
    StateArray<uint8_t, 8> scratch{};
};

} // namespace od::port::st0e_spawner_state
