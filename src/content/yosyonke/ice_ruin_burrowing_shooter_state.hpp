// The class states of ice_ruin_burrowing_shooter.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/field_alias.hpp"
#include "game/state_array.hpp"

namespace od::port::ice_ruin_burrowing_shooter_state {

// its hit entry's kind (table 0x47a180), none past it
enum class ShooterHitKind : uint32_t { Body = 0, Thrown = 1, Wriggling = 2, Harmless = 3, None = 0xff };

// The shooter's own bytes
struct ShooterFields : ActorClassState {
    explicit ShooterFields(AnimatedActor& actor) : hitKind(actor.bossFlags), bits(actor.shownParts), ticks(actor.hopperBytes[0]), hopSide(actor.hopperBytes[1]), hopCount(actor.hopperBytes[2]) {}
    uint32_t& hitKind; // ShooterHitKind (the carry reads it too: 1 thrown, 2 wriggling)
    uint8_t& bits;
    uint8_t& ticks;
    FieldAlias<int8_t, uint8_t> hopSide;
    uint8_t& hopCount;
    HitBoxHead hit;
    int32_t playerDistance = 0;
    int16_t turnStep = 0;
    StateArray<uint8_t, 2> clearedPad0{}; // bytes the block clear covers (no reader)
    uint8_t contact = 0;
    StateArray<uint8_t, 7> clearedPad1{}; // bytes the block clear covers (no reader)
    void clearAll() {
        hit = {};
        playerDistance = 0;
        turnStep = 0;
        clearedPad0 = {};
        contact = 0;
        clearedPad1 = {};
        hitKind = 0;
        bits = 0;
        ticks = 0;
        hopSide = 0;
        hopCount = 0;
    }
    void setHitKind(ShooterHitKind k) { hitKind = uint32_t(k); }

    void clearBlock() override {
        hit = {};
        playerDistance = 0;
        turnStep = 0;
        clearedPad0 = {};
        contact = 0;
        clearedPad1 = {};
    }
};

// The shot's own bytes: its attack (the falling form reads the low byte as "big"); the shooter while it charges (the
// falling form's hit box there), the flying hit box, the rings' size, the timer, the falling ticks
// The shot's own bytes: its attack (the falling form reads the low byte as "big"); the shooter while it charges and
// flies, the flying hit box after it; the falling form's hit box over the shooter and the flying box's first word, its
// ticks after it (the exe lays the two forms over the same bytes; each form keeps its own); the rings' size, the timer
struct ShotFields : ActorClassState {
    explicit ShotFields(ProjectileActor& actor) : attack(actor.params, 0), big(actor.params[0]) {}
    BytesField<int16_t, 4> attack;
    uint8_t& big;
    ActorRef<AnimatedActor> shooter; // charging / flying (+0xbc)
    HitBoxHead hit;                  // flying (+0xc0)
    HitBoxHead fallHit;              // falling (+0xbc)
    int16_t fallTicks = 0;           // falling (+0xc4)
    uint16_t ringSize = 0;
    uint16_t timer = 0;
};

} // namespace od::port::ice_ruin_burrowing_shooter_state
