// The cast of the Calbania chapter's scripts 0x69 / 0x20 (ST1F) and 0x3c (ST46), content/calbania/ch3_script_cast.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x49bea0 (ST1F type 0x2f sub 0; script 0x69 record 0x8acd70): texture-track cues by the step sub / tick
void script69CastUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x4c6fe0 (ST1F type 0x43 sub 4; script 0x20 records 0x8a3940..0x8a397c): Appo / Dah walk in and talk
void script20AppoDahUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x4e2da0 (ST46 type 0x5b sub 0; script 0x3c record 0x8a7498): animation cues, walks to the origin
void script3cCastUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
