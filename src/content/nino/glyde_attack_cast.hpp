// The cast of the cutscenes of Glyde's attack on Nino (ST17 script 0x39, ST18 script 0x3b), content/nino/
// glyde_attack_cast.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x498860 (ST17 type 0x2d sub 2): the Birdbots of script 0x39 (animated or frozen, a blinking part pair)
void script39BirdbotUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x498bf0 (ST18 type 0x2d sub 3): the Birdbots of script 0x3b (hops / fly up, turn and spark)
void script3bBirdbotUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x4e34f0 (ST17 type 0x5c): script 0x39's cast member that comes down and lands; texture cells on the step tick
void script39DescenderUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x4e30f0 (ST17 type 0x5b): script 0x39's cast member that comes down and lands; a blinking part pair
void script39BlinkerUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x44acc0 (ST17 type 2): script 0x39's cast member with timed animations / cells; variant 1 rises away
void script39RiserUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
