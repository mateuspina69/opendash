// What the camera sees (see visibility.hpp).
#include "game/vectors.hpp"
#include "game/visibility.hpp"

#include "game/camera_state.hpp"
#include "game/distances.hpp"
#include "game/player/player_state.hpp"
#include "game/projection.hpp"
#include "game/projection_state.hpp"
#include "game/stage_state.hpp"

namespace od::port {

namespace {

// the screen the projection maps to
constexpr uint16_t kScreenWidth = 0x140;
constexpr uint16_t kScreenHeight = 0xf0;

// a camera point on screen and nearer than the stage's draw range (its depth as the projection's output, exe 0x40c200
// / 0x40d010)
bool onScreenInRange(const CameraPoint& cp) {
    const int32_t z = cp.view[2];
    const int32_t depth = z >= projection().nearDepth ? z : (z <= 0 ? 0 : z);
    if (uint16_t(cp.sx) >= kScreenWidth || uint16_t(cp.sy) >= kScreenHeight) return false;
    return uint32_t(depth >> 2) < uint32_t(stage().depthRange);
}

} // namespace

int32_t pointVisibleOrNear(const int16_t v[3], uint32_t dist) {
    ProjectionState& proj = projection();
    proj.camera = cameraTransform().view;
    proj.point.x = v[0];
    proj.point.y = v[1];
    proj.point.z = v[2];
    const CameraPoint cp = cameraPoint(v);
    for (size_t i = 0; i < 3; ++i) proj.viewPoint[i] = cp.view[i];
    proj.clipFlags = cp.clipped ? kProjectionClipped : 0u;
    if (onScreenInRange(cp)) return 0;
    return uint32_t(distance3(player().pos, v)) < dist ? 0 : -1;
}

int32_t pointVisibleOrNear(const Fixed16Vec3& pos, uint32_t dist) {
    const int16_t v[3] = {pos.x.whole, pos.y.whole, pos.z.whole};
    return pointVisibleOrNear(v, dist);
}

int32_t pointOnScreenUnrecorded(const Fixed16Vec3& pos) {
    const int16_t v[3] = {pos.x.whole, pos.y.whole, pos.z.whole};
    return onScreenInRange(cameraPoint(v)) ? 0 : -1;
}

} // namespace od::port
