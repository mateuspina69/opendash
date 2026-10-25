// The class states of carrier_boat.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/state_array.hpp"

namespace od::port::carrier_boat_state {

// The boat's own bytes: busy on a trip (its rocking waits), the engine sound's ticks (the velocity's spare lane), the
// hit box, the gun cycle, the ram, the trip, the hurt flash, the damage stage, a timer, the two fires' slots
struct BoatFields : ActorClassState {
    explicit BoatFields(StaticActor& actor) : busy(actor.params[0]), engineTicks(actor.velocity.spare), hitBox(actor.hitBox), hurtTicks(actor.gunshipPitchTarget()), damage(actor.gunshipTimer()) {}
    uint8_t& busy;
    int16_t& engineTicks;
    HitBoxHead& hitBox;
    uint8_t& hurtTicks;
    uint8_t& damage; // BoatDamage
    uint8_t gunCycle = 0;
    uint8_t ram = 0; // kRamming: the bow's hit entry this tick
    uint8_t trip = 0; // BoatTrip
    uint8_t timer = 0;
    StateArray<uint8_t, 2> fires{}; // the fires' slots

    void clearBlock() override {
        gunCycle = 0;
        ram = 0;
        trip = 0;
        timer = 0;
        fires = {};
    }
};

} // namespace od::port::carrier_boat_state
