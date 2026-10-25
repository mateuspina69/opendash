// The player hanging: from an actor's bar (the bar's class asks for the grab: playerBarGrab, and places the hanging
// player: playerPlaceAt) and from a ledge (grabbed while falling next to a wall).
#pragma once

namespace od::port {

struct Actor;

// exe 0x573bd0(bar): the bar grab. Not busy: hanging from this bar -> 2 while hanging and nothing hit (a hit lets the bar
// go); else, jumping / falling within the bar's interaction box (its centre turned by the bar's yaw, its reach, the
// height of the hands over the feet) -> the grab: sub-state BarHang, 1. 0: none
int playerBarGrab(const Actor& bar);
// exe 0x573d70(bar, keepYaw, x, y, z): the player placed at the bar's offset (x / z turned by the yaw; the hands 0xb4
// above the feet). keepYaw 0: the player turned to face the bar (its yaw - a half turn); else the bar takes the
// player's facing. The z term adds the raw sine and x (the exe's own arithmetic)
void playerPlaceAt(Actor& bar, int keepYaw, int x, int y, int z);

} // namespace od::port
