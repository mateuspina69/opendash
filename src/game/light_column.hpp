// The light column and its glow (stages ST0F / ST28 / ST2F; the column also in ST14 / ST3B / ST4B / ST4C): the glow
// emitter (pool 0xa0 class 0x437fa0, stage effect type 0x13) owns a light column (pool 0x60 class 0x598460, stage
// static type 0x11) and sends rising motes up it (the same class, variant | 0x80); the column is a capsule of hit
// entries between its base and its top, which either fades over 16 ticks or stays (variant bit 0x20).
#pragma once

#include <cstdint>

#include "game/field_alias.hpp"

namespace od::port {

struct PlayerEnv;

constexpr uint8_t kLightColumnStageType = 0x11; // the column's stage static type
// The column's spawn parameters (its spawners write them): the capsule's radius, its height in 8-unit steps, its hit
// entries' attack
struct LightColumnParams {
    explicit LightColumnParams(const StaticActor& actor) : LightColumnParams(const_cast<StaticActor&>(actor)) {}
    explicit LightColumnParams(StaticActor& actor)
        : radius(actor.params, 0), heightSteps(actor.params[2]), damage(actor.params[3]) {}
    BytesField<uint16_t, 4> radius;
    uint8_t& heightSteps;
    uint8_t& damage;
};

// exe 0x437fa0: the glow emitter / a mote (ActorUpdateFn)
void lightGlowUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x598460: the light column (ActorUpdateFn)
void lightColumnUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
