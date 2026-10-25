// The class states of leaping_critter.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"

namespace od::port::leaping_critter_state {

// Its own fields: the path leg's limit / the turn's rate (its velocity's spare lane), the waddle's phase (its
// acceleration's spare lane) and its class block
struct CritterFields : ActorClassState {
    explicit CritterFields(AnimatedActor& actor) : pathLimit(actor.velocity.spare), turnRate(actor.velocity.spare), waddlePhase(actor.acceleration.spare), bits(actor.shownParts), cooldown(actor.hopperBytes[4]), hits(actor.hopperBytes[5]) {}
    int16_t& pathLimit; // the path leg's end on its axis
    int16_t& turnRate; // the turn's yaw a tick
    int16_t& waddlePhase;
    uint8_t& bits; // kCritter* behaviour bits
    uint8_t& cooldown; // ticks after a leap
    uint8_t& hits;
    HitBox hitBox; // its contact.x: the hit's direction
    int16_t timer = 0;
    int16_t targetYaw = 0;
    uint8_t contact = 0; // kCritter* contact bits of this tick
    int16_t nearPatience = 0; // ticks left with the player near
    int16_t visiblePatience = 0; // ticks left seen
    uint32_t pathCursor = 0; // the path's next (yaw, limit) pair (exe data)

    void clearBlock() override {
        hitBox = {};
        timer = 0;
        targetYaw = 0;
        contact = 0;
        nearPatience = 0;
        visiblePatience = 0;
        pathCursor = 0;
    }
};

} // namespace od::port::leaping_critter_state
