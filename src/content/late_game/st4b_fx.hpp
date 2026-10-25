// ST4B: the effect / helper classes of the boss (exe 0x4b8d90, content/late_game/st4b), content/late_game/st4b_fx.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x5b8000 (static ST4B type 0x52 sub 0; two spawned by the boss's exe 0x4bad40, their eye point = the boss's block
// +8 / +0x10): an eye's glow trail -- a ring of 0x1a past positions of the eye point drawn as an additive ribbon
// (DR_MODE + 4 POLY_G4 per segment, OT); freed when the point's word +6 (the block's +0xe / +0x16) becomes non-zero
void eyeTrailUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x5d0cb0 (pool 0x40 ST4B type 0x30; the boss's volley exe 0x4ba950): a shot flying at its speed along its
// rotation for 0x64 ticks -- a hit capsule (previous -> current position), two billboards and a ring; hit / wall /
// timeout -> a hit spark and freed
void volleyShotUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x595e80 (static ST4B type 6 sub 2): the script-driven falling piece (states 3 / 4 / 5 of exe 0x595d80: table
// 0x8ce090 = 0x8ce084 + 0xc)
void scriptFallingPieceUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
