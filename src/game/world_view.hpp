// A session's world has no screen: each player's view is on their own machine. What the game
// decides by the screen there goes by the players instead: "seen" is within kWorldViewRange of any player, so that no enemy
// sleeps while a teammate looks at it, and a direction taken from the view is the player's facing. Single player and the
// windowed game keep the camera.
#pragma once

#include <cstdint>

namespace od::port {

struct Player;

constexpr uint32_t kWorldViewRange = 0xe00; // the far plane of a room at the game's largest scale (the main mode's table)

void setWorldHasNoView(bool noView);
bool worldHasNoView();
// the yaw a direction "as the view faces" takes, for what happens near this player
int16_t viewYawNear(const Player& p);

} // namespace od::port
