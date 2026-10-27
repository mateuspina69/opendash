// The class states of st28_flyer.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/field_alias.hpp"
#include "game/state_array.hpp"

namespace od::port::st28_flyer_state {

// The flyer's own bytes: its hit box, the hits left before a stun, its timer, its sway and bob angles, its turn speed,
// its flags, the bob's carry, whether its drop gives pickups, the key entry of its next animation
struct FlyerFields : ActorClassState {
    explicit FlyerFields(AnimatedActor& actor) : flagsLow(actor.hopperBytes[4]), bobCarry(actor.townBirdHits), dropsPickups(actor.townBirdHitsHigh), animEntry(actor.hopperTimer), velocity(actor.velocity) {}
    uint8_t& flagsLow;
    uint8_t& bobCarry;
    uint8_t& dropsPickups;
    uint8_t& animEntry;
    ShortVec4& velocity; // the slot's velocity (the appearance's byte stores over its x / y / z)
    HitBox hitBox; // its shape asks for the contact output: the hit list writes it after the head
    uint16_t stunHits = 0;
    int16_t timer = 0;
    int16_t swayAngle = 0;
    int16_t bobAngle = 0;
    int16_t spin = 0;
    uint16_t flags = 0;
    PartField<uint8_t, int16_t> timerLow{timer, 0};
    // the velocity's six bytes stored one at a time: 0, 0, 0, 0, 0, 0xff (x 0, y 0, z -0x100)
    void setAppearVelocity() {
        velocity.x = 0;
        velocity.y = 0;
        velocity.z = int16_t(0xff00);
    }

    void clearBlock() override {
        hitBox = {};
        stunHits = 0;
        timer = 0;
        swayAngle = 0;
        bobAngle = 0;
        spin = 0;
        flags = 0;
    }
};

} // namespace od::port::st28_flyer_state
