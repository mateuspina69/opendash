// OpenDash's photo mode: the game stands still (no logic tick, no sound, no input reaches it) while a free camera
// looks at the frozen picture without the 2D layer, and saves it. Leaving it puts the game's camera back and the game
// goes on from the same tick; nothing of the game's state is touched. Off in a co-op session (the other worlds run on).
#pragma once

#include <cstdint>
#include <ctime>
#include <filesystem>
#include <string>

#include "render/renderer.hpp"

namespace od::app {

// one frame's camera input: move (right, up, forward in the view), look (yaw, pitch radians), roll (radians), zoom
// (> 0 narrows the view), fast (the run modifier)
struct PhotoInput {
    float move[3]{};
    float look[2]{};
    float roll = 0.0f;
    float zoom = 0.0f;
    bool fast = false;
};

struct PhotoMode {
    static constexpr float kMinFov = 0.15f, kMaxFov = 2.2f, kMaxPitch = 1.55f, kMaxRoll = 1.0f;
    static constexpr float kFastScale = 4.0f;

    bool active = false;
    // left, waiting for the buttons held when it closed to come up: the game stays still until then, so none of them
    // reaches it as a press
    bool resuming = false;
    bool hint = true;      // the controls shown
    bool capture = false;  // save this frame's picture
    od::Camera cam{};
    float speed = 1.0f;    // world units a second

    // from the camera drawn this frame; `focus` = the distance to what it looks at (the player): the speed
    void enter(const od::Camera& from, float focus);
    void leave();
    // the game runs (a tick may happen): neither in the mode nor waiting for the release
    bool gameRuns() const { return !active && !resuming; }
    void step(const PhotoInput& in, float dt);
};

// the mode may open: in play, out of the title's menus, in a game of one's own (no co-op session), not a scripted run
bool photoModeAllowed(bool play, bool titleMenus, bool coop, bool scripted);

// <dir>/opendash_YYYYMMDD_HHMMSS[_N].png, N from 2 when that second's name is taken
std::filesystem::path photoPath(const std::filesystem::path& dir, const std::tm& when);

} // namespace od::app
