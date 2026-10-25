// The class states of st41_carrier_pod.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/field_alias.hpp"

namespace od::port::st41_carrier_pod_state {

// The pod's bytes: its anchor (x / y / z in the velocity's, the acceleration's and the rotation's spare lanes), the
// sway's phase (the spin's spare lane); its class block: its carrier, two timers, its glow's mode and level, its hit box
struct PodFields : ActorClassState {
    explicit PodFields(StaticActor& actor) : anchorX(actor.velocity.spare), anchorY(actor.acceleration.spare), anchorZ(actor.rotation.spare), phase(actor.spin.spare) {}
    int16_t& anchorX;
    int16_t& anchorY;
    int16_t& anchorZ;
    int16_t& phase;
    ActorRef<AnimatedActor> carrier;
    union {
        uint8_t timer = 0;
        int8_t timerSigned;
    };
    uint8_t amplitude = 0;
    uint8_t glowMode = 0;
    int8_t glowLevel = 0;
    HitBoxHead hitBox;
    void clearBytes() {
        timer = 0;
        amplitude = 0;
        glowMode = 0;
        glowLevel = 0;
        hitBox = {};
    }

    void clearBlock() override {
        carrier = nullptr;
        timer = 0;
        amplitude = 0;
        glowMode = 0;
        glowLevel = 0;
        hitBox = {};
    }
};

} // namespace od::port::st41_carrier_pod_state
