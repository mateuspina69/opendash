// The walking reaverbot (pool 0x20 class 0x451c00, stage type 5: the ruins ST0F, ST28, ST2D / ST2E, ST40..ST42, ST4A):
// it waits (for a story flag, hidden, dormant) or patrols, then circles the player, gives way to its neighbours,
// lunges, hops at it, lobs a shot (game/lobbed_shot), flinches and is knocked back / aside; it can be lifted and thrown
// (game/carry); its death is an explosion and the drops. Its shadow (exe 0x5f20f0) is render output.
#pragma once

#include <cstdint>

namespace od::port {

struct PlayerEnv;

// exe 0x451c00: the walking reaverbot's update (ActorUpdateFn)
void reaverbotWalkerUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
