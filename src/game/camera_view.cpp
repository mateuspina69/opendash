// The camera's view transform (see camera_view.hpp).
#include "game/camera_view.hpp"

#include "game/angles.hpp"
#include "game/fixed_point.hpp"
#include "game/stage_state.hpp"

namespace od::port {

namespace {
PlayerCamera g_camera;
} // namespace

int32_t cameraTransformRow(const int16_t* row, const int32_t v[3]) {
    const int64_t s = int64_t(row[0]) * v[0] + int64_t(row[1]) * v[1] + int64_t(row[2]) * v[2];
    return int32_t(s / 4096);
}

void cameraEyeFromTarget(int dist, int yaw, int pitch, const int32_t target[3], int32_t out[3]) {
    out[0] = ((fixedSin(yaw) * dist >> 12) * fixedCos(pitch) >> 12) - target[0];
    out[1] = (fixedSin(pitch) * dist >> 12) - target[1];
    out[2] = ((fixedCos(yaw) * dist >> 12) * fixedCos(pitch) >> 12) - target[2];
}

void cameraViewOf(const int32_t target[3], int dist, int yaw, int pitch, const Matrix3& r, Matrix3& t, int32_t translation[3]) {
    const int shift = stage().coordinateShift & 0x1f;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) t.e[i * 3 + j] = r.e[j * 3 + i];
    const int32_t shiftedTarget[3] = {target[0] << shift, target[1] << shift, target[2] << shift};
    int32_t shifted[3];
    cameraEyeFromTarget(dist << shift, yaw, pitch, shiftedTarget, shifted);
    for (size_t i = 0; i < 3; ++i) translation[i] = cameraTransformRow(t.e + i * 3, shifted);
}

PlayerCamera thisPlayerCamera() { return g_camera; }
void setThisPlayerCamera(const PlayerCamera& camera) { g_camera = camera; }

void playerCameraView(const PlayerCamera& c, Matrix3& matrix, int32_t translation[3]) {
    const int yaw = c.angles[0] & kAngleMask, pitch = c.angles[1] & kAngleMask, roll = c.angles[2] & kAngleMask;
    const Matrix3 r = rotationYXZ(-pitch & kAngleMask, yaw, roll);
    const int32_t target[3] = {c.target[0], c.target[1], c.target[2]};
    cameraViewOf(target, c.distance, yaw, pitch, r, matrix, translation);
}

} // namespace od::port
