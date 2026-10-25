// The ST00 prop (static type 1, exe 0x58d950), content/saul_kada/st00_spin_prop.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x58d950 (static ST00 type 1): a prop; variant 2 drawn as scenery, variant 4 spins
void spinPropUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
