// The class states of enemy_spawners.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/field_alias.hpp"
#include "game/state_array.hpp"

namespace od::port::enemy_spawners_state {

// The spawners' own bytes: their point (the spawn record's), and the class's bytes from +0x18: the stage's story flag
// as read at the start, the side of the player (ST26), the flame walls' countdown / the block's marker shown (ST3B)
struct SpawnerFields : ActorClassState {
    explicit SpawnerFields(ControllerActor& actor) : x(actor.recordPoint.x), y(actor.recordPoint.y), z(actor.recordPoint.z) {}
    int16_t& x;
    int16_t& y;
    int16_t& z;
    int16_t countdown = 0;
    StateArray<uint8_t, 6> clearedPad0{}; // bytes the block clear covers (no reader)
    PartField<uint8_t, int16_t> flagSet{countdown, 0};
    PartField<uint8_t, int16_t> markerShown{countdown, 0};
    PartField<uint8_t, int16_t> side{countdown, 1};
    void clearBytes() {
        countdown = 0;
        clearedPad0 = {};
    }
};

} // namespace od::port::enemy_spawners_state
