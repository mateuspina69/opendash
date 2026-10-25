// OpenDash's modern controls driving the game camera (see camera_drive.hpp).
#include "game/camera_drive.hpp"

#include <algorithm>

#include "game/angles.hpp"
#include "game/camera_state.hpp"
#include "game/player/player_state.hpp"

namespace od::port {

namespace {

CameraDrive g_drive;

// the range of the driven look pitch around the base pitch (the aim's)
constexpr int kLookRange = 0x200;

} // namespace

void cameraSetDrive(const CameraDrive& d) { g_drive = d; }

bool cameraDriven() { return g_drive.on; }

int cameraDriveDistance() { return g_drive.dist; }

bool cameraDriveFollow() {
    if (!g_drive.on) return false;
    CameraState& cam = cameraState();
    const int pitch = signedAngle(g_drive.pitch);
    cam.pose.yaw = int16_t(g_drive.yaw & kAngleMask);
    cam.pose.pitch = int16_t(pitch);
    // the look only without a lock-on target: locked, the lock-on (game/player/player_lock_on) turns it toward the
    // target and the lock completes (and later releases) only once it got there
    if (!player().lockedActor)
        cam.lookPitch = int16_t(std::clamp(pitch - int(cam.basePose.pitch), -kLookRange, kLookRange));
    return true;
}

} // namespace od::port
