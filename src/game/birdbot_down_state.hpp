// The class states of birdbot_down.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/birdbot.hpp"
#include "game/field_alias.hpp"

namespace od::port::birdbot_down_state {

// The class block: the bounce's heading, the timers, its last hit and the smoke while bounced
struct DownFields : BirdbotBody {
    explicit DownFields(const AnimatedActor& actor) : DownFields(const_cast<AnimatedActor&>(actor)) {}
    explicit DownFields(AnimatedActor& actor) : BirdbotBody(actor), timer(actor.guardianBits), turnTimer(actor.shownParts), offScreen(actor.hopperBytes[0]), animKeyStart(actor.hopperBytes[1]), hitKind(actor.hopperBytes[4]), smoking(actor.townBirdHits) {}
    uint8_t& timer;
    uint8_t& turnTimer; // the run's turn back at a wall
    uint8_t& offScreen; // ticks left off screen
    uint8_t& animKeyStart;
    uint8_t& hitKind; // the last hit: 1, 2 from far below
    uint8_t& smoking; // smoke while bounced
    HitBox hitBox; // its contact: where the hit came from
    int16_t heading = 0; // the bounce's heading

    void clearBlock() override {
        BirdbotBody::clearBlock();
        hitBox = {};
        heading = 0;
    }
};

} // namespace od::port::birdbot_down_state
