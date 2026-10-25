// ST31 sub 0 (Glyde's base, the box room): the grid walkers and their rolling bomb (content/nino/glyde_box_walkers.cpp),
// their class updates in the class registry (content/nino/register.cpp).
#pragma once

#include <cstdint>

namespace od::port {

struct PlayerEnv;

// exe 0x4accc0 (pool 0x20 type 0x38): the grid walkers -- they plan paths on the room's 7 x 7 walk grid to the player
// or a box, throw boxes, charge along free lines, swing, fire their guns and throw rolling bombs
void boxWalkerUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x5c8420 (pool 0x40 ST31 stage type 0x16): their rolling bomb (an arc, then it rolls until it hits or meets a
// wall)
void walkerBombUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
