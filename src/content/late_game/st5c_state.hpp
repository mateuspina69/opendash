// The class states of st5c.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/cutscene_actor.hpp"
#include "content/late_game/st5c.hpp"
#include "game/field_alias.hpp"
#include "game/state_array.hpp"
#include "game/vectors.hpp"

namespace od::port::st5c_state {

// The intro cast's class block: the shared cast layout (its face tracks, flags and key) and its fade counter
struct IntroFields : CastFields {
    explicit IntroFields(AnimatedActor& actor) : CastFields(actor) {}
    int16_t fadeCounter = 0;

    void clearBlock() override {
        CastFields::clearBlock();
        fadeCounter = 0;
    }
};

// The defeat cast's class block: the shared cast layout, then its ground height (16.16) and two words
struct DefeatFields : CastFields {
    explicit DefeatFields(AnimatedActor& actor) : CastFields(actor) {}
    Fixed16 groundY;
    uint16_t wordA = 0;
    uint16_t wordB = 0;

    void clearBlock() override {
        CastFields::clearBlock();
        groundY = 0;
        wordA = 0;
        wordB = 0;
    }
};

constexpr uint32_t kBurstSparks = 0x10;

// one spark: its point and its speed
struct BurstSpark {
    int16_t x = 0;
    int16_t y = 0;
    int16_t z = 0;
    int16_t speed = 0;
};

// The burst's own bytes: its spawn bytes, the sparks and their sizes, its timer (the kinds 7..9 keep their count in
// its low byte), its count
struct BurstFields : ActorClassState, SparkBurstParams {
    explicit BurstFields(StaticActor& actor) : SparkBurstParams(actor), timerLow(actor.scaleBytes[0]), countB(actor.scaleBytes[0]), count(actor.scaleBytes[2]) {}
    uint8_t& timerLow;
    FieldAlias<int8_t, uint8_t> countB;
    FieldAlias<int8_t, uint8_t> count;
    StateArray<BurstSpark, kBurstSparks> sparks{};
    StateArray<int8_t, kBurstSparks> sizes{};
    uint16_t timer = 0;

    void clearBlock() override {
    }
};

// The boss's own bytes (its block from +0x14c)
struct BossFields : ActorClassState {
    explicit BossFields(AnimatedActor&) {}
    HitBoxHead hitBox;
    uint16_t timerA = 0;
    uint16_t timerB = 0;
    uint16_t cooldown = 0;
    uint16_t word = 0;
    uint16_t flags = 0;
    uint16_t queuedDamage = 0;
    StateArray<ActorRef<AnimatedActor>, 3> afterImages{};
    PartField<uint8_t, uint16_t> flagsLow{flags, 0};

    void clearBlock() override {
        hitBox = {};
        timerA = 0;
        timerB = 0;
        cooldown = 0;
        word = 0;
        flags = 0;
        queuedDamage = 0;
        afterImages = {};
    }
};

} // namespace od::port::st5c_state
