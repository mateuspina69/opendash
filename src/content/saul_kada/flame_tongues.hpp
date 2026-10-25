// A pair of flame / spark tongues (pool 0xa0, exe 0x42dde0), content/saul_kada/flame_tongues.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x42dde0 (pool 0xa0): a pair of flame / spark tongues thrown outward (two OT F3 triangles)
void tonguesUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
