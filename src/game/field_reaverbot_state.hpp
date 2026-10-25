// The class states of field_reaverbot.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/field_alias.hpp"

namespace od::port::field_reaverbot_state {

// The reaverbot's own fields: its patrol's leg (a spawn parameter), the patrol's heading (its rotation's spare lane),
// the home point it walks back to (its spin's z / spare lanes) and its class block
struct FieldReaverbotFields : ActorClassState {
    explicit FieldReaverbotFields(AnimatedActor& actor) : patrolCells(actor.params[1]), homeYaw(actor.rotation.spare), homeX(actor.spin.z), homeZ(actor.spin.spare), behaviour(actor.shownParts), bits(actor.hopperBytes[0]), cooldown(actor.hopperBytes[4]) {}
    FieldAlias<int8_t, uint8_t> patrolCells; // the patrol's leg (in its cell counts)
    int16_t& homeYaw; // the patrol's heading (its rotation's spare lane)
    int16_t& homeX; // the home point (its spin's z / spare lanes)
    int16_t& homeZ;
    uint8_t& behaviour; // the next sub-state's row [0x88eb1c]
    uint8_t& bits; // kField* behaviour bits
    int8_t hitsLeft = 0; // the flinches before a knock-back
    uint8_t& cooldown; // ticks before the next lunge / shot
    int8_t farTicks = 0; // ticks left far from the player before it sinks away
    int8_t group = 0; // its enemy-group entry (exe 0x5e6cd0)
    HitBox hitBox; // its contact.x: the hit's direction
    int16_t timer = 0;
    int16_t heading = 0; // the knock's direction / the patrol's leg count / the wall turn's rate
    uint8_t contact = 0; // kField* contact bits of this tick
    uint16_t neighbour = 0; // game/enemy_neighbours

    void clearBlock() override {
        hitsLeft = 0;
        farTicks = 0;
        group = 0;
        hitBox = {};
        timer = 0;
        heading = 0;
        contact = 0;
        neighbour = 0;
    }
};

} // namespace od::port::field_reaverbot_state
