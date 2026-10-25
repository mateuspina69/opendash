// The class states of end_cast.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/cutscene_actor.hpp"
#include "game/vectors.hpp"
#include "game/field_alias.hpp"
#include "game/state_array.hpp"

namespace od::port::end_cast_state {

// The ending effects' spawn bytes: the slot of the actor they follow and three arguments (by variant: the glow disc's
// radius / height / follow-the-player, the sparkles' first bone)
struct EndingEffectParams {
    explicit EndingEffectParams(const EffectActor& actor) : EndingEffectParams(const_cast<EffectActor&>(actor)) {}
    explicit EndingEffectParams(EffectActor& actor) : slot(actor.params[0]), arg0(actor.params[1]), arg1(actor.params[2]), arg2(actor.params[3]) {}
    FieldAlias<int8_t, uint8_t> slot;
    FieldAlias<int8_t, uint8_t> arg0;
    FieldAlias<int8_t, uint8_t> arg1;
    FieldAlias<int8_t, uint8_t> arg2;
};

// The cue casts' class block: the shared cast layout, then the fade timer (the third group member's; the finale casts
// clear it)
struct EndCastExtra : CastFields {
    explicit EndCastExtra(AnimatedActor& actor) : CastFields(actor) {}
    int16_t fadeTimer = 0;

    void clearBlock() override {
        CastFields::clearBlock();
        fadeTimer = 0;
    }
};

// The prop's own bytes: its kind and its stop (the spawn's parameters), its tick counter and base height
struct BobbingPropFields : ActorClassState {
    explicit BobbingPropFields(AnimatedActor& actor) : kind(actor.params[0]), stop(actor.params[1]) {}
    FieldAlias<int8_t, uint8_t> kind;
    uint8_t& stop; // set from outside: back to the base, state 2
    uint32_t ticks = 0;
    int32_t baseY = 0;

    void clearBlock() override {
        ticks = 0;
        baseY = 0;
    }
};

// ---- 0x43ec20 (variant 4): a rising trail of 8 billboards -------------------------------------------------------------
// States: 0 grows in over 0x3f ticks, 1 holds (spawns puffs (variant 5) on rng & 7 == 0) until released, 2 fades over
// 0x20 ticks, then freed. A history of 8 heights (the newest = y + sin(counter * 0x10) >> 10); every 4th tick y rises
// by 1 while the rise count >= 1 (counted down). Colours from the counter (the scratch's colours), 7 dimmer copies.
struct TrailFields : ActorClassState {
    explicit TrailFields(EffectActor& actor) : counter(actor.params, 0), counterLow(actor.params[0]), riseCount(actor.params[3]) {}
    BytesField<uint32_t, 4> counter;
    uint8_t& counterLow;
    FieldAlias<int8_t, uint8_t> riseCount; // the counter's top byte
    union {
        StateArray<uint8_t, 0x10> block{};
        StateArray<int16_t, 8> heights;
    };
};

// ---- 0x43d050 (variant 0): the player's ring band --------------------------------------------------------------------
// States: 0 init (the lift = arg0 << 2, the radius = arg1 << 2, the counter = (arg2 & 0x10) << 4), 1 hold (brightness
// 0x20) until released (its step counter), 2 fade (brightness (0x10 - counter) * 2, the radius grows by 4; freed when
// the counter reaches 0x10). The centre follows the player's bone point 0 (lifted); the band spins (by +-0x18 by arg2
// bit 0) and wobbles (sin(frame * 0x20) >> 7). Drawn while drawn and the centre is on screen: 32 segments (Rz Rx Ry of
// the wobble / spin rotated, half height = the segment chord / 2), each DR_MODE + (even segments) POLY_FT4 + the two
// band halves, colour front / back by NCLIP; the counter + 1 per drawn frame.
struct RingBandFields : ActorClassState, EndingEffectParams {
    explicit RingBandFields(EffectActor& actor) : EndingEffectParams(actor), counter(actor.trail[0]), wobbleX(actor.trail[1]), spin(actor.trail[2]), wobbleZ(actor.trail[3]), radius(actor.trail[4]), lift(actor.trail[5]) {}
    int16_t& counter;
    int16_t& wobbleX;
    int16_t& spin;
    int16_t& wobbleZ;
    int16_t& radius;
    int16_t& lift;
    StateArray<uint8_t, 4> clearedPad0{}; // bytes the block clear covers (no reader)
    void clearBlockBytes() {
        clearedPad0 = {};
        counter = 0;
        wobbleX = 0;
        spin = 0;
        wobbleZ = 0;
        radius = 0;
        lift = 0;
    }
};

// ---- 0x43d970 (variant 1): the glow disc and ring under an actor -------------------------------------------------------
// The radius = arg0 << 2, the height = arg1 << 2; arg2 != 0: state 1 (fading in over 0x10 drawn frames, then 2)
// following the player (its slot), else state 2 following the slot of its spawn. Freed when the followed actor is gone
// (not live). While drawn: a flat POLY_FT4 disc (2r) at its feet and a 32-segment band (radius r + 0x10, from -h to
// +h/2, h = (sin(frame * 0x40) >> 9) + height), the counter + 1.
struct GlowDiscFields : ActorClassState, EndingEffectParams {
    explicit GlowDiscFields(EffectActor& actor) : EndingEffectParams(actor), slotByte(actor.params[0]), followPlayer(actor.params[3]), counter(actor.trail[0]), radius(actor.trail[1]), height(actor.trail[2]) {}
    uint8_t& slotByte;
    uint8_t& followPlayer;
    int16_t& counter;
    int16_t& radius;
    int16_t& height;
    StateArray<uint8_t, 10> clearedPad0{}; // bytes the block clear covers (no reader)
    void clearBlockBytes() {
        clearedPad0 = {};
        counter = 0;
        radius = 0;
        height = 0;
    }
};

// ---- 0x43ef80 (variant 5): a drifting puff (one billboard, its colour ramping over its life) ---------------------------
struct PuffFields : ActorClassState {
    explicit PuffFields(EffectActor& actor) : colour(actor.params, 0), counter(actor.trail[0]), life(actor.trail[1]), size(actor.trail[5]), yaw(actor.trail[6]), yawStep(actor.trail[7]) {}
    ArrayFields<int8_t, 3, uint8_t, 4> colour;
    int16_t& counter;
    int16_t& life;
    int16_t& size;
    int16_t& yaw;
    int16_t& yawStep;
    ShortVec3 drift;
    void clearBlockBytes() {
        drift = {};
        counter = 0;
        life = 0;
        size = 0;
        yaw = 0;
        yawStep = 0;
    }
};

// ---- 0x43f260 (variant 6): a tumbling two-triangle glow fan ----------------------------------------------------------
// States (word stores: the sub-state cleared on each change): 0 init (its counter, its hold = arg0, two angle triples =
// rng & 0xfff and those - (rng & 0xff) + 0x80, its spins = 2 - (rng & 3)), fading in (k = counter * 2) until the counter
// == 0x20; 1 holds (colour 0x408080, its radius) until the counter == its hold; 2 fades (k = (0x20 - counter) * 2),
// freed at the counter == 0x20. The centre follows the actor in its slot (when != 0). Each tick (no drawn gate):
// corners v1 / v2 = the point (sin, +-r/4, -cos)(angle (counter & 0x1f) * 0x80) * r (r = radius * k >> 6) rotated by Rz
// Rx Ry of each angle triple, + centre, << shift; DR_MODE (abr 1) + two POLY_G3 0x32 {centre col, v1 0, mid(v1, v2) 0}
// {centre col, mid 0, v2 0}; the counter + 1, the triples += the spins. The NCLIP result 0x6d01e8 and the screen points
// 0x6d0110.. are render-only (not stored, as the other variants).
struct GlowFanFields : ActorClassState {
    explicit GlowFanFields(EffectActor& actor) : followSlot(actor.params[0]), spins(actor.params, 1), radius(actor.posSpare), counter(actor.trail[0]), hold(actor.trail[1]) {}
    uint8_t& followSlot;
    ArrayFields<int8_t, 3, uint8_t, 4> spins;
    int16_t& radius;
    int16_t& counter;
    int16_t& hold;
    ShortVec3 anglesA;
    ShortVec3 anglesB;
    void clearBlockBytes() {
        anglesA = {};
        anglesB = {};
        counter = 0;
        hold = 0;
    }
};

// ---- 0x43f980 (variant 7): a pulsing screen-space glow (game/glow_fan) ------------------------------------------------
// States (dword stores: the sub-state and the step bytes cleared); every state's first tick clears the counter. 0 fade
// in (e = 2 * counter, -> 1 at the counter == 0x20); 1 hold (0x306060) until released -> 2; 2 pulse (e = (cos((counter
// & 0x1f) * 0x80) >> 9) + 0x38, -> 3 when released and the counter & 0x1f == 0x1f); 3 flare (e = counter * 4 + 0x40, ->
// 4 at the counter == 0x10); 4+ fade out sinking (e = (0x20 - counter) * 4, y - 1; freed at the counter == 0x20).
// Colour 0x306060 at e == 0x40, else c = e * 0x60 >> 6 as c | (c | (c & 0x1fe) << 7) << 8; radii exe 0x5f2f20(pos, arg
// * 4) (y: the slot byte, x: arg0); the counter + 1 every drawn tick
struct PulseGlowFields : ActorClassState, EndingEffectParams {
    explicit PulseGlowFields(EffectActor& actor) : EndingEffectParams(actor), counter(actor.trail[0]) {}
    int16_t& counter;
};

} // namespace od::port::end_cast_state
