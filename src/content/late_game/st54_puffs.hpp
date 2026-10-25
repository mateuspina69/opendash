// ST54's puff emitter (static, exe 0x5c2040), content/late_game/st54_puffs.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x5c2040 (ST54 static): the puff emitter (18 smoke / dust billboards following an owner, faded by a script cue)
void puffEmitterUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
