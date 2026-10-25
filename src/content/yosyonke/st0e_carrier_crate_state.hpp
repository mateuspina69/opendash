// The class states of st0e_carrier_crate.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/field_alias.hpp"

namespace od::port::st0e_carrier_crate_state {

// the crate's bytes
struct CrateFields : ActorClassState {
    explicit CrateFields(StaticActor& actor) : servbot(actor.params, 0), box(actor.hitBox), throwTarget(actor.gunshipCooldown()), ringSlot(actor.gunshipAttackCooldown()) {}
    BytesField<uint32_t, 4> servbot; // the Servbot inside (kNoServbot: none)
    HitBoxHead& box;
    uint8_t& throwTarget; // the slot it was thrown at (0: none)
    uint8_t& ringSlot; // its trail ring's slot (0: none)
    uint8_t kind = 0; // kCrate*: what its hit entry is
    uint16_t fallTicks = 0; // the ticks before it aims (thrown: before it breaks)
    uint16_t life = 0;

    void clearBlock() override {
        kind = 0;
        fallTicks = 0;
        life = 0;
    }
};

} // namespace od::port::st0e_carrier_crate_state
