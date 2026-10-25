// The class states of lens_flare.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/field_alias.hpp"
#include "game/state_array.hpp"

namespace od::port::lens_flare_state {

// ---- the dropping ring ---------------------------------------------------------------------------------------------
// The ring's fields: its tilt (x / y angles >> 4) and radius / 2 (the spawner's), where it stops, its distance and ticks
struct DroppingRingFields : ActorClassState {
    explicit DroppingRingFields(EffectActor& actor) : tiltX(actor.params[0]), tiltY(actor.params[1]), halfRadius(actor.params[2]), stopAt(actor.posSpare) {}
    FieldAlias<int8_t, uint8_t> tiltX;
    FieldAlias<int8_t, uint8_t> tiltY;
    FieldAlias<int8_t, uint8_t> halfRadius;
    int16_t& stopAt;
    uint16_t distance = 0;
    uint8_t ticks = 0;
};

// The tail every kind clears at its start (+0x18..+0x27, one byte at a time)
// ---- the pulsing disc ----------------------------------------------------------------------------------------------
// The disc's fields: its ticks and radius (the position it sits on: the 16.16 position its parameter word names, an
// actor's +0x10, its integer parts added; 0: its own)
struct PulsingDiscFields : ActorClassState {
    explicit PulsingDiscFields(EffectActor& actor) : ticks(actor.trail[0]), radius(actor.trail[1]) {}
    FieldAlias<uint16_t, int16_t> ticks;
    FieldAlias<uint16_t, int16_t> radius;
};

// ---- the shrinking disc and its rays -------------------------------------------------------------------------------
// A shrinking disc / a ray: the point it sits on is the s16 point (<< the stage shift) its parameter word (+0xc) names
// (a bone point), its integer parts >> the shift added to the effect's position (0: the effect's own)
struct PointedFields : ActorClassState {
    explicit PointedFields(EffectActor&) {}
};

// The shrinking disc's fields: its ticks, its width, the script's step sub-state it started at (its end: a change)
struct ShrinkingDiscFields : PointedFields {
    explicit ShrinkingDiscFields(const EffectActor& actor) : ShrinkingDiscFields(const_cast<EffectActor&>(actor)) {}
    explicit ShrinkingDiscFields(EffectActor& actor) : PointedFields(actor), ticks(actor.trail[0]), width(actor.trail[1]), startStep(actor.trail[2]) {}
    FieldAlias<uint16_t, int16_t> ticks;
    FieldAlias<uint16_t, int16_t> width;
    FieldAlias<uint16_t, int16_t> startStep;
};

// The ray's fields: its angle (the spawner's, in the header's step word), its width (the spawner's), its ticks, its
// width, the step sub-state it started at, its turn speed, its angle, an unread byte
struct RayFields : PointedFields {
    explicit RayFields(const EffectActor& actor) : RayFields(const_cast<EffectActor&>(actor)) {}
    explicit RayFields(EffectActor& actor)
        : PointedFields(actor), spawnAngle(actor.step, actor.stepCounter), spawnWidth(actor.posSpare) {}
    BytesOfField<uint16_t> spawnAngle;
    FieldAlias<uint16_t, int16_t> spawnWidth;
    uint16_t ticks = 0;
    uint16_t width = 0;
    uint16_t startStep = 0;
    uint16_t turnSpeed = 0;
    uint16_t angle = 0;
    uint8_t unread22 = 0; // set at its start (its reader is not traced)

    void clearBlock() override {
        ticks = 0;
        width = 0;
        startStep = 0;
        turnSpeed = 0;
        angle = 0;
        unread22 = 0;
    }
};

// The globals each kind keeps its tail's address in (their readers are not traced)
struct LensFlareTails {
    uint32_t sun = 0; // 0xa59e04
    uint32_t droppingRing = 0; // 0xa59dec
    uint32_t pulsingDisc = 0; // 0xa59df0
    uint32_t shrinkingDisc = 0; // 0xa59df4
    uint32_t ray = 0; // 0xa59df8
    uint32_t tint = 0; // 0xa59dfc
    uint32_t sprite = 0; // 0xa59e00
};
inline LensFlareTails& tails() {
    static LensFlareTails s;
    return s;
}

} // namespace od::port::lens_flare_state
