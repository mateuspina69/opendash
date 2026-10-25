// The snow of the towns, class 0x591640 (static pool type 2, global or per-stage: ST08 / ST09 / ST0B / ST1C / ST5A):
// 32 screen-space particles around the camera, moved by the wind and the camera's motion and born again on the box's
// edges by the RNG. The logic; the sprites (exe 0x591c10, a 0x28-byte packet per particle on the OT) are drawn by
// game/weather_draw from the draw block this logic keeps (SnowDrawBlock).
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x591640 (ActorUpdateFn)
void snowUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
