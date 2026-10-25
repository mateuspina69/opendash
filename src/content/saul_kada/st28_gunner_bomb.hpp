// The fire-ruin gunner boss's bomb (ST28 pool 0x40 type 0x18, exe 0x5c92d0), see content/saul_kada/st28_gunner_bomb.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"
#include "game/actor_state.hpp"
#include "game/field_alias.hpp"

namespace od::port {

constexpr uint8_t kGunnerBombType = 0x18;
// The bomb's spawn parameter: its attack
struct GunnerBombParams {
    explicit GunnerBombParams(const ProjectileActor& actor) : GunnerBombParams(const_cast<ProjectileActor&>(actor)) {}
    explicit GunnerBombParams(ProjectileActor& actor) : attack(actor.params, 0) {}
    BytesField<int16_t, 4> attack;
};

// exe 0x5c92d0: its flight, then a patch of flames around its landing point
void gunnerBombUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
