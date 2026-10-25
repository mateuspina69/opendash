// The pirate base's carrier (ST3B sub 0, story 12: static type 0x49, exe 0x5b4000) -- the scripted carrier that holds
// the player on it (variant 0) or a talk prop (variant != 0). See content/saul_kada/st3b_pirate_carrier.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x5b4000 (static ST3B type 0x49): variant 0 follows the script's step sub-id through its moves
// (smoke / spark effects, the player kept at a point on it), then bursts (4 blast rings + 6 smoke columns, camera
// shake) and frees itself; variant != 0 is a solid that opens message 0x28 when used
void pirateCarrierUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
