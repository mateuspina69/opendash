// The class states of blade_trap.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/field_alias.hpp"
#include "game/state_array.hpp"

namespace od::port::blade_trap_state {

// The trap's own values: the Actor lanes it keeps them in (the travel yaw in the rotation's spare lane, the blade's
// spin / the bob / the speed and their step in the velocity, the countdowns, the blade's radius and the scale offset
// in the acceleration, the leg left, the contact bits and the mode in the spin) and its class block (two hit boxes)
struct TrapFields : ActorClassState {
    explicit TrapFields(StaticActor& actor) : travelYaw(actor.rotation.spare), spinSpeed(actor.velocity.x), bob(actor.velocity.y), speed(actor.velocity.z), stepSize(actor.velocity.spare), timer(actor.acceleration.x), partsLeft(actor.acceleration.y), bladeRadius(actor.acceleration.z), scaleOffset(actor.acceleration.spare), legLeft(actor.spin.x), contact(actor.spin.y, 0), mode(actor.spin.y, 1), hit(actor.hitBox) {}
    int16_t& travelYaw;
    int16_t& spinSpeed;
    int16_t& bob;
    int16_t& speed;
    int16_t& stepSize;
    int16_t& timer; // a countdown / the yaw aimed at
    int16_t& partsLeft; // the parts of the unfold / fold still moving; the charge's countdown
    int16_t& bladeRadius;
    int16_t& scaleOffset;
    FieldAlias<uint16_t, int16_t> legLeft;
    PartField<uint8_t, int16_t> contact; // kTouched*
    PartField<uint8_t, int16_t> mode; // kTrap*
    HitBoxHead& hit; // its body closed, the blade's edge armed
    StateArray<uint8_t, 4> clearedPad0{}; // bytes the block clear covers (no reader)
    HitBoxHead armedHit; // its body while the blade is out
    void clearLanesBytes() {
        clearedPad0 = {};
        timer = 0;
        partsLeft = 0;
        bladeRadius = 0;
        scaleOffset = 0;
        legLeft = 0;
        contact = 0;
        mode = 0;
    }
    void clearBlockBytes() {
        armedHit = {};
        hit = {};
    }

    void clearBlock() override {
        armedHit = {};
    }
};

} // namespace od::port::blade_trap_state
