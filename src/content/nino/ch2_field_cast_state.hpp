// The class states of ch2_field_cast.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"

namespace od::port::ch2_field_cast_state {

// =================================================================================================================
// the hovering target 0x49d1d0 / the hovering drifter 0x49de80
// =================================================================================================================
// The hovering target's fields: its hit box, its smoke stage, the hurt flash, the smoke stage just changed, the alarm
// sent
struct HoverFields : ActorClassState {
    explicit HoverFields(AnimatedActor& actor) : alarmSent(actor.guardianBits) {}
    uint8_t& alarmSent;
    HitBoxHead hitBox;
    uint8_t smokeStage = 0; // 0 none, 1 below half, 2 below a quarter
    uint8_t hurtTicks = 0;
    uint8_t smokeChanged = 0;

    void clearBlock() override {
        hitBox = {};
        smokeStage = 0;
        hurtTicks = 0;
        smokeChanged = 0;
    }
};

// =================================================================================================================
// the crashing carrier 0x4d2e80
// =================================================================================================================
// The crashing carrier's fields (the box carrier's layout, content/nino/field_enemies): the yaw it falls along, its carry
// bits, its contact bits, a timer, its home, its group
struct CrashCarrierFields : ActorClassState {
    explicit CrashCarrierFields(AnimatedActor& actor) : carry(actor.shownParts), contact(actor.hopperBytes[0]), timer(actor.hopperBytes[1]), group(actor.townBirdHitsHigh) {}
    uint8_t& carry;
    uint8_t& contact; // kCrashHit*
    uint8_t& timer;
    uint8_t& group;
    int16_t fallYaw = 0;
    int16_t homeX = 0;
    int16_t homeZ = 0;

    void clearBlock() override {
        fallYaw = 0;
        homeX = 0;
        homeZ = 0;
    }
};

// =================================================================================================================
// the scripted walker 0x4ea500
// =================================================================================================================
// The walker's timer
struct WalkerFields : ActorClassState {
    explicit WalkerFields(AnimatedActor&) {}
    uint8_t timer = 0;

    void clearBlock() override {
        timer = 0;
    }
};

} // namespace od::port::ch2_field_cast_state
