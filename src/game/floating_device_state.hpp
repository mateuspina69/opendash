// The class states of floating_device.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"

namespace od::port::floating_device_state {

// The device's own fields: its spawn parameters and its class block
struct FloatingDeviceFields : ActorClassState {
    explicit FloatingDeviceFields(AnimatedActor& actor) : leadsOut(actor.params[0]), scaleSteps(actor.params[1]), spin(actor.params[2]), message(actor.params[3]) {}
    uint8_t& leadsOut; // non-zero: the examine is the event that leads out (then freed)
    uint8_t& scaleSteps; // its scale (<< 5: the model scale)
    uint8_t& spin; // its yaw step per tick
    uint8_t& message; // the examine's message
    int16_t baseHeight = 0; // the height it bobs around
    uint16_t bobPhase = 0; // the bob's 64-step phase
    uint8_t talking = 0; // a talk runs

    void clearBlock() override {
        baseHeight = 0;
        bobPhase = 0;
        talking = 0;
    }
};

} // namespace od::port::floating_device_state
