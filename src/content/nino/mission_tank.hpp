// The Nino mission tanks (ST15 / ST16 pool 0x20 class 0x4a1c40, type 0x33): fixed tanks that pick a target among the
// actors of one pool class and type (ST15: the pool-0x60 type-0x1b statics), turn their turret and barrel to its
// position led by its last move, and fire the tank shell (game/tank_shell) at it. Damage smokes them (one smoke per
// damage level, put out when the extinguisher repairs them); destroyed they explode into a wreck and cost the balance.
#pragma once

#include <cstdint>

namespace od::port {

struct PlayerEnv;

// exe 0x4a1c40: the tank's update (ActorUpdateFn)
void missionTankUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
