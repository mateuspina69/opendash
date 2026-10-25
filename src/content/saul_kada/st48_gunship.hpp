// The ST48 gunship (static type 6 sub 0 / type 3 sub 1, exe 0x593190), see content/saul_kada/st48_gunship.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x593190: the gunships of the ST48 field encounters (their class records 0x9095c0: variants 0x80 / 0x81) --
// cruise, strafe, climb, fire pairs of shots (pool 0x40 type 9), crash when shot down
void st48GunshipUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
