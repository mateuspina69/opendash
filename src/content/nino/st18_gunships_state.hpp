// The class states of st18_gunships.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/gunship_flight.hpp"
#include "game/field_alias.hpp"

namespace od::port::st18_gunships_state {

// The fight gunship's own bytes: its orders (1: to the square when near it, 2: at once), its shield (off the lock-on),
// the orbit (the acceleration's spare lane: its offset from the bearing to the centre, << 4) and its step (the turn
// step's word), the orbit's radius / the last bearing (the heading's word)
// The fight gunship's own bytes: its orders (1: to the square when near it, 2: at once), its shield (off the lock-on),
// the orbit (the acceleration's spare lane: its offset from the bearing to the centre, << 4) and its step (the turn
// step's word), the orbit's radius / the last bearing (the heading's word)
struct FightGunshipFields : GunshipFields {
    explicit FightGunshipFields(const StaticActor& actor) : FightGunshipFields(const_cast<StaticActor&>(actor)) {}
    explicit FightGunshipFields(StaticActor& actor)
        : GunshipFields(actor), orders(actor.params[0]), shielded(actor.params[1]), orbit(actor.acceleration.spare),
          orbitStep(actor.spin.spare), orbitRadius(actor.velocity.spare), lastBearing(actor.velocity.spare) {}
    FieldAlias<int8_t, uint8_t> orders; // ShipFightGunshipParams (st18_gunships.hpp)
    uint8_t& shielded;
    int16_t& orbit;
    int16_t& orbitStep;
    int16_t& orbitRadius;
    int16_t& lastBearing;
};

} // namespace od::port::st18_gunships_state
