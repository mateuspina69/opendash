// The ST3C rising / sinking model (static, exe 0x596170), content/saul_kada/st3c_rising_model.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x596170 (ST3C static): variant 0 rises out of the floor and bobs; the others sink
void st3cRisingModelUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
