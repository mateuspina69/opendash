// The ST54 swinging model (static, exe 0x5b7e90), content/late_game/st54_props.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x5b7e90 (ST54 static): a swinging model (its swing started by a script through its sub-state)
void swingingModelUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
