// The class states of square_gunship.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/gunship_flight.hpp"
#include "game/field_alias.hpp"

namespace od::port::square_gunship_state {

// The square gunship's own bytes: the story flag it sets when it settles, its rock timer, the dive's pitch and height,
// the bombing cooldown (the roll target's word, which the attitude also writes)
// The square gunship's own bytes: the story flag it sets when it settles, its rock timer, the dive's pitch and height,
// the bombing cooldown (the roll target's word, which the attitude also writes)
struct SquareGunshipFields : GunshipFields {
    explicit SquareGunshipFields(const StaticActor& actor) : SquareGunshipFields(const_cast<StaticActor&>(actor)) {}
    explicit SquareGunshipFields(StaticActor& actor)
        : GunshipFields(actor), settleFlag(actor.params, 0), rockTicks(actor.params[3]), divePitch(actor.velocity.spare),
          diveHeight(actor.acceleration.spare), bombCooldown(actor.rotation.spare) {}
    BytesField<uint16_t, 4> settleFlag;
    uint8_t& rockTicks;  // a strong hit's: no hits taken while it runs
    int16_t& divePitch;  // the heading's word during the dive
    int16_t& diveHeight;
    int16_t& bombCooldown;
};

} // namespace od::port::square_gunship_state
