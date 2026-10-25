// The class states of shockwave.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/shockwave.hpp"
#include "game/field_alias.hpp"

namespace od::port::shockwave_state {

// The ring visual's fields (pool 0xa0)
struct ShockRingFxFields : ActorClassState {
    explicit ShockRingFxFields(EffectActor& actor) : done(actor.params[0]), downward(actor.params[1]), depthExtra(actor.params, 2), radius(actor.trail[2]), height(actor.trail[3]) {}
    uint8_t& done; // set by its shock when the ring ends
    uint8_t& downward; // the wall hangs down
    BytesField<int16_t, 4> depthExtra; // its outer radius (the depth test's margin)
    int16_t& radius;
    int16_t& height;
    ActorRef<ActorHeader> shock;
    uint32_t colour = 0;
    int8_t phase = 0;
};

// The shock's fields (pool 0x40; the velocity's x lane is the radius' growth per tick, its y lane the phase's step per
// tick)
struct ShockwaveFields : ActorClassState, ShockwaveParams {
    explicit ShockwaveFields(ProjectileActor& actor) : ShockwaveParams(actor), hitBox(actor.hitBox), slot_(actor) {}
    HitBoxHead& hitBox;
    ActorRef<EffectActor> ring; // the ring's visual
    uint32_t sequence = 0;      // the sequencer's next step (ShockStep, exe data)
    int16_t radius = 0;
    int16_t height = 0;
    uint32_t colour = 0;
    int16_t delay = 0;          // the sequencer's ticks to its next step
    int16_t damage = 0;
    int8_t phase = 0;           // the wall's 64-step height phase (bit 0x20: the ring ends)

    // the block (the hit box to the phase and the bytes after it) cleared as the exe's transcription stores it, dword by
    // dword
    void clearBlockWords() {
        hitBox = {};
        slot_.debrisFlags = 0;
        ring = nullptr;
        sequence = 0;
        radius = 0;
        height = 0;
        colour = 0;
        delay = 0;
        damage = 0;
        phase = 0;
    }

private:
    ProjectileActor& slot_;
};

} // namespace od::port::shockwave_state
