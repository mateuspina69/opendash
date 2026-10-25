// The cast of the Nino chapter's scripts 0x31 (ST46), 0x32 (ST19) and 0x37 (ST18) and script 0x37's flash effect
// 0x4304d0, content/nino/ch2_script_cast.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x44a470 (ST46 type 2 sub 0; script 0x31 record 0x8a5cec): the take-off's cast -- texture-track cues and a type
// 0x47 child (0x4cd290)
void takeOffCastUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x497be0 (ST19 type 0x2d sub 0; script 0x32 records 0x8a5fe8..0x8a6038): the talking cast, mouth flaps by tick
// windows
void script32CastUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x498160 (ST18 type 0x2d sub 2; script 0x37 records 0x8a6778..): the talking cast; variant 5 lying with smoke
void script37CastUpdate(const PlayerEnv& env, uint32_t actor);

// exe 0x4304d0 (pool 0xa0 ST18 stage type 5; script 0x37 step 0x5296b0): a 3-tick flash (two 0xf0 discs + 16 G4 spikes)
void script37FlashUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
