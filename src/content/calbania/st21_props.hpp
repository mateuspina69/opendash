// The static classes of ST21 / ST22 (the lure piece, exe 0x596e60) and ST25 (the rising model, exe 0x5957d0),
// content/calbania/st21_props.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x596e60 (ST21 / ST22 static): a bouncing liftable / throwable piece (lures the nearby actors; breaks on hits)
void lurePieceUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x5957d0 (ST25 static): a script-driven rising / bobbing / sinking model with dust bursts (scripts 5 / 6 / 7 / 9 /
// 10)
void st25RisingModelUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
