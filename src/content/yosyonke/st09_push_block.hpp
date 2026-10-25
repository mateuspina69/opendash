// ST09's push block, static class 0x5b5010 (content/yosyonke/st09_push_block.cpp).
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x5b5010 (ST09 static type 0x4b sub 0, stage table 0x8d817c): a block the player moves by hitting it -- standing
// on it and kicking: it slides along the player's yaw (sound 0x29b); a bump from behind (0x10000) or an actor bumping
// it: a hop back; else (the kick 0x80000): a high hop (sound 0x29b); it falls when the floor drops more than 0x20;
// solid at rest. The block spawns a pool-0x60 type-0x4b variant-1 copy of itself (same class) that follows its
// position and travel yaw.
void pushBlockUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
