// The class states of status_markers.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/vectors.hpp"
#include "game/field_alias.hpp"
#include "game/state_array.hpp"

namespace od::port::status_markers_state {

constexpr uint32_t kBoltEnds = 0x10; // two end angles (64-step) a bolt
constexpr int16_t kBolts = 8, kBoltFade = 2;

// The marker's fields: the bolts drawn (fading by kBoltFade), the bolts' end angles
struct SlowMarkerFields : ActorClassState {
    explicit SlowMarkerFields(StaticActor& actor) : bolts(actor.wallHit, 0) {}
    PartField<int16_t, uint32_t> bolts;
    StateArray<uint8_t, kBoltEnds> boltEnds{};
};

constexpr uint32_t kDrainParticles = 0xc;
constexpr int16_t kFullLevel = 0xf, kFadeStart = 0x1e, kFadeEnd = 0x1f, kFadeStep = 2;

// one rising particle: its point and its life
struct DrainParticle {
    ShortVec3 point;
    int16_t life = 0;
};

// The marker's fields (the slot past its header and position): its level (0..kFullLevel, fading kFadeStart - level ..
// kFadeEnd), the particles alive, the sound's ticks, the particles
struct DrainBurnMarkerFields : ActorClassState {
    explicit DrainBurnMarkerFields(StaticActor& actor) : level(actor.wallHit, 0), particlesAlive(actor.wallHit, 2), soundTicks(actor.actorHit, 0) {}
    PartField<int16_t, uint32_t> level;
    PartField<int16_t, uint32_t> particlesAlive;
    PartField<int16_t, uint32_t> soundTicks;
    StateArray<DrainParticle, kDrainParticles> particles{};

    void clearBlock() override {
        for (size_t i = 10; i < 12; ++i) particles[i] = {};
    }
};

} // namespace od::port::status_markers_state
