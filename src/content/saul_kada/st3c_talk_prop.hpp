// The ST3C talking prop (static type 0x50 variant 1, exe 0x5b7de0), content/saul_kada/st3c_talk_prop.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x5b7de0 (static ST3C type 0x50 variant 1): a solid prop that talks (message 0x6f)
void talkPropUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
