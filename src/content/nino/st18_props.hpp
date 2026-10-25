// ST18 (Nino) scenery classes and effects: the still models and the sun, content/nino/st18_props.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x58ffa0 (ST18 static type 1 sub 0, rooms 0 / 1): a still solid model
void st18StillModelUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x417250 (ST18 pool e0 type 1): the sun and its lens flares
void st18SunUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x5abb70 (ST18 static type 0x2d subs 1 / 3, after script 0x37): a still model
void st18SquarePropUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
