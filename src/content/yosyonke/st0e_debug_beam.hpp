// The ST0E pad-steered debug beam, class 0x5cebe0 (pool 0x40 stage type 0x28; content/yosyonke/st0e_debug_beam.cpp).
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x5cebe0 (ST0E stage type 0x28): a pad-steered debug beam (rings + G4 body as the ST35 beam); no spawner found
void debugBeamUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
