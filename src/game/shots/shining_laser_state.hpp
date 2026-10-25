// The class states of shining_laser.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/field_alias.hpp"

namespace od::port::shining_laser_state {

// The wall spark's fields: its size (the spawner's; then its brightness, size * 3), its disc's size and shrink, the
// fade's ticks
struct LaserWallSparkFields : ActorClassState {
    explicit LaserWallSparkFields(EffectActor& actor) : size(actor.params, 0) {}
    BytesField<uint16_t, 4> size;
    uint16_t discSize = 0;
    uint16_t shrink = 0;
    uint8_t fadeTicks = 0;
};

} // namespace od::port::shining_laser_state
