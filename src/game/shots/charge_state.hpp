// The class states of charge.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/field_alias.hpp"
#include "game/state_array.hpp"

namespace od::port::charge_state {

constexpr uint32_t kChargeParticles = 0x30;

// The marker's fields: its phase, the sphere's shrink (its radius (4 - shrink) * 32), the hum's ticks, the particles'
// angles (12-bit: the spiral's step)
struct ChargeMarkerFields : ActorClassState {
    explicit ChargeMarkerFields(StaticActor& actor) : phase(actor.rotation.spare), shrink(actor.prevRotation.x), soundTicks(actor.prevRotation.y) {}
    int16_t& phase;
    int16_t& shrink;
    int16_t& soundTicks;
    StateArray<int16_t, kChargeParticles> particles{};
};

// ---- the shock wave (static global type 0x17) -------------------------------------------------------------------------
// The wave's fields: its level (the spawn's parameter), its hit box, its attack, its ticks left and its life, its full
// size and its size
struct ShockWaveFields : ActorClassState {
    explicit ShockWaveFields(StaticActor& actor) : level(actor.params[0]), attack(actor.rootMatrix.e[8]), ticks(actor.rootTranslation[0], 0), life(actor.rootTranslation[0], 2), fullSize(actor.rootTranslation[1], 0), size(actor.rootTranslation[1], 2) {}
    FieldAlias<int8_t, uint8_t> level;
    FieldAlias<uint16_t, int16_t> attack;
    PartField<int16_t, int32_t> ticks;
    PartField<int16_t, int32_t> life;
    PartField<int16_t, int32_t> fullSize;
    PartField<int16_t, int32_t> size;
    HitBoxHead hitBox;
};

} // namespace od::port::charge_state
