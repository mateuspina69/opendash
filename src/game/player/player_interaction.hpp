// The player's interactions: the talk / lift target query over the actor pools (the interaction box in front of the
// player, the nearest one), the buttons that ask for it, talking (the sub-state the message system sets) and the event
// poses (event modes 1 / 2 / 3).
#pragma once

#include <cstdint>

namespace od::port {

struct Player;

// how the use button asks (exe 0x57b340's mode)
constexpr int kTalkOnPress = 0;  // pressed this tick
constexpr int kTalkWhileHeld = 1; // held
// exe 0x57b340(player, mode): no weapon / carry busy, no event: the use button picks the nearest talk target (1)
int tryTalkTarget(Player& p, int mode);
// exe 0x57b2e0: the special button held with no special weapon picks the nearest liftable actor (1)
int tryLiftTarget(Player& p);

} // namespace od::port
