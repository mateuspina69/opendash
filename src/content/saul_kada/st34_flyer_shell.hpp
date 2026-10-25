// The shell the ruin flyer hangs dormant in (ST27 / ST34 pool 0x20 type 0x67 sub 0, exe 0x4ec580), see
// content/saul_kada/st34_flyer_shell.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x4ec580: it hangs dormant until the player comes within 0x700 (or it is hit), drops to the floor, then circles
// / backs off and fires breath puffs; liftable and throwable (the throw kills it); dying it releases the flyer
// (content/saul_kada/st34_ruin_flyer) and sparks
void flyerShellUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
