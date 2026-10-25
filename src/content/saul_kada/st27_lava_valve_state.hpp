// The class states of st27_lava_valve.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "content/saul_kada/st27_lava_valve.hpp"
#include "game/field_alias.hpp"

namespace od::port::st27_lava_valve_state {

// The valve's own bytes: its hit box, the regeneration's ticks, its home yaw, the shake's ticks, the damage fraction
struct ValveFields : ActorClassState, LavaValveCues {
    explicit ValveFields(StaticActor& actor) : LavaValveCues(actor), hitBox(actor.hitBox), shakeTicks(actor.gunshipTimer()) {}
    HitBoxHead& hitBox;
    uint8_t& shakeTicks;
    uint16_t regenTicks = 0;
    int16_t homeYaw = 0;
    int8_t damageFraction = 0;

    void clearBlock() override {
        regenTicks = 0;
        homeYaw = 0;
        damageFraction = 0;
    }
};

} // namespace od::port::st27_lava_valve_state
