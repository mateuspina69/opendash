// The cutscene casts of the ruin under Nino's guardian (ST35 scripts 0x15 / 0x16 / 0x17 / 0x19), content/nino/
// guardian_cast.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x4b2a00 (pool 0x20 type 0x3a sub 1, script 0x15): the guardian waking, its animations on the script's cues
void guardianWakesUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x4b2ba0 (type 0x3a sub 2, script 0x16): the guardian beaten, backing away
void guardianBeatenUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x4b2d00 (type 0x3a sub 3, script 0x17): the second guardian dropping in (a shake and dust on landing)
void guardianDropsInUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x4b2f50 (type 0x3a sub 4, script 0x19): the second guardian dissolving (sparks, a star, the bones fading)
void guardianDissolvesUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x488350 (pool 0x20 type 0x24 sub 1, script 0x19): a cast member fading in and out
void fadingCastUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
