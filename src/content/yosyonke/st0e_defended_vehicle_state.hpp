// The class states of st0e_defended_vehicle.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/field_alias.hpp"

namespace od::port::st0e_defended_vehicle_state {

// the vehicle's bytes
struct DefendedFields : ActorClassState {
    explicit DefendedFields(StaticActor& actor) : round(actor.params[1]), wantedGap(actor.params, 2) {}
    uint8_t& round;
    BytesField<int16_t, 4> wantedGap; // the wanted x distance behind the boss vehicle
    uint8_t turretGroup = 0; // the turret's enemy-group index
    uint8_t timer = 0;
    uint8_t voicesSaid = 0;

    void clearBlock() override {
        turretGroup = 0;
        timer = 0;
        voicesSaid = 0;
    }
};

} // namespace od::port::st0e_defended_vehicle_state
