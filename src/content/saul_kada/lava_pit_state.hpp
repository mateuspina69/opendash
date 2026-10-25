// The class states of lava_pit.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"

namespace od::port::lava_pit_state {

// The controllers' own bytes: the creature, the spot it was last put at
struct PitFields : ActorClassState {
    explicit PitFields(ControllerActor&) {}
    uint32_t creature = 0;
    uint8_t lastSpot = 0;
};

// The flame's own bytes: its hit box, the ticks it hurts, its brightness, its size, its frame's texel
struct FlameFields : ActorClassState {
    explicit FlameFields(StaticActor& actor) : hitBox(actor.hitBox) {}
    HitBoxHead& hitBox;
    uint16_t burnTicks = 0;
    int16_t brightness = 0;
    int16_t size = 0;
    int16_t frameU = 0;

    void clearBlock() override {
        burnTicks = 0;
        brightness = 0;
        size = 0;
        frameU = 0;
    }
};

// The creature's own bytes: its hit box, a timer; its hidden flag (no hit entry) is the header's step counter
struct CreatureFields : ActorClassState {
    explicit CreatureFields(StaticActor& actor) : hidden(actor.stepCounter), velocitySpare(actor.velocity.spare), hitBox(actor.hitBox) {}
    uint8_t& hidden;
    int16_t& velocitySpare; // cleared at the bind (its reader is not traced)
    HitBoxHead& hitBox;
    uint16_t ticks = 0;

    void clearBlock() override {
        ticks = 0;
    }
};

} // namespace od::port::lava_pit_state
