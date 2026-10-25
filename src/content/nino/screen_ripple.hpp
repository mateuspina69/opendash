// The screen ripples (pool 0xa0 type 0x16), content/nino/screen_ripple.cpp.
#pragma once

#include <cstdint>

#include "engine/routine_ids.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_state.hpp"
#include "game/field_alias.hpp"


namespace od::port {

constexpr RoutineId kScreenRippleClass = RoutineId::ScreenRippleUpdate;   // the class routine (pool 0xa0 type 0x16)
constexpr uint8_t kGrowingRippleVariant = 0;         // the frame-buffer ripple (the renderer's ScreenRipple draws it)

// Variant 0's fields: the start radius, the growth a tick, the largest radius, the radius and the phase (the second
// ring's distance: phase >> 8), and the shift byte the picture's uv corners move by (the renderer's)
struct GrowingRippleFields : ActorClassState {
    explicit GrowingRippleFields(EffectActor& actor) : startRadius(actor.params[0]), growth(actor.params[1]), shift(actor.params[2]), maxRadius(actor.posSpare), radius(actor.trail[0]) {}
    uint8_t& startRadius;
    FieldAlias<int8_t, uint8_t> growth;
    FieldAlias<int8_t, uint8_t> shift;
    int16_t& maxRadius;
    int16_t& radius;
    uint16_t phase = 0;
};

// exe 0x438750 (pool 0xa0 type 0x16): by the variant -- 0 a growing ripple of the frame buffer (the ST18 machine's
// pulse), 1 two rings breathing in the camera plane, 2 a shrinking ray star
void screenRippleUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
