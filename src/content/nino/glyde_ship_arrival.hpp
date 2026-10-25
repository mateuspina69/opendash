// Glyde's ship arriving (ST30 sub 2 static type 0x1b variant 1 during script 0x25, exe 0x5a1dc0; hypothesis),
// content/nino/glyde_ship_arrival.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x5a1dc0: the ship rising out of its hangar (16 debris statics around it at its start), decelerating, then
// bobbing; already up when its parameter is set
void glydeShipArrivalUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
