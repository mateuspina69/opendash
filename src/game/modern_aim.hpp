// OpenDash's free aim (modern gameplay and controls, not the exe): the shots go where the crosshair points, no lock-on
// needed. The exe's shots leave the hand along the body's facing and the aim pitch, so they never meet the point the
// view is on. Here the modern camera (app) hands over a ray through the game's aim centre (kAimCentreX / kAimCentreY,
// game/lock_on_targets), from the player's depth to the first room surface; the hit pass hands over the lock-on
// candidates' spheres. The aim point is where the ray first meets a candidate, else its end. A shot takes the angles
// from its muzzle to that point; the aim pitch +0x28 (what tilts the upper body) follows them. A lock-on keeps the
// exe's aim: the ray is off while one is held.
#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "game/hud.hpp"

namespace od::port {

struct AimRay {
    bool on = false;
    float origin[3]{}; // game units (x, y down, z)
    float dir[3]{};    // unit length
    float length = 0.0f;
};

// the ray the host sets for the next ticks
void modernAimSetRay(const AimRay& ray);
// the hit pass: the lock-on candidates among the first `count` entries of the hit list
void modernAimCollectTargets(uint32_t count);
// the angles from `from` (16.16) to the aim point, as a shot's rotation; false: no free aim now, or the point is too
// close to the muzzle to give a direction
bool modernAimAngles(const int32_t from[3], int16_t& pitch, int16_t& yaw);
// a session: the angles another machine's player (this table's) fired with, which their shots here take (set: false: the
// shot left along the body, as the exe's)
void setPlayerShotAim(size_t player, bool set, int16_t pitch, int16_t yaw);
// ... and the point another machine's player's crosshair is on now (set false: no free aim)
void setPlayerAimPoint(size_t player, bool set, const std::array<int16_t, 3>& point);
// this machine's player: the point the crosshair is on (false: no free aim now)
bool modernAimPointNow(std::array<int16_t, 3>& out);
// the crosshair, while the player can fire with the free aim: white, red over a target
std::vector<hud::Prim> modernAimDraw();

} // namespace od::port
