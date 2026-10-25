// The reaverbot turret (pool 0x20 class 0x458400, stage type 8: the ruins ST0F, ST2E, ST41 / ST42, ST4D..ST4F): a slow
// walker that faces the player and strikes when it is in its melee box, or charges (game/charge_glow) and fires a
// homing bullet (game/turret_bullet) from its bones; four behaviours by variant (stand / walk out to a range and back /
// walk around the walls and neighbours / advance while the player is in its far box); staggered below half its hit
// points; its death is an explosion and the drops.
#pragma once

#include <cstdint>

namespace od::port {

struct PlayerEnv;

// exe 0x458400: the turret's update (ActorUpdateFn)
void reaverbotTurretUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
