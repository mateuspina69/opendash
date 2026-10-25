// The tank shell (pool 0x40 class 0x5c6cb0, stage projectile type 0x11 of ST15 / ST16 and ST46: fired by the Nino
// mission tanks, content/nino/mission_tank, and by its ST46 users): a glowing ball flying straight along its rotation
// for its ticks; it ends on an actor, a hit, the player's shield or a wall (a breakable wall's hit).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/field_alias.hpp"

namespace od::port {

struct PlayerEnv;

constexpr uint8_t kTankShellStageType = 0x11; // its stage projectile type

// The shell's spawn parameters (its spawner writes them); the variant: & 3 its size, >> 2 & 3 its speed (0x700 +
// 0x100 each)
struct TankShellParams {
    explicit TankShellParams(const ProjectileActor& actor) : TankShellParams(const_cast<ProjectileActor&>(actor)) {}
    explicit TankShellParams(ProjectileActor& actor) : ticks(actor.params, 0), attack(actor.params, 2) {}
    BytesField<int16_t, 4> ticks; // its life (the flight's ticks)
    BytesField<uint16_t, 4> attack;
};

// exe 0x5c6cb0: the shell's update (ActorUpdateFn)
void tankShellUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
