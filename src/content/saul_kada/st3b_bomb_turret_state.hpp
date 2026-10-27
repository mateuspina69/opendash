// The class states of st3b_bomb_turret.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"

namespace od::port::st3b_bomb_turret_state {

// Its own bytes: its hit box, the gun's yaw offset (from its yaw), the bomb's aim above the player, a timer
struct TurretFields : ActorClassState {
    explicit TurretFields(AnimatedActor& actor) : timer(actor.shownParts) {}
    uint8_t& timer;
    HitBox hitBox; // its shape asks for the contact output: the hit list writes it after the head
    int16_t yawOffset = 0;
    int16_t bombHeight = 0;

    void clearBlock() override {
        hitBox = {};
        yawOffset = 0;
        bombHeight = 0;
    }
};

} // namespace od::port::st3b_bomb_turret_state
