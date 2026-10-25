// The class states of reflector.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/shots/shot_common.hpp"
#include "game/field_alias.hpp"
#include "game/state_array.hpp"

namespace od::port::reflector_state {

constexpr uint32_t kSpraySparks = 0xc;
// one spark: its direction (pitch falling toward the ground, yaw, roll) and its speed
struct SpraySpark {
    int16_t pitch = 0;
    int16_t yaw = 0;
    int16_t roll = 0;
    int16_t speed = 0;
};

// ---- the reflector ----------------------------------------------------------------------------------------------------
// Its block +0xbc: the stats, the deflection's cooldown, the floor skid's ticks
struct ReflectorFields : ActorClassState {
    explicit ReflectorFields(ShotActor&) {}
    ShotStats stats;
    uint16_t deflectTicks = 0;
    uint16_t skidTicks = 0;
};

// The spray's fields: the direction it sprays (the spawn's parameters: pitch, yaw), its ticks left (the header's step
// counter) and the sparks
struct SparkSprayFields : ActorClassState {
    explicit SparkSprayFields(StaticActor& actor)
        : pitch(actor.params, 0), yaw(actor.params, 2), ticks(actor.stepCounter) {}
    BytesField<uint16_t, 4> pitch;
    BytesField<uint16_t, 4> yaw;
    uint8_t& ticks;
    StateArray<SpraySpark, kSpraySparks> sparks{};

    void clearBlock() override {
        sparks[10].roll = 0;
        sparks[10].speed = 0;
        sparks[11] = {};
    }
};

} // namespace od::port::reflector_state
