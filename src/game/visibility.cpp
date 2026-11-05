// What the camera sees (see visibility.hpp).
#include <algorithm>
#include "game/world_view.hpp"
#include "game/player/players.hpp"
#include "game/vectors.hpp"
#include "game/actor_movement.hpp"
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

// A session's world: a point a player's camera would show -- in front of the camera that follows them (behind them along
// their facing, at the room's follow distance), within the screen's half width at the projection's focal length, nearer
// than the far plane
bool inPlayersView(const Player& p, const int16_t v[3]) {
    constexpr int kUnit = 0x400;
    int16_t forward[3] = {0, 0, 0};
    moveAlongYaw16(forward, viewYawNear(p), 0, 0, kUnit);
    const int back = int(cameraState().basePose.distance) + kFollowDistanceExtra;
    const int64_t camX = int64_t(p.pos.x.whole) - int64_t(forward[0]) * back / kUnit;
    const int64_t camZ = int64_t(p.pos.z.whole) - int64_t(forward[2]) * back / kUnit;
    const int64_t dx = v[0] - camX, dz = v[2] - camZ;
    const int64_t along = (dx * forward[0] + dz * forward[2]) / kUnit;
    const int64_t side = (dz * forward[0] - dx * forward[2]) / kUnit;
    if (along <= 0 || dx * dx + dz * dz >= int64_t(kWorldViewRange) * kWorldViewRange) return false;
    const ProjectionState& proj = projection();
    if (proj.focal <= 0) return true; // no projection yet: the half-plane ahead
    return (side < 0 ? -side : side) * proj.focal <= along * proj.centreX;
}

} // namespace

int32_t pointVisibleOrNear(const int16_t v[3], uint32_t dist) {
    if (worldHasNoView()) // seen by a player's camera, or near a player
        return anyPlayer([&](const Player& p) { return inPlayersView(p, v) || uint32_t(distance3(p.pos, v)) < dist; }) ? 0 : -1;
    ProjectionState& proj = projection();
    proj.camera = cameraTransform().view;
    proj.point.x = v[0];
    proj.point.y = v[1];
    proj.point.z = v[2];
    const CameraPoint cp = cameraPoint(v);
    for (size_t i = 0; i < 3; ++i) proj.viewPoint[i] = cp.view[i];
    proj.clipFlags = cp.clipped ? kProjectionClipped : 0u;
    if (onScreenInRange(cp)) return 0;
    return anyPlayer([&](const Player& p) { return uint32_t(distance3(p.pos, v)) < dist; }) ? 0 : -1;
}

int32_t pointVisibleOrNear(const Fixed16Vec3& pos, uint32_t dist) {
    const int16_t v[3] = {pos.x.whole, pos.y.whole, pos.z.whole};
    return pointVisibleOrNear(v, dist);
}

int32_t pointOnScreenUnrecorded(const Fixed16Vec3& pos) {
    const int16_t v[3] = {pos.x.whole, pos.y.whole, pos.z.whole};
    if (worldHasNoView()) return anyPlayer([&](const Player& p) { return inPlayersView(p, v); }) ? 0 : -1;
    return onScreenInRange(cameraPoint(v)) ? 0 : -1;
}

} // namespace od::port
