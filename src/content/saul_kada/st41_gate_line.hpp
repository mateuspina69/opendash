// The ST41 / ST42 gate line (pool e0 type 0x2a, exe 0x427500), content/saul_kada/st41_gate_line.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x427500 (pool e0 type 0x2a: ST41 / ST42): a gate line of 6 posts; crossing it sets flag 0x155 (sound 0x2bf)
void gateLineUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
