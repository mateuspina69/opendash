// The ST2A sliding block (static type 0xc, exe 0x597310), content/saul_kada/st2a_slide_block.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x597310 (ST2A static type 0xc): the block sliding in line with the player, breakable
void slideBlockUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
