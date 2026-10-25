// The ST09 still model, static class 0x5b6a20 (content/yosyonke/st09_props.cpp).
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x5b6a20 (ST09 static): a still model (the model by the variant; no collision)
void stillModelUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
