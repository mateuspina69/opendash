// The carrier boat of the ST17 mission (static type 0x21, exe 0x5a5ea0), content/nino/carrier_boat.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// The boat's busy byte (on a trip; the ST17 walker spawners read it: content/nino/field_spawners)
// The boat's busy byte (on a trip; the ST17 walker spawners read it: content/nino/field_spawners)
struct CarrierBoatParams {
    explicit CarrierBoatParams(const StaticActor& actor) : CarrierBoatParams(const_cast<StaticActor&>(actor)) {}
    explicit CarrierBoatParams(StaticActor& actor) : busy(actor.params[0]) {}
    uint8_t& busy;
};

// exe 0x5a5ea0: variant 0 the mission's carrier boat (comes in, waits, leaves, rams; its guns; sunk: the mission's end,
// id 9 defeated); variant 1 the boat that brings walkers during script 0x3d
void carrierBoatUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
