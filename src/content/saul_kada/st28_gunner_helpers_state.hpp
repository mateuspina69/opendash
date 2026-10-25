// The class states of st28_gunner_helpers.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"

namespace od::port::st28_gunner_helpers_state {

// The helper's own bytes: its boss, its hit box (its contact angle: the hit's yaw), the hit's yaw kept, a timer, a
// counter, its neighbour word, its flags, its walls' contact, its hit entry's kind, the sub-state it takes at a ledge
struct HelperFields : ActorClassState {
    explicit HelperFields(AnimatedActor& actor) : flags(actor.townBirdHits), contact(actor.townBirdHitsHigh), entryKind(actor.hopperTimer) {}
    uint8_t& flags;
    uint8_t& contact;
    uint8_t& entryKind;
    uint32_t boss = 0;
    HitBox hitBox;
    uint16_t hitYaw = 0;
    uint16_t timer = 0;
    int16_t counter = 0;
    uint16_t neighbour = 0;
    uint8_t ledgeNext = 0;

    void clearBlock() override {
        boss = 0;
        hitBox = {};
        hitYaw = 0;
        timer = 0;
        counter = 0;
        neighbour = 0;
        ledgeNext = 0;
    }
};

} // namespace od::port::st28_gunner_helpers_state
