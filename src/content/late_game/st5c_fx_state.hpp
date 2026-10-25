// The class states of st5c_fx.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/field_alias.hpp"
#include "game/state_array.hpp"
#include "game/vectors.hpp"

namespace od::port::st5c_fx_state {

// The rock's own bytes: the boss it was spawned with (the parameter word), then its block: the tint ticks, a word, its
// sound played, its hit box, the boss
struct RockFields : ActorClassState {
    explicit RockFields(AnimatedActor& actor) : spawnBoss(actor.params, 0) {}
    BytesField<uint32_t, 4> spawnBoss;
    uint16_t tintTicks = 0;
    uint16_t word = 0;
    uint16_t soundPlayed = 0;
    HitBoxHead hitBox;
    ActorRef<AnimatedActor> boss;

    void clearBlock() override {
        tintTicks = 0;
        word = 0;
        soundPlayed = 0;
        hitBox = {};
        boss = nullptr;
    }
};

// The shot's own bytes: the boss it was spawned with (the parameter word), then its block: a timer, a word, the
// rotation it travels along, its hit box, the boss
struct StompShotFields : ActorClassState {
    explicit StompShotFields(ProjectileActor& actor) : spawnBoss(actor.params, 0) {}
    BytesField<uint32_t, 4> spawnBoss;
    uint16_t timer = 0;
    uint16_t word = 0;
    ShortVec4 travel;
    HitBoxHead hitBox;
    ActorRef<AnimatedActor> boss;
};

// The ball's own bytes: the boss it was spawned with, then its block: a timer, a word, its hit box, the boss
struct BallFields : ActorClassState {
    explicit BallFields(ProjectileActor& actor) : spawnBoss(actor.params, 0) {}
    BytesField<uint32_t, 4> spawnBoss;
    uint16_t timer = 0;
    uint16_t word = 0;
    HitBoxHead hitBox;
    ActorRef<AnimatedActor> boss;
};

constexpr uint32_t kChargeRays = 0x20;

// The charge's own bytes: the boss it was spawned with (the parameter word), then its block: the timer, a word, the
// rays' angles, lengths and spins, the boss
struct ChargeFields : ActorClassState {
    explicit ChargeFields(StaticActor& actor) : spawnBoss(actor.params, 0), timer(actor.rotation.x), word(actor.rotation.y) {}
    BytesField<uint32_t, 4> spawnBoss;
    int16_t& timer;
    FieldAlias<uint16_t, int16_t> word;
    StateArray<uint16_t, kChargeRays> angles{};
    StateArray<uint8_t, kChargeRays> lengths{};
    StateArray<int8_t, kChargeRays> spins{};
    ActorRef<AnimatedActor> boss;
};

// The blade's own bytes: the boss it was spawned with (the parameter word), then its block: a timer, its segments, the
// bone / height / radius it was spawned with, its hit box, the boss
struct BladeFields : ActorClassState {
    explicit BladeFields(StaticActor& actor) : spawnBoss(actor.params, 0), timer(actor.rootMatrix.e[0]), segments(actor.rootMatrix.e[1]), bone(actor.rootMatrix.e[2]), height(actor.rootMatrix.e[3]), radius(actor.rootMatrix.e[4]) {}
    BytesField<uint32_t, 4> spawnBoss;
    int16_t& timer;
    int16_t& segments;
    int16_t& bone;
    int16_t& height;
    int16_t& radius;
    HitBoxHead hitBox;
    ActorRef<AnimatedActor> boss;
};

} // namespace od::port::st5c_fx_state
