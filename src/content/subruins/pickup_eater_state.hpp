// The class states of pickup_eater.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"

namespace od::port::pickup_eater_state {

// Its own fields: its hit box (the hit's direction in its contact.x), the timer, the height / direction it keeps, its
// behaviour bits, this tick's contact bits, the loop it walks (its angle, centre, phase and yaw) and the pickup it runs
// to (its static pool index)
struct EaterFields : ActorClassState {
    explicit EaterFields(AnimatedActor& actor) : bits(actor.shownParts), target(actor.townBirdHits), loopPhase(actor.townBirdHitsHigh) {}
    uint8_t& bits; // kEater* behaviour bits
    uint8_t& target; // the pickup's static pool index
    uint8_t& loopPhase;
    HitBox hitBox;
    int16_t timer = 0;
    int16_t kept = 0; // the hop's height / the hit's direction
    uint8_t contact = 0; // kEater* contact bits of this tick
    int16_t loopAngle = 0;
    int16_t centreX = 0;
    int16_t centreZ = 0;
    int16_t loopYaw = 0;

    void clearBlock() override {
        hitBox = {};
        timer = 0;
        kept = 0;
        contact = 0;
        loopAngle = 0;
        centreX = 0;
        centreZ = 0;
        loopYaw = 0;
    }
};

} // namespace od::port::pickup_eater_state
