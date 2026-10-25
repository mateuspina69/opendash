// The player's lock-on: the locked target's actor by its slot, the aim pitch, the camera's look pitch and the body yaw
// turned toward its hit point, the arm raised once both are reached, the lock-on sound; and the lock dropped (the soft
// target of the screen's centre kept).
#pragma once

namespace od::port {

struct Player;

// exe 0x5780e0: locked on (the lock target's slot): the target actor, the aim pitch toward its hit point (from 0x80 above
// the feet) 0x40 a tick, the camera's look pitch too (unless the lock pitch is set and the point on screen y 0x10..0xc0),
// the body yaw 0x80 a tick (0x40 airborne); both reached with a lock request: the request fired (the arm's queued shot);
// the lock-on sound when its countdown runs out
void trackLockOn(Player& p);
// exe 0x578500: no lock-on: the soft target's actor kept (or the targets cleared), the lock request bits but kLockKeepAim
// cleared
void dropLockOn(Player& p);

} // namespace od::port
