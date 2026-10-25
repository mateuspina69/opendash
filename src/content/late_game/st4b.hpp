// ST4B (the first visit's cutscene of script 0x59 and the boss fight that follows it): stage classes (see
// content/late_game/st4b_st4c_rooms script 0x59: cast 0x8aa3d0 / 0x8aa584, records 0x8aa6b8 at its end).
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x478a90 (pool 0x20 type 0x16): a cast member of script 0x59 (variants 0..2 by the timeline's tick and the
// scene: walks in, animations 1 / 7 / 8 .. 9; face rows exe 0x4fb5d0 keyed to the cast timeline)
void script59FaceRowCastUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x44b5c0 (pool 0x20 type 2, cast 0x8aa3d0): a cast member of script 0x59 (variant 0 walks in twice, 2 face
// tracks at tick 0x14, 3 an animation switch at the script's cue 0x81)
void script59WalkInCastUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x4b8d90 (pool 0x20 type 0x3f sub 1, record 0x8aa6b8): the flying boss -- hovers at a distance of 0x3138 ..
// 0x3200 from the player (or the point (0, -0x2d0, 0)), turns to him, random attacks (dive, drop rain, shot volleys;
// taunt windows of bank 0x73c000), its hit entries, its damage, the boss gauge, half / quarter life phases (the fall,
// grounded), its death (story flag 0x681)
void st4bFlyingBossUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x595d80 (static type 6): state 1 a piece circling the arena (back to (0x1200, 0x600, -0x2400) every 0x80
// ticks); states 3 / 4 a script-driven falling piece (a hit entry, rocking exe 0x59e580)
void st4bCirclingPieceUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x595420 (static type 6 sub 1): a spinning piece moving at 0x600 along its rotation (pitch 0x80, roll -0x100; a
// hit entry, flashing when hit)
void st4bSpinningPieceUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
