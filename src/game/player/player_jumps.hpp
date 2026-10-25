// The player in the air (game/player/player_jumps.cpp): the air control and the landing every fall shares.
#pragma once

namespace od::port {

struct Player;

// exe 0x577310(player, mode): the stick steers the air speeds (the exe subtracts each
// step twice), the turn and the lean; on the ground the landing: the speeds stopped, a throw in the air back to the
// hold, the landing step of the mode (the jumps: RunningJump's Landing; the skate fall: its Land), the arm down, the
// landing sound and splashes
void airControl(Player& p, int mode);
constexpr int kAirJump = 0, kAirRunningJump = 1, kAirSkateFall = 2;

} // namespace od::port
