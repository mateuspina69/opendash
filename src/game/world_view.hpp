// A session's world has no screen: each player's view is on their own machine. What the game
// decides by the screen there goes by the players instead: "seen" is in front of a player's camera (behind them along their facing) within kWorldViewRange, so that no enemy
// sleeps while a teammate looks at it, and a direction taken from the view is the player's facing. Single player and the
// windowed game keep the camera.
#pragma once

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

} // namespace od::port
