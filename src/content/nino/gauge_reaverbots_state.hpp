// The class states of gauge_reaverbots.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/vectors.hpp"
#include "game/field_alias.hpp"

namespace od::port::gauge_reaverbots_state {

// The mine's class block: its hit box, its life, its contact and mode bytes
struct MineFields : ActorClassState {
    explicit MineFields(AnimatedActor&) {}
    HitBoxHead hit;
    int16_t ticks = 0;
    uint8_t contact = 0;
    uint8_t mode = 0;

    void clearBlock() override {
        hit = {};
        ticks = 0;
        contact = 0;
        mode = 0;
    }
};

struct CastFields : ActorClassState {
    explicit CastFields(AnimatedActor& actor) : velocitySpare(actor.velocity.spare), accelerationSpare(actor.acceleration.spare) {}
    int16_t& velocitySpare;
    int16_t& accelerationSpare;
    uint8_t animFlags = 0; // bit 0: set the animation again
    uint8_t startKey = 0;
    uint8_t glow = 0;

    void clearBlock() override {
        animFlags = 0;
        startKey = 0;
        glow = 0;
    }
};

// The reaverbot's own values: its home (the rotation's / the velocity's spare lanes), the point it circles (the
// acceleration's lanes) and its class block
struct GaugeFields : ActorClassState {
    explicit GaugeFields(AnimatedActor& actor) : homeX(actor.rotation.spare), homeZ(actor.velocity.spare), contact(actor.hopperBytes[2]), mode(actor.hopperBytes[3]), startKey(actor.hopperBytes[4]), glow(actor.hopperBytes[5]) {}
    int16_t& homeX;
    int16_t& homeZ;
    uint8_t& contact;
    uint8_t& mode;
    uint8_t& startKey;
    uint8_t& glow;
    int8_t glowStep = 0;
    ShortVec3 circlePoint;
    HitBoxHead hit;
    int16_t ticks = 0;
    int16_t counter = 0; // the wall back-off's ticks / the spin back's yaw / the burst's shot speed
    int16_t startY = 0;

    void clearBlock() override {
        glowStep = 0;
        hit = {};
        ticks = 0;
        counter = 0;
        startY = 0;
    }
};

} // namespace od::port::gauge_reaverbots_state
