// The floor switches of the Nino ruin: ST36's switch and the gates it moves, ST37's pressure plates (static type 1 of
// their stage tables), content/nino/floor_switch.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x590ba0: states [0x8cdd84]; variant 0 the switch (sinks while the player stands on it), variants 1 / 2 the
// gates (their parameter: 0 they sink back to their home height, 1 they rise to y 0); a stand-on solid
void floorSwitchUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x590cd0: states [0x8cdd94]; by its last parameter (0 / 1) the plate sinks while the player or the room's box
// stands on it and sets its flag (0x96 / 0x97), else it rises back and clears it; a stand-on solid
void pressurePlateUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
