// The effects of the Nino ruin's stage tables (ST36 / ST38 / ST50): the falling glow blob, the flash and the
// per-variant effect, content/nino/ruin_effects.cpp.
#pragma once

#include <cstdint>

#include "engine/routine_ids.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_state.hpp"
#include "game/field_alias.hpp"

namespace od::port {

// The flash's stage type and its spawn parameter (its size); its spawners: its stage's classes (the ST38 boss's shots)
constexpr uint8_t kRuinFlashType = 8;
struct RuinFlashParams {
    explicit RuinFlashParams(const EffectActor& actor) : RuinFlashParams(const_cast<EffectActor&>(actor)) {}
    explicit RuinFlashParams(EffectActor& actor) : size(actor.params, 0) {}
    BytesField<uint16_t, 4> size;
};

// exe 0x5cbc60 (pool 0x40 stage type 0x20): a falling glow blob (a billboard, draw command 0x2c); a hit entry while its
// parameter asks for one
void glowBlobUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x431820 (pool 0xa0 stage type 8): a flash -- variant 0 two billboards and sound 0x9b, else three rings
void ruinFlashUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x5cc7b0(shot): the hit spark (the global effect type 0) at a shot of the ruin (its end on a bump)
void ruinShotHitSpark(const Actor& shot);
// exe 0x5cc800(shot, variant): the flash (variant 0: a wall, 1: its life over) at a shot of the ruin
void ruinShotFlash(const Actor& shot, uint8_t variant);
// exe 0x431e90 (pool 0xa0 stage type 0xa): a per-variant effect ([0x88cb04]: 0..6 nothing, 7 / 8 the storm ribbon's
// start / draw)
void ruinVariantEffectUpdate(const PlayerEnv& env, uint32_t actor);
constexpr RoutineId kRuinVariantEffectClass = RoutineId::RuinVariantEffectUpdate;
enum class VariantEffectState : uint8_t { Start = 0, Run = 1, Free = 2 };
constexpr uint8_t kScreenWaveVariant = 5; // the variant whose screen wave strength is its own byte
// The effect's spawn parameter (its low nibble a size, bit 4 a side) and its block (their readers are not traced: the
// variants 0..6 do nothing on PC). The PS1 / PSP draw of the variants 0 / 1 / 5 is a screen wave (OpenDash's renderer
// draws it): its tint the three bytes from +0xc (the parameter is the first), its strength the kind byte (variant 5:
// waveStrength)
struct VariantEffectFields : ActorClassState {
    explicit VariantEffectFields(EffectActor& actor) : param(actor.params[0]), tint(actor.params, 0), waveStrength(actor.params[3]), size(actor.trail[2]), side(actor.trail[3]) {}
    uint8_t& param;
    ArrayFields<uint8_t, 3, uint8_t, 4> tint;
    FieldAlias<int8_t, uint8_t> waveStrength;
    FieldAlias<uint16_t, int16_t> size;
    FieldAlias<uint16_t, int16_t> side;
    uint8_t tickA = 0;
    uint8_t tickB = 0;
    uint8_t kind = 0;
};


} // namespace od::port
