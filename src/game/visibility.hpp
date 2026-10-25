// What the camera sees: whether a point is on screen within the draw range (or near the player), the test the enemies
// and props use to sleep, despawn or wait while off screen (exe 0x5e59a0).
#pragma once

#include <cstdint>

#include "game/vectors.hpp"

namespace od::port {

// exe 0x5e59a0(pos, dist): the point (integer parts) projected with the camera: 0 when on screen (x < 320, y < 240)
// and nearer than the stage's draw range (depth / 4 below the depth range); else 0 when nearer the player than `dist`
// (unsigned), -1 otherwise. The projection's registers are left as the exe leaves them (its float scratch is the
// draw pass's, not kept).
int32_t pointVisibleOrNear(const int16_t v[3], uint32_t dist);
int32_t pointVisibleOrNear(const Fixed16Vec3& pos, uint32_t dist);
// exe 0x5e59a0(pos, 0) as content/forbidden_island/elephant_boss's transcription has it: the same answer without the
// projection registers' writes (the exe leaves its point in them)
int32_t pointOnScreenUnrecorded(const Fixed16Vec3& pos);

} // namespace od::port
