// A session's world has no screen: each player's view is on their own machine. What the game decides by the screen there
// goes by the players' cameras instead: "seen" is what one of them shows -- each player's own camera as their machine sends
// it, or, before it has come, one behind them along their facing within kWorldViewRange -- so that no enemy sleeps while a
// teammate looks at it, and a direction taken from the view is the player's facing. Single player and the windowed game
// keep the camera.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace od::port {

struct Player;

constexpr uint32_t kWorldViewRange = 0xe00; // the far plane of a room at the game's largest scale (the main mode's table)

void setWorldHasNoView(bool noView);
bool worldHasNoView();
// A player's game in a session: the room's logic runs in its world, this game only takes its doors
void setWorldIsElsewhere(bool elsewhere);
bool worldIsElsewhere();
// the yaw a direction "as the view faces" takes, for what happens near this player
int16_t viewYawNear(const Player& p);

// A player's camera as their machine built its view last (game/camera's buildViewTransform): the point it aims at, the
// eye's distance from it and its yaw / pitch / roll (12-bit). What it shows is the same view rebuilt (game/visibility).
struct PlayerCamera {
    std::array<int16_t, 3> target{};
    int16_t distance = 0;
    std::array<uint16_t, 3> angles{}; // yaw, pitch, roll
    bool known = false;               // a camera has been built (or received)
    friend bool operator==(const PlayerCamera&, const PlayerCamera&) = default;
};
// a world: what a player's machine sent of their camera (a player who leaves or joins has none until theirs comes)
void setWorldPlayerCamera(size_t id, const PlayerCamera& camera);
const PlayerCamera& worldPlayerCamera(size_t id);

} // namespace od::port
