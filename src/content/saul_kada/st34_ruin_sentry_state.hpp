// The class states of st34_ruin_sentry.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/field_alias.hpp"

namespace od::port::st34_ruin_sentry_state {

// The sentry's own bytes (0x20 cleared at the bind): its hit box (its contact angle kept by a compared-height hit),
// its timer, a counter (turn steps / shots / a target yaw), the step its routine resumes after a stagger and the
// resume request, its flags, its march timer, its walls' contact, its stagger count, its pose frozen, its enemy group,
// the drop point (x / z)
struct SentryFields : ActorClassState {
    explicit SentryFields(AnimatedActor& actor) : resumeStep(actor.shownParts), flags(actor.hopperBytes[0]), marchTimer(actor.hopperBytes[1]), contact(actor.hopperBytes[2]), frozen(actor.hopperBytes[4]), group(actor.hopperBytes[5]) {}
    uint8_t& resumeStep;
    uint8_t& flags;
    uint8_t& marchTimer;
    uint8_t& contact;
    int8_t staggerCount = 0;
    uint8_t& frozen;
    uint8_t& group;
    HitBox hitBox;
    uint16_t timer = 0;
    uint16_t counter = 0;
    uint8_t resume = 0;
    int16_t dropX = 0;
    int16_t dropZ = 0;
    PartField<uint8_t, uint16_t> timerLow{timer, 0};

    void clearBlock() override {
        staggerCount = 0;
        hitBox = {};
        timer = 0;
        counter = 0;
        resume = 0;
        dropX = 0;
        dropZ = 0;
    }
};

} // namespace od::port::st34_ruin_sentry_state
