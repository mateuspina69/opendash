// The blizzard / sky layer (pool 0xa0 stage class type 9, exe 0x431af0; ST02 / ST03 / ST10 / ST11 / ST1D / ST49): an
// effect whose scroll words move by its speed record every tick; its 9 x 11 quads over the screen are drawn by
// game/weather_draw (its fields: game/weather_draw.hpp BlizzardLayerFields). A class of two areas (the attract intro's
// clouds, Forbidden Island's blizzard), registered by game/actors.
#pragma once

#include <cstdint>

namespace od::port {

struct PlayerEnv;

// exe 0x431af0: the layer's update
void blizzardLayerUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
