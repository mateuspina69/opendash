// The class states of snow.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/weather_draw.hpp"
#include "game/field_alias.hpp"
#include "game/state_array.hpp"

namespace od::port::snow_state {

constexpr uint8_t kDefaultPeriod = 0x1e;

// The class's bytes: the wind, the respawn period, the live mask (bit k = particle k), the particles (u32 {x + 0xb4 :
// 9, y : 9, z : 10}: screen space around the camera) and the respawn countdown
struct SnowFields : ActorClassState {
    explicit SnowFields(StaticActor& actor) : windX(actor.params[0]), windY(actor.params[1]), windZ(actor.params[2]), period(actor.params[3]), countdown(actor.cullWidth, 0) {}
    FieldAlias<int8_t, uint8_t> windX;
    FieldAlias<int8_t, uint8_t> windY;
    FieldAlias<int8_t, uint8_t> windZ;
    uint8_t& period; // 0: kDefaultPeriod
    PartField<uint8_t, int16_t> countdown;
    uint32_t live = 0;
    StateArray<uint32_t, kSnowParticles> particles{};
};

} // namespace od::port::snow_state
