// The flying platform boss of the Nino ruin's lowest floor (ST38 sub 11) and its attacks, content/nino/platform_boss.cpp.
#pragma once

#include <cstdint>

#include "engine/routine_ids.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_state.hpp"


namespace od::port {

// exe 0x500b50 (pool 0x20 ST38 type 0x79): the boss -- patrols the waypoints 0x897c10, then hunts (dives at the player,
// spawns its sparks); hurt only by the 0x1c0000 attacks
void platformBossUpdate(const PlayerEnv& env, uint32_t actor);
constexpr RoutineId kPlatformBossClass = RoutineId::PlatformBossUpdate;
// the waypoint the boss heads to (its x / z): the host's playtest bot waits for the boss there
struct PlatformBossTarget {
    int16_t x, z;
};
PlatformBossTarget platformBossTarget(const AnimatedActor& boss);
// exe 0x5bab10 (pool 0x60 ST38 type 0x5a): the boss's spark effects (variants 0 / 1 / 4 gather, 2 / 3 burst; draw
// commands 0xf0), spawning the shots 0x5d0770 (variant 1), 0x5c2830 (variant 4) and pool-0x40 type 0x23 (variant 0)
void bossSparkUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x423f40 (pool 0xe0 ST38 type 0xf): a lightning bolt hanging from its parent (OT POLY_G4 quads)
void lightningBoltUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x5c2830 (pool 0x40 ST38 type 0): the aimed shot (billboard draw command 0x2c)
void aimedShotUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x5d0770 (pool 0x40 ST38 type 0x2f): the homing orb (draw command 0xf0, bolts 0x423f40)
void homingOrbUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
