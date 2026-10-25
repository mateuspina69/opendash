// The talking cast of the cutscenes around Glyde's last attack on Nino (pool 0x20 type 0x69),
// content/nino/glyde_attack_talkers.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x4f07b0 (ST17 script 0x39, records 0x8a6a9c / 0x8a6ab0): a script puppet -- its animation and its two texture
// tracks (its face) keyed on the script's step tick
void st17TalkerUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x4f0a50 (ST18 scripts 0x3a / 0x3b, variants 0..2): the same, with a part pair swapped on animation 3
void st18TalkerUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
