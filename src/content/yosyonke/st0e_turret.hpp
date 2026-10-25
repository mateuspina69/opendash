// ST0E: the boss vehicle's turret and what it fires (content/yosyonke/st0e_turret.cpp; the chase: st0e_boss_vehicle.hpp).
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"
#include "game/actor_state.hpp"
#include "game/field_alias.hpp"

namespace od::port {

// exe 0x503bd0: pool 0x20 type 0x7b sub 0 (spawned by the vehicle's first sub-state) -- the turret: arms, cannons,
// beam, core
void bossTurretUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x5cc850: pool 0x40 stage type 0x24 -- the bomb the turret (exe 0x504810) and the riding Servbots lob at the
// defended vehicle
void turretBombUpdate(const PlayerEnv& env, uint32_t actor);
// The bomb's spawn bytes (pool 0x40 stage type kBombType): lobbed (thrown by a Servbot), its damage, held by a
// Servbot (still until thrown); its state kBombBlast bursts it
struct BombSpawnParams {
    explicit BombSpawnParams(const ProjectileActor& actor) : BombSpawnParams(const_cast<ProjectileActor&>(actor)) {}
    explicit BombSpawnParams(ProjectileActor& actor) : flying(actor.stepCounter), damage(actor.params[0]), held(actor.params[1]) {}
    uint8_t& flying;
    FieldAlias<int8_t, uint8_t> damage;
    uint8_t& held;
};
constexpr uint8_t kBombType = 0x24;
constexpr uint8_t kBombBlast = 2;
// exe 0x5d0430: pool 0x40 stage type 0x2e -- the turret cannons' shot (exe 0x505d80)
void turretShotUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x43a760: pool 0xa0 stage type 0x1b -- the glow billboard (variant 1: the beam's muzzle glow, exe 0x505330)
void beamGlowUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
