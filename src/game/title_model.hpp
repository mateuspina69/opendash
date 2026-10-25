// The title screen's model (pool 0x20 type 0 of the title's class table 0x88c3fc, exe 0x42a830): the model of its
// variant (retried until the bank has it), scaled up, playing its animation 1.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x42a830 (the title's pool 0x20 type 0)
void titleModelUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
