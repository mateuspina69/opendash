#include "app/photo_mode.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace od::app {

void PhotoMode::enter(const od::Camera& from, float focus) {
    active = true;
    resuming = false;
    capture = false;
    cam = from;
    speed = std::max(focus, 1.0f);
}

void PhotoMode::leave() {
    active = false;
    resuming = true;
    capture = false;
}

void PhotoMode::step(const PhotoInput& in, float dt) {
    cam.yaw += in.look[0];
    cam.pitch = std::clamp(cam.pitch + in.look[1], -kMaxPitch, kMaxPitch);
    cam.roll = std::clamp(cam.roll + in.roll, -kMaxRoll, kMaxRoll);
    if (in.zoom != 0.0f) cam.fovY = std::clamp(cam.fovY * std::exp(-in.zoom), kMinFov, kMaxFov);
    // forward along the view, right level with the ground, up straight up (y up)
    const float cp = std::cos(cam.pitch), sp = std::sin(cam.pitch), sy = std::sin(cam.yaw), cy = std::cos(cam.yaw);
    const float fwd[3] = {sy * cp, sp, cy * cp}, right[3] = {cy, 0.0f, -sy}, up[3] = {0.0f, 1.0f, 0.0f};
    const float d = speed * (in.fast ? kFastScale : 1.0f) * dt;
    for (int c = 0; c < 3; ++c) cam.eye[c] += (right[c] * in.move[0] + up[c] * in.move[1] + fwd[c] * in.move[2]) * d;
}

bool photoModeAllowed(bool play, bool titleMenus, bool coop, bool scripted) {
    return play && !titleMenus && !coop && !scripted;
}

std::filesystem::path photoPath(const std::filesystem::path& dir, const std::tm& when) {
    char base[48];
    std::snprintf(base, sizeof base, "opendash_%04d%02d%02d_%02d%02d%02d", when.tm_year + 1900, when.tm_mon + 1,
                  when.tm_mday, when.tm_hour, when.tm_min, when.tm_sec);
    std::filesystem::path p = dir / (std::string(base) + ".png");
    std::error_code ec;
    for (int n = 2; std::filesystem::exists(p, ec); ++n) p = dir / (std::string(base) + "_" + std::to_string(n) + ".png");
    return p;
}

} // namespace od::app
