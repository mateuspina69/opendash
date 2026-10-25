// The class states of st54_takeoff.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/field_alias.hpp"

namespace od::port::st54_takeoff_state {

// The arc's own bytes: its start key and its climb rate (the spawn's parameters), the arc angle its pitch follows
struct TakeoffFields : ActorClassState {
    explicit TakeoffFields(AnimatedActor& actor) : startKey(actor.params[0]), fastClimb(actor.params[1]) {}
    FieldAlias<int8_t, uint8_t> startKey;
    uint8_t& fastClimb; // 0: the arc opens by 3 a tick, else 4
    int16_t arcAngle = 0;

    void clearBlock() override {
        arcAngle = 0;
    }
};

} // namespace od::port::st54_takeoff_state
