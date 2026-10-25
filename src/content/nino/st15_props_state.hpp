// The class states of st15_props.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/field_alias.hpp"
#include "game/state_array.hpp"

namespace od::port::st15_props_state {

// ==== 0x5a3910 (ST17): the hovering machine ===========================================================================
// The machine's own bytes: its hit box, the dying's timer, the hurt flash, the fire stage, dead, the fires' slots
struct HoverMachineFields : ActorClassState {
    explicit HoverMachineFields(StaticActor& actor) : hitBox(actor.hitBox), dead(actor.gunshipPitchTarget()) {}
    HitBoxHead& hitBox;
    uint8_t& dead;
    uint8_t timer = 0;
    uint8_t hurtTicks = 0;
    uint8_t fireStage = 0;
    StateArray<uint8_t, 4> fires{};

    void clearBlock() override {
        timer = 0;
        hurtTicks = 0;
        fireStage = 0;
        fires = {};
    }
};

constexpr uint8_t kJoltAsked = 0x01;

// ==== 0x5af870 (ST19): the still solid / the hittable target ===========================================================
// The target's own bytes: its hit box, the regeneration timer (its init stores the two bytes), its home yaw, the jolt's
// timer, the damage meter (s8: an overflow costs a hit point), the jolt request
struct St19TargetFields : ActorClassState {
    explicit St19TargetFields(StaticActor& actor) : hitBox(actor.hitBox), joltTicks(actor.gunshipTimer()), jolt(actor.gunshipCooldown()) {}
    HitBoxHead& hitBox;
    uint8_t& joltTicks;
    uint8_t& jolt; // kJoltAsked
    int16_t regenTicks = 0;
    uint16_t homeYaw = 0;
    int8_t damageMeter = 0;
    PartField<uint8_t, int16_t> regenTicksLow{regenTicks, 0};
    PartField<uint8_t, int16_t> regenTicksHigh{regenTicks, 1};

    void clearBlock() override {
        regenTicks = 0;
        homeYaw = 0;
        damageMeter = 0;
    }
};

} // namespace od::port::st15_props_state
