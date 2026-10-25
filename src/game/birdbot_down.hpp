// The knocked-down Birdbot (pool 0x20 class 0x492330, type 0x2d sub-type 0 of ST15..ST17, ST30 / ST31): what a Birdbot
// soldier (game/birdbot_soldier) leaves when its hit points run out (variant: how it went down | 0x80). Bounced off
// spinning (smoking), it gets up and runs around the player; it can be lifted and thrown; it is freed far away, in the
// water or after 0x1e ticks off screen.
#pragma once

#include <cstdint>

namespace od::port {

struct PlayerEnv;

// exe 0x492330: the knocked-down Birdbot's update (ActorUpdateFn)
void birdbotDownUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
