// The ST3B glow bound to its owner (static, exe 0x598150), content/saul_kada/st3b_glow.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x598150 (ST3B static): a spinning additive glow bound to its owner (a type-10 actor), fading away when the
// player leaves or the owner is gone
void ownerGlowUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
