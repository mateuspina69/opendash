// The class states of town_props.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"

namespace od::port::town_props_state {

// ---- exe 0x58e0e0: the ST09 static type 1 (states 0x8cdb6c) ---------------------------------------------------------
// The kickable block's fields (the class block +0xbc..): its hit box head, then its own words in the contact lanes
struct St09PropFields : ActorClassState {
    explicit St09PropFields(StaticActor& actor) : hitBox(actor.hitBox) {}
    HitBoxHead& hitBox;
    uint16_t flags = 0; // kPropKicked | kPropFlying | kPropLifted
    uint16_t timer = 0; // the kick's ticks; the breakable's debris ticks
    int16_t travelYaw = 0; // the yaw it flies along
    int16_t playerAngle = 0; // the player's direction from it (-0x800 .. 0x7ff)

    void clearBlock() override {
        flags = 0;
        timer = 0;
        travelYaw = 0;
        playerAngle = 0;
    }
};

} // namespace od::port::town_props_state
