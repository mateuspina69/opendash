// OpenDash's modern controls driving the game camera (not the exe): one camera. The modern camera controller (app:
// mouse / right stick, lock-on framing, wall pull-in) drives the follow camera's yaw / pitch / look instead of running
// beside it, so the game logic sees the view on screen: the view transform (screen-centre target, on-screen checks,
// effects) and the look pitch the player's aim pitch follows (the shots tilt with the camera, as L2's look mode does).
// Off in classic controls and while the game runs its own camera (script / blend modes, events).
#pragma once

namespace od::port {

struct CameraDrive {
    bool on = false;
    int yaw = 0;   // 12-bit, the convention of the camera's pose yaw (game/camera_state)
    int pitch = 0; // 12-bit signed, the pose pitch (positive: the eye above, looking down)
    int dist = 0;  // eye -> target, game units (after the wall pull-in)
};

// the drive the host sets for the next ticks
void cameraSetDrive(const CameraDrive& d);
// the follow camera is driven
bool cameraDriven();
// The follow camera's step while driven (game/camera): the room's base distance / height stay the game's (its follow
// style computed without the look) -- the controller's base; the pose yaw / pitch from the drive, the look pitch =
// pitch - the base pitch (within the aim's +-0x200; not while locked on: the lock turns the look toward the target and
// completes / releases only once it gets there), and the view built at the driven (wall pulled-in) distance
// cameraDriveDistance(), not stored. False when not driven.
bool cameraDriveFollow();
// the driven distance
int cameraDriveDistance();

} // namespace od::port
