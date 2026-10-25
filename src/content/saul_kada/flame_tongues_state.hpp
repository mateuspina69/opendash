// The class states of flame_tongues.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/field_alias.hpp"
#include "game/state_array.hpp"

namespace od::port::flame_tongues_state {

// The tongues' own bytes. The spawn's s16 point (x / y / z at +0x10..) and yaw (+0x16) are read at the start; then:
// the growth and the height (<< 4), the two tongues' velocities (x / z), their x / z, their shapes, their tilts
struct TongueFields : ActorClassState {
    explicit TongueFields(EffectActor& actor) : colour(actor.params, 0), spawnX(actor.pos.x), spawnY(actor.pos.y), spawnZ(actor.pos.z), spawnYaw(actor.posSpare), growth(actor.pos.x), height(actor.pos.y) {}
    BytesField<uint32_t, 4> colour; // bit 31: opaque, else semi-transparent (blend (colour >> 24) & 3)
    int16_t& spawnX;
    int16_t& spawnY;
    int16_t& spawnZ;
    int16_t& spawnYaw;
    int16_t& growth;
    int16_t& height;
    StateArray<int16_t, 2> velocityX{};
    StateArray<int16_t, 2> x{};
    StateArray<int16_t, 2> z{};
    StateArray<int16_t, 2> velocityZ{};
    StateArray<uint8_t, 2> shapes{};
    int8_t tiltB = 0;
    int8_t tiltA = 0;
};

} // namespace od::port::flame_tongues_state
