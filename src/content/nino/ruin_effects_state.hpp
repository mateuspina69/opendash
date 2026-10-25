// The class states of ruin_effects.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "content/nino/ruin_effects.hpp"
#include "game/field_alias.hpp"
#include "game/state_array.hpp"

namespace od::port::ruin_effects_state {

// The flash's size (its spawn parameter) and its own block: the ticks left, the billboards' depth offset, the rings'
// colour (red, green, blue)
struct FlashFields : ActorClassState, RuinFlashParams {
    explicit FlashFields(EffectActor& actor) : RuinFlashParams(actor), ticks(actor.trail[0]), depthOffset(actor.trail[1]), red(actor.trail[2]), green(actor.trail[3]), blue(actor.trail[4]) {}
    int16_t& ticks;
    int16_t& depthOffset;
    FieldAlias<uint16_t, int16_t> red;
    FieldAlias<uint16_t, int16_t> green;
    FieldAlias<uint16_t, int16_t> blue;
};

// The blob's spawn parameters (its attack, a hit entry, its life) and its own values (its start size in the rotation's
// spare lane, its growth in the velocity's) and class block (the hit box, the life, the brightness, the size, the burst
// frame)
struct BlobFields : ActorClassState {
    explicit BlobFields(ProjectileActor& actor) : attack(actor.params, 0), hits(actor.params[2]), life(actor.params[3]), startSize(actor.rotation.spare), growth(actor.velocity.spare), hit(actor.hitBox) {}
    BytesField<int16_t, 4> attack;
    uint8_t& hits;
    FieldAlias<int8_t, uint8_t> life;
    int16_t& startSize;
    int16_t& growth;
    HitBoxHead& hit;
    StateArray<uint8_t, 4> clearedPad0{}; // bytes the block clear covers (no reader)
    uint16_t ticksLeft = 0;
    int16_t brightness = 0;
    int16_t size = 0;
    int16_t burstFrame = 0;
    StateArray<uint8_t, 12> clearedPad1{}; // bytes the block clear covers (no reader)
    void clearBlockBytes() {
        clearedPad0 = {};
        ticksLeft = 0;
        brightness = 0;
        size = 0;
        burstFrame = 0;
        clearedPad1 = {};
        hit = {};
    }
};

} // namespace od::port::ruin_effects_state
