// The class states of bars_and_gate.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/field_alias.hpp"
#include "game/hit_list.hpp"
#include "game/state_array.hpp"

namespace od::port::bars_and_gate_state {

// The bar's class block: the hanger's height when it hung on, its light's slot, the trigger spawned (the ST17 lever);
// the ST17 liftable static's hit box, lifted once, its hit entry's kind (kLiftable* bits)
struct BarFields : ActorClassState {
    explicit BarFields(StaticActor& actor) : light(actor.gunshipTimer()) {}
    uint8_t& light;
    HitBoxHead hitBox;   // +0xbc (the liftable static's: no contact output)
    uint8_t lifted = 0;  // +0xc4: lifted once (it no longer rides its gate)
    uint8_t entry = 0;   // +0xc5
    int16_t hangY = 0;
    uint8_t triggerSpawned = 0;
    StateArray<uint8_t, 2> clearedPad1{}; // bytes the block clear covers (no reader)
    void clearBlockBytes() {
        clearBlock();
        light = 0;
    }

    void clearBlock() override {
        hitBox = {};
        lifted = 0;
        entry = 0;
        hangY = 0;
        triggerSpawned = 0;
        clearedPad1 = {};
    }
};

// The gate's class block: its target height, open (toggled at each landing), the bounce / wave countdown, the wave's
// statics left, moved once
struct GateFields : ActorClassState {
    explicit GateFields(StaticActor& actor) : ticks(actor.gunshipPitchTarget()), waveLeft(actor.gunshipTimer()) {}
    uint8_t& ticks;
    uint8_t& waveLeft;
    StateArray<uint8_t, 8> clearedPad0{}; // bytes the block clear covers (no reader)
    int16_t targetY = 0;
    uint8_t open = 0;
    uint8_t moved = 0;
    StateArray<uint8_t, 2> clearedPad1{}; // bytes the block clear covers (no reader)
    void clearBlockBytes() {
        clearedPad0 = {};
        targetY = 0;
        open = 0;
        moved = 0;
        clearedPad1 = {};
        ticks = 0;
        waveLeft = 0;
    }

    void clearBlock() override {
        clearedPad0 = {};
        targetY = 0;
        open = 0;
        moved = 0;
        clearedPad1 = {};
    }
};

} // namespace od::port::bars_and_gate_state
