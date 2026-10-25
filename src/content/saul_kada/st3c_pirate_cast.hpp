// The pirates of the pirate base's cutscenes (ST3C pool 0x20 type 0xc, exe 0x466f00),
// content/saul_kada/st3c_pirate_cast.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x466f00 (pool 0x20 type 0xc: ST3C sub 2): the pirates of the base's cutscenes (the cue chain of the script)
void pirateCastUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
