// The class states of st15_spray.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/state_array.hpp"
#include "game/vectors.hpp"
#include "game/field_alias.hpp"

namespace od::port::st15_spray_state {

// One puff (0x18 bytes): its state, animation frame, age, brightness, hit box, s16 position, yaw and speeds
struct SprayPuff {
    uint8_t state = 0; // PuffState
    uint8_t frame = 0;
    uint8_t age = 0;
    uint8_t brightness = 0;
    HitBoxHead hitBox;
    ShortVec3 pos;
    int16_t yaw = 0;
    int16_t riseSpeed = 0; // y of its move along the yaw
    int16_t forwardSpeed = 0; // z of its move along the yaw
};

// The spray's fields over the slot (+0x74..): the three puffs and the count of those alive
struct SprayFields : ActorClassState {
    explicit SprayFields(ProjectileActor&) {}
    StateArray<SprayPuff, 3> puffs{};
    uint8_t puffCount = 0;
    StateArray<uint8_t, 31> clearedPad0{}; // bytes the block clear covers (no reader)
    void clear() {
        puffs = {};
        puffCount = 0;
        clearedPad0 = {};
    }
};

} // namespace od::port::st15_spray_state
