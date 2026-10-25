// The class states of ruin_swimmers.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/field_alias.hpp"
#include "game/state_array.hpp"

namespace od::port::ruin_swimmers_state {

// The big swimmer's own values: the lunge's yaw (the velocity's spare lane) and its class block (its jaw's / its
// tail's hit boxes)
struct BigSwimmerFields : ActorClassState {
    explicit BigSwimmerFields(AnimatedActor& actor) : lungeYaw(actor.velocity.spare), contact(actor.hopperBytes[2]), mode(actor.hopperBytes[3]), startKey(actor.hopperBytes[4]) {}
    int16_t& lungeYaw;
    uint8_t& contact;
    uint8_t& mode;
    uint8_t& startKey;
    HitBoxHead hit;
    HitBoxHead tailHit;
    int16_t ticks = 0;
    int16_t counter = 0; // the turn's heading / the torpedoes left
    int16_t groundY = 0;
    uint16_t neighbour = 0;

    void clearBlock() override {
        hit = {};
        tailHit = {};
        ticks = 0;
        counter = 0;
        groundY = 0;
        neighbour = 0;
    }
};

// The small swimmer's own values: the lunge's yaw (the velocity's spare lane) and its class block
struct SmallSwimmerFields : ActorClassState {
    explicit SmallSwimmerFields(AnimatedActor& actor) : lungeYaw(actor.velocity.spare) {}
    int16_t& lungeYaw;
    HitBoxHead hit;
    int16_t ticks = 0;
    int16_t heading = 0;
    int16_t groundY = 0;
    uint16_t neighbour = 0;
    uint8_t contact = 0;
    uint8_t mode = 0;
    uint8_t startKey = 0;

    void clearBlock() override {
        hit = {};
        ticks = 0;
        heading = 0;
        groundY = 0;
        neighbour = 0;
        contact = 0;
        mode = 0;
        startKey = 0;
    }
};

// The companion of ST2F's torpedoes (its stage's static type 0x68): the torpedo it follows
struct TorpedoCompanionFields : ActorClassState {
    explicit TorpedoCompanionFields(StaticActor&) {}
    ActorRef<Actor> torpedo;

    void clearBlock() override {
        torpedo = nullptr;
    }
};

// The torpedo's class block: its shooter, its hit box, its homing ticks, the exhaust's counter
struct TorpedoFields : ActorClassState {
    explicit TorpedoFields(ProjectileActor&) {}
    ActorRef<Actor> shooter;
    HitBoxHead hit;
    StateArray<uint8_t, 2> clearedPad0{}; // bytes the block clear covers (no reader)
    int16_t homingTicks = 0;
    StateArray<uint8_t, 1> clearedPad1{}; // bytes the block clear covers (no reader)
    uint8_t exhaustTick = 0;
    StateArray<uint8_t, 14> clearedPad2{}; // bytes the block clear covers (no reader)
    void clearBlockBytes() {
        shooter = {};
        hit = {};
        clearedPad0 = {};
        homingTicks = 0;
        clearedPad1 = {};
        exhaustTick = 0;
        clearedPad2 = {};
    }
};

} // namespace od::port::ruin_swimmers_state
