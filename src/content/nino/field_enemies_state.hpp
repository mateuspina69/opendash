// The class states of field_enemies.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "content/nino/field_enemies.hpp"
#include "game/field_alias.hpp"
#include "game/state_array.hpp"

namespace od::port::field_enemies_state {

// The critter's fields: part 1's tilt (the rotation's spare lane / the acceleration's: the decoded frame's), the yaw
// it is thrown along (the velocity's spare lane); its class block: the hit box (contact x: the hit's direction), its
// behaviour bits, the wander's tick, its contact bits, the animation's start key, the drop-in's tick, the off-screen
// ticks, the stagger meter
struct CritterFields : ActorClassState {
    explicit CritterFields(AnimatedActor& actor) : knockedMark(actor.params[3]), tiltY(actor.rotation.spare), flightYaw(actor.velocity.spare), tiltX(actor.acceleration.spare), behaviour(actor.hopperBytes[0]), wanderTicks(actor.hopperBytes[2]), contact(actor.hopperBytes[3]), animKeyStart(actor.hopperBytes[4]), dropTicks(actor.hopperBytes[5]), offScreenTicks(actor.hopperTimer) {}
    uint8_t& knockedMark; // cleared when it gets up from a knock (no reader traced)
    int16_t& tiltY;
    int16_t& flightYaw;
    int16_t& tiltX;
    uint8_t& behaviour; // kCritter*
    uint8_t& wanderTicks;
    uint8_t& contact; // kCritterHit*
    uint8_t& animKeyStart;
    uint8_t& dropTicks;
    uint8_t& offScreenTicks;
    HitBox hitBox;
    uint8_t stagger = 0;

    void clearBlock() override {
        hitBox = {};
        stagger = 0;
    }
};

// The carrier's fields: its spawn parameters (field_enemies.hpp) and carry bits; its class block: the hit box, the
// brake distance (the dying: the yaw it falls along), its riders' slots (one word for the "any rider" test), the box's
// index (in the static pool), its contact bits, a timer, its home, the damage meter, its enemy group, the footsteps'
// ticks
struct CarrierFields : BoxCarrierGrip {
    explicit CarrierFields(const AnimatedActor& actor) : CarrierFields(const_cast<AnimatedActor&>(actor)) {}
    explicit CarrierFields(AnimatedActor& actor) : BoxCarrierGrip(actor), contact(actor.hopperBytes[0]), timer(actor.hopperBytes[1]), damage(actor.townBirdHits), group(actor.townBirdHitsHigh), footstepTicks(actor.hopperTimer) {}
    uint8_t& contact; // kCarrierHit*
    uint8_t& timer;
    uint8_t& damage;
    uint8_t& group;
    uint8_t& footstepTicks;
    HitBox hitBox; // its shape asks for the contact output: the hit list writes it after the head
    union {
        int16_t brakeDistance = 0;
        int16_t fallYaw;
    };
    union {
        StateArray<uint8_t, 2> riders{};
        int16_t ridersWord;
    };
    uint8_t boxIndex = 0;
    int16_t homeX = 0;
    int16_t homeZ = 0;

    void clearBlock() override {
        BoxCarrierGrip::clearBlock();
        hitBox = {};
        brakeDistance = 0;
        riders = {};
        boxIndex = 0;
        homeX = 0;
        homeZ = 0;
    }
};

} // namespace od::port::field_enemies_state
