// The tilted model of ST03 (static, exe 0x5a7af0): bound once with its collision box and a fixed tilt
// (content/intro/st03_props.cpp).
#pragma once

#include <cstdint>

namespace od::port {

struct PlayerEnv;

// exe 0x5a7af0: the still model's update (registered in content/intro/register.cpp)
void st03TiltedModelUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
