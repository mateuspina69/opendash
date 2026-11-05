// The view transform the camera builds (exe 0x40de70's arithmetic, game/camera builds the game's with it), and a player's
// camera as a session's world keeps it: the same view rebuilt from what their machine sends (game/world_view's
// PlayerCamera).
#pragma once

#include <cstdint>

#include "game/matrix.hpp"
#include "game/world_view.hpp"

namespace od::port {

// The camera's model of exe 0x40bbb0 (a row of m times v / 4096, truncated): the exact integer sum of the three
// products, divided by 4096 with truncation. The exe sums on the x87 at 24-bit precision (v / 4096, each product and
// each partial sum rounded to a float mantissa), so the two differ when a rounding crosses an integer before the
// truncation: an approximation, not a proven equivalence
int32_t cameraTransformRow(const int16_t* row, const int32_t v[3]);
// the rotated offset of the eye from the target at `dist` (yaw / pitch 12-bit), minus the target
void cameraEyeFromTarget(int dist, int yaw, int pitch, const int32_t target[3], int32_t out[3]);
// the rotation's transpose (exe 0x40c170) and the translation of the view of the eye at `dist` from `target` (world
// units) along yaw / pitch (12-bit), in the stage's shifted units
void cameraViewOf(const int32_t target[3], int dist, int yaw, int pitch, const Matrix3& rotation, Matrix3& matrix,
                  int32_t translation[3]);

// this machine's camera as its view was last built (not known before the first)
PlayerCamera thisPlayerCamera();
void setThisPlayerCamera(const PlayerCamera& camera);
// the view transform a camera builds: its matrix (the rotation's transpose) and its translation
void playerCameraView(const PlayerCamera& camera, Matrix3& matrix, int32_t translation[3]);

} // namespace od::port
