// The snow reaverbot (pool 0x20 class 0x4a2a50, stage type 0x34 and others of ST1D, ST2D and ST4F; hypothesis: the
// "wolf reaverbot"): sub-type 0 buried in the snow until the player comes near -- it bursts out (snow chunks), howls,
// circles, lunges, bounces off walls, retreats, gives way to its pack; sub-type 1 a pack member that leaps in, runs
// and drops off a ledge (ST1D's pack timer); sub-type 4 a sentry that paces out from its spawn point and back (ST4F).
// Its death is an explosion, a blast and the drops. The shadow's entry (exe 0x5f20f0) is render output, its ground
// probe is kept (the next pack member's snow puffs copy the collision scratch it leaves).
#pragma once

#include <cstdint>

namespace od::port {

struct PlayerEnv;

// exe 0x4a2a50: the snow reaverbot's update (ActorUpdateFn)
void snowReaverbotUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
