// The class states of st45_spawner.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/field_alias.hpp"
#include "game/state_array.hpp"

namespace od::port::st45_spawner_state {

// The spawner's own bytes: its countdown (the spawn record's first parameter bytes) and the scratch the init clears
struct SpawnerFields : ActorClassState {
    explicit SpawnerFields(ControllerActor& actor) : countdown(actor.params, 0) {}
    BytesField<uint16_t, 4> countdown;
    StateArray<uint8_t, 8> scratch{};
};

} // namespace od::port::st45_spawner_state
