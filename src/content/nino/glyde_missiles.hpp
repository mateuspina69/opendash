// ST18 (Nino island) at stories 9 / 0xa, Glyde's last attack: Glyde's ship launching its missiles, the missiles and
// their flares, content/nino/glyde_missiles.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x5af3d0 (static stage type 0x38): Glyde's ship -- bobs, launches the missiles in bursts by its launch script
void missileShipUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x5a7b30 (static stage type 0x24): a missile -- homes on its target (variant 0 the player / 1 the centre / 2
// around the machine), lifted, carried and thrown back (homing on the lock-on target)
void glydeMissileUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x4370b0 (pool 0xa0 stage type 0x10): a glow sprite following an actor (the missile's flare)
void actorGlowUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
