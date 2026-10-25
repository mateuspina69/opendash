// The guardian reaverbot (pool 0x20 class 0x455480, stage type 7: the first ruin's guardian of ST0F sub 12, script
// 0x52's cast; also ST51's): a heavy walker that turns toward the player, looks around when it loses it, slams its
// fists (shockwaves, a strong hit, a fist stuck in a stronger wall), reels when its weak point at the head is hit from
// the front, sparks below 0x41 hit points, and collapses in blasts and drops. Its glow trails are game/glow_trails, its
// shockwaves game/shockwave, its sparks game/spark_emitter.
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"

namespace od::port {

struct PlayerEnv;

// the weak point's hit box (exe 0xa59e10: its result, its previous x / z, the contact's angle; the hit list's entry
// names it)
HitBox& guardianWeakPointBox();

// exe 0x455480: the guardian's update (ActorUpdateFn)
void guardianReaverbotUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
