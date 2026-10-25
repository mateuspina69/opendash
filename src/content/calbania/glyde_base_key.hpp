// The keys of Glyde's base, class exe 0x5b26d0 (static type 0x42 of ST30), content/calbania/glyde_base_key.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x5b26d0: a key of the base -- variant 0 falls (from a shot-down bomber, the last tank) and spins; variant 1 (a
// room script's) hovers and is picked up by the player within 0x32: its story flag (0x3e5..0x3e7) and the pick-up
// event
void glydeBaseKeyUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
