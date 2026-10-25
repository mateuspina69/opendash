// The class states of st3b_flame_wall.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/field_alias.hpp"
#include "game/state_array.hpp"

namespace od::port::st3b_flame_wall_state {

// Its own bytes (over the contact block +0x64..+0xcb): its hit box, its two phase angles, the ticks left
struct FlameFields : ActorClassState {
    explicit FlameFields(StaticActor& actor) : phaseA(actor.rootMatrix.e[0]), phaseB(actor.rootMatrix.e[1]), ticksLeft(actor.rootMatrix.e[2]) {}
    FieldAlias<uint16_t, int16_t> phaseA;
    FieldAlias<uint16_t, int16_t> phaseB;
    FieldAlias<uint16_t, int16_t> ticksLeft;
    HitBoxHead hitBox;
    StateArray<uint8_t, 8> clearedPad0{}; // bytes the block clear covers (no reader)
    StateArray<uint8_t, 82> clearedPad1{}; // bytes the block clear covers (no reader)
    void clearBytes() {
        hitBox = {};
        clearedPad0 = {};
        clearedPad1 = {};
        phaseA = 0;
        phaseB = 0;
        ticksLeft = 0;
    }

    void clearBlock() override {
        for (size_t i = 66; i < 82; ++i) clearedPad1[i] = {};
    }
};

} // namespace od::port::st3b_flame_wall_state
