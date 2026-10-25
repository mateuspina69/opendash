// The class states of ch2_script_cast.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/keyframe_tracks.hpp"
#include "game/field_alias.hpp"
#include "game/state_array.hpp"

namespace od::port::ch2_script_cast_state {

// The talking cast's fields: the animation's start key, the smoke pattern's step, a tick, animated (else decoded
// only), the mouth open
struct TalkingCastFields : ActorClassState {
    explicit TalkingCastFields(AnimatedActor& actor) : animKeyStart(actor.hopperBytes[1]), smokeStepByte(actor.hopperBytes[2]), ticks(actor.hopperBytes[5]), animated(actor.townBirdHits), mouthOpen(actor.townBirdHitsHigh), smokeStep(actor.hopperBytes, 2) {}
    uint8_t& animKeyStart;
    uint8_t& smokeStepByte;
    uint8_t& ticks;
    uint8_t& animated;
    uint8_t& mouthOpen;
    ArrayField<uint16_t, uint8_t, 6> smokeStep; // the step as the word its set-up stores (its low byte smokeStepByte)

    void clearBlock() override {
        smokeStep = 0;
    }
};

// The take-off cast's fields: its child and its texture tracks
struct TakeOffCastFields : ActorClassState {
    explicit TakeOffCastFields(AnimatedActor&) {}
    ActorRef<AnimatedActor> child;
    KeyframeTracks tracks;

    void clearBlock() override {
        child = nullptr;
        tracks = {};
    }
};

// The flash's fields: its size, its ticks, its 16 spikes
struct FlashFields : ActorClassState {
    explicit FlashFields(EffectActor& actor) : size(actor.params, 0), ticks(actor.params[3]) {}
    BytesField<uint16_t, 4> size;
    uint8_t& ticks;
    StateArray<uint8_t, 0x10> spikes{};
};

} // namespace od::port::ch2_script_cast_state
