// The static classes of the Manda ruins (ST12 / ST13 / ST14), content/manda/manda_ruin_props.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x59b710 (ST12 / ST13 static): the moving platform (variant & 4: the spinning spark wheel) with its wheel (static
// type 0x17 variant 4) and an optional rider (pool 0x20 type 0x22)
void mandaPlatformUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x5aa580 (ST12 static): the script-driven gates / lifts of scripts 1 / 4 (variant 0 / 1 / 2)
void mandaGateUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x5b1c10 (ST14 static): a spinning debris piece thrown out and falling until x < 0x201
void mandaDebrisPieceUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
