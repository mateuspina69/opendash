// The placed props of ST2B (static type 1, exe 0x5906a0: pots you lift and throw, plates, the floor switches of flags
// 0x96 / 0x97) and the plates of ST32 (exe 0x590b80, the same routines), content/saul_kada/st2b_props.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x5906a0 (ST2B static type 1)
void st2bPropUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x590b80 (ST32 statics): the plates (sink under the player / move by their parameter / the floor switches)
void st32PlateUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
